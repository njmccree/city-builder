#include "citysim/debug/terrain_walk_grid.hpp"

#include "citysim/profiling/profile.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/water_state.hpp"
#include "citysim/world/world_terrain.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace citysim::debug {

namespace {

constexpr std::uint32_t k_block_cells = 16; // 64 m: where a river is looked for at all
constexpr std::uint32_t k_blocks_per_side = TerrainWalkGrid::k_cells_per_tile / k_block_cells;

struct TileInputs {
    const world::WorldTerrain* terrain = nullptr;
    const world::WaterState* water = nullptr;
    world::TileCoord coord;
    const world::CityTile* tile = nullptr;        // null if the tile is not active
    const world::FloodMap::Grid* flood = nullptr; // null if nothing is flooded on the tile
    float max_slope_sq = 0.0F;
};

// Is the middle of cell (cx, cy) of the tile inside a river channel that holds water there?
bool in_water_channel(const TileInputs& in, std::uint32_t cx, std::uint32_t cy, float ground) {
    const world::RiverNetwork& rivers = in.terrain->map()->rivers();
    const world::WorldPos origin = world::tile_origin(in.coord);
    const world::WorldPos pos{origin.x + (static_cast<double>(cx) + 0.5) * world::k_city_sample_spacing_m,
                              origin.z + (static_cast<double>(cy) + 0.5) * world::k_city_sample_spacing_m};
    bool inside = false;
    float nearest = 0.0F;
    float level = 0.0F;
    // The nearest centerline within its half width decides (ties go to the first visited, which depends only
    // on the data), as world::water_level_at does.
    rivers.for_each_near(pos, rivers.max_half_width_m(), [&](const world::ChannelSample& channel) {
        if (channel.distance_m > channel.half_width_m) {
            return;
        }
        if (!inside || channel.distance_m < nearest) {
            inside = true;
            nearest = channel.distance_m;
            level = channel.level_m + in.water->level_offset(channel.reach);
        }
    });
    return inside && level - ground > 0.0F;
}

// The walkability of one 512 x 512 tile of cells, written into the grid's words; returns how many cells
// changed.
std::size_t compute_tile(const TileInputs& in, std::vector<std::uint64_t>& words, std::uint32_t column0,
                         std::uint32_t row0) {
    const world::RiverNetwork& rivers = in.terrain->map()->rivers();
    // Which 64 m blocks have a river near them (so the per-cell channel search is skipped elsewhere).
    std::vector<std::uint8_t> river_block(k_blocks_per_side * k_blocks_per_side, 0);
    if (in.tile != nullptr && !rivers.empty()) {
        const world::WorldPos origin = world::tile_origin(in.coord);
        const double block_m = static_cast<double>(k_block_cells) * world::k_city_sample_spacing_m;
        for (std::uint32_t by = 0; by < k_blocks_per_side; ++by) {
            for (std::uint32_t bx = 0; bx < k_blocks_per_side; ++bx) {
                const double x0 = origin.x + static_cast<double>(bx) * block_m;
                const double z0 = origin.z + static_cast<double>(by) * block_m;
                river_block[by * k_blocks_per_side + bx] =
                    rivers.any_near_box(x0, z0, x0 + block_m, z0 + block_m, rivers.max_half_width_m()) ? 1
                                                                                                       : 0;
            }
        }
    }

    const std::uint32_t samples = world::k_city_tile_samples;
    std::size_t changed = 0;
    for (std::uint32_t cy = 0; cy < TerrainWalkGrid::k_cells_per_tile; ++cy) {
        const std::size_t row_word =
            (static_cast<std::size_t>(row0 + cy) * TerrainWalkGrid::k_cells + column0) / 64U;
        for (std::uint32_t w = 0; w < TerrainWalkGrid::k_cells_per_tile / 64U; ++w) {
            std::uint64_t bits = 0;
            if (in.tile != nullptr) {
                for (std::uint32_t b = 0; b < 64; ++b) {
                    const std::uint32_t cx = w * 64U + b;
                    const float h00 = in.tile->sample(cx, cy);
                    const float h10 = in.tile->sample(cx + 1, cy);
                    const float h01 = in.tile->sample(cx, cy + 1);
                    const float h11 = in.tile->sample(cx + 1, cy + 1);
                    const float ground = 0.25F * (h00 + h10 + h01 + h11);
                    // The slope of the bilinear surface at the middle of the 4 m cell.
                    const float dx = ((h10 + h11) - (h00 + h01)) * 0.125F;
                    const float dz = ((h01 + h11) - (h00 + h10)) * 0.125F;
                    bool ok = dx * dx + dz * dz < in.max_slope_sq && ground > world::k_sea_level_m;
                    if (ok && in.flood != nullptr) {
                        const auto marked = [&](std::uint32_t sx, std::uint32_t sy) {
                            return (*in.flood)[static_cast<std::size_t>(sy) * samples + sx] != 0;
                        };
                        ok = !(marked(cx, cy) || marked(cx + 1, cy) || marked(cx, cy + 1) ||
                               marked(cx + 1, cy + 1));
                    }
                    if (ok &&
                        river_block[(cy / k_block_cells) * k_blocks_per_side + cx / k_block_cells] != 0) {
                        ok = !in_water_channel(in, cx, cy, ground);
                    }
                    if (ok) {
                        bits |= std::uint64_t{1} << b;
                    }
                }
            }
            std::uint64_t& word = words[row_word + w];
            changed += static_cast<std::size_t>(std::popcount(word ^ bits));
            word = bits;
        }
    }
    return changed;
}

} // namespace

TerrainWalkGrid::SyncResult TerrainWalkGrid::sync(core::World& world, core::JobSystem& jobs,
                                                  std::uint32_t city_id, const world::CityArea& area) {
    CS_PROFILE_ZONE("debug.terrain_walk_sync");
    SyncResult result;
    const auto* terrain = world.ctx().find<world::WorldTerrain>();
    const auto* water = world.ctx().find<world::WaterState>();
    if (terrain == nullptr || water == nullptr || !terrain->has_map()) {
        return result;
    }
    const auto* flood = world.ctx().find<world::FloodMap>();

    if (city_id == 0) {
        if (city_id_ != 0) {
            reset(0, 0, world::k_city_sample_spacing_m, false);
            city_id_ = 0;
            stamps_ = {};
            offsets_.clear();
            result.city_changed = true;
        }
        return result;
    }
    if (city_id != city_id_ || !(area == area_)) {
        reset(k_cells, k_cells, static_cast<float>(world::k_city_sample_spacing_m), false);
        city_id_ = city_id;
        area_ = area;
        stamps_ = {};
        offsets_.clear();
        result.city_changed = true;
    }

    // Which tiles need recomputing?
    const std::vector<float> now(water->level_offsets().begin(), water->level_offsets().end());
    const bool levels_changed = now != offsets_;
    const world::RiverNetwork& rivers = terrain->map()->rivers();
    struct Dirty {
        std::size_t stamp = 0;
        world::TileCoord coord;
        Stamp next;
    };
    std::vector<Dirty> dirty;
    for (std::uint32_t ty = 0; ty < k_tiles_per_side; ++ty) {
        for (std::uint32_t tx = 0; tx < k_tiles_per_side; ++tx) {
            const std::size_t slot = static_cast<std::size_t>(ty) * k_tiles_per_side + tx;
            const world::TileCoord coord{area.first_tile.tx + static_cast<std::int32_t>(tx),
                                         area.first_tile.ty + static_cast<std::int32_t>(ty)};
            Stamp next;
            next.tile = terrain->tile(coord);
            next.flood = flood != nullptr ? flood->grid(coord) : nullptr;
            next.built = true;
            const Stamp& old = stamps_[slot];
            bool needs = !old.built || old.tile != next.tile || old.flood != next.flood;
            if (!needs && levels_changed && !rivers.empty()) {
                const world::WorldPos origin = world::tile_origin(coord);
                needs = rivers.any_near_box(origin.x, origin.z, origin.x + world::k_tile_size_m,
                                            origin.z + world::k_tile_size_m, rivers.max_half_width_m());
            }
            if (needs) {
                dirty.push_back(Dirty{slot, coord, std::move(next)});
            }
        }
    }
    offsets_ = now;
    if (dirty.empty()) {
        return result;
    }

    // One chunk per tile; each writes its own words and reports how many cells it changed.
    std::vector<std::size_t> changed(dirty.size(), 0);
    const float max_slope = terrain->terrain_defines().walk_max_slope;
    std::vector<std::uint64_t>& words = this->words();
    jobs.parallel_for(dirty.size(), 1, [&](std::size_t begin, std::size_t end, std::size_t) {
        for (std::size_t i = begin; i < end; ++i) {
            TileInputs in;
            in.terrain = terrain;
            in.water = water;
            in.coord = dirty[i].coord;
            in.tile = dirty[i].next.tile.get();
            in.flood = dirty[i].next.flood.get();
            in.max_slope_sq = max_slope * max_slope;
            const auto tx = static_cast<std::uint32_t>(dirty[i].stamp % k_tiles_per_side);
            const auto ty = static_cast<std::uint32_t>(dirty[i].stamp / k_tiles_per_side);
            changed[i] = compute_tile(in, words, tx * k_cells_per_tile, ty * k_cells_per_tile);
        }
    });
    for (std::size_t i = 0; i < dirty.size(); ++i) {
        stamps_[dirty[i].stamp] = std::move(dirty[i].next);
        result.cells_changed += changed[i];
    }
    result.tiles_rebuilt = dirty.size();
    if (result.cells_changed > 0) {
        touch();
    }
    return result;
}

void TerrainWalkGrid::update_regions() {
    if (regions_current()) {
        return;
    }
    CS_PROFILE_ZONE("debug.walk_regions");
    const std::uint32_t cells = cell_count();
    regions_.assign(cells, 0);
    std::uint32_t label = 0;
    for (std::uint32_t seed = 0; seed < cells; ++seed) {
        if (regions_[seed] != 0 || !walkable(seed)) {
            continue;
        }
        ++label;
        regions_[seed] = label;
        queue_.clear();
        queue_.push_back(seed);
        for (std::size_t head = 0; head < queue_.size(); ++head) {
            const std::uint32_t cell = queue_[head];
            const std::uint32_t x = x_of(cell);
            const std::uint32_t y = y_of(cell);
            const auto visit = [&](std::int64_t nx, std::int64_t ny) {
                if (!walkable(nx, ny)) {
                    return;
                }
                const std::uint32_t next =
                    index_of(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
                if (regions_[next] == 0) {
                    regions_[next] = label;
                    queue_.push_back(next);
                }
            };
            visit(static_cast<std::int64_t>(x) - 1, y);
            visit(static_cast<std::int64_t>(x) + 1, y);
            visit(x, static_cast<std::int64_t>(y) - 1);
            visit(x, static_cast<std::int64_t>(y) + 1);
        }
    }
    regions_version_ = version();
}

} // namespace citysim::debug
