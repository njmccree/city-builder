#include "citysim/world/world_terrain.hpp"

#include "citysim/core/job_system.hpp"
#include "citysim/profiling/profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>

namespace citysim::world {

namespace {

const std::string k_no_map_id;

// The base heights plus a layer of offsets, as a new tile.
std::shared_ptr<const CityTile> compose(const CityTile& base, const TileDelta& delta, std::uint64_t version) {
    std::vector<float> heights(base.heights().begin(), base.heights().end());
    const std::span<const float> offsets = delta.offsets();
    for (std::size_t i = 0; i < heights.size(); ++i) {
        heights[i] += offsets[i];
    }
    return std::make_shared<const CityTile>(base.coord(), version, std::move(heights));
}

} // namespace

WorldTerrain::WorldTerrain(std::shared_ptr<const LoadedMap> map, std::uint64_t map_seed,
                           TerrainDefines defines)
    : map_(std::move(map)), map_seed_(map_seed), defines_(defines) {
    if (!map_) {
        throw std::invalid_argument("WorldTerrain needs a map");
    }
}

const std::string& WorldTerrain::map_id() const {
    return map_ ? map_->map_id() : k_no_map_id;
}

std::uint64_t WorldTerrain::package_hash() const {
    return map_ ? map_->package_hash() : 0;
}

const LoadedMap& WorldTerrain::require_map() const {
    if (!map_) {
        throw std::logic_error("WorldTerrain has no map");
    }
    return *map_;
}

void WorldTerrain::activate_tiles(core::JobSystem& jobs, std::span<const TileCoord> tiles) {
    CS_PROFILE_ZONE("world.activate_tiles");
    const LoadedMap& map = require_map();
    std::vector<TileCoord> missing;
    for (const TileCoord tile : tiles) {
        if (!map.contains_tile(tile)) {
            throw std::out_of_range("tile (" + std::to_string(tile.tx) + ", " + std::to_string(tile.ty) +
                                    ") lies outside the map");
        }
        if (!tiles_.contains(tile)) {
            missing.push_back(tile);
        }
    }
    std::sort(missing.begin(), missing.end());
    missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
    if (missing.empty()) {
        return;
    }

    // One chunk per tile; each writes only its own slot, and the slots are added in order afterwards
    // (architecture §7.2). If a tile fails, the lowest failing one is rethrown and nothing is added.
    std::vector<std::shared_ptr<const CityTile>> made(missing.size());
    const std::uint64_t seed = map_seed_;
    const TerrainDefines defines = defines_;
    jobs.parallel_for(missing.size(), 1, [&](std::size_t begin, std::size_t end, std::size_t) {
        for (std::size_t i = begin; i < end; ++i) {
            made[i] = TerrainSynth::make_tile(map, missing[i], seed, defines);
        }
    });
    for (std::size_t i = 0; i < missing.size(); ++i) {
        if (const TileDelta* delta = deltas_.find(missing[i]); delta != nullptr) {
            bases_[missing[i]] = made[i];
            made[i] = compose(*made[i], *delta, made[i]->version());
        }
        tiles_.emplace(missing[i], std::move(made[i]));
    }
}

void WorldTerrain::deactivate_tiles(std::span<const TileCoord> tiles) {
    for (const TileCoord tile : tiles) {
        tiles_.erase(tile);
        bases_.erase(tile);
    }
}

std::vector<TileCoord> WorldTerrain::active_tiles() const {
    std::vector<TileCoord> out;
    out.reserve(tiles_.size());
    for (const auto& entry : tiles_) {
        out.push_back(entry.first);
    }
    return out;
}

std::shared_ptr<const CityTile> WorldTerrain::tile(TileCoord tile) const {
    const auto it = tiles_.find(tile);
    return it == tiles_.end() ? nullptr : it->second;
}

float WorldTerrain::height_at(WorldPos pos) const {
    const LoadedMap& map = require_map();
    if (!tiles_.empty() && std::isfinite(pos.x) && std::isfinite(pos.z)) {
        // The same tie-break as the detail DEM: on an edge, the first active tile that has the point.
        const LoadedMap::Candidates candidates = LoadedMap::covering_tiles(pos);
        for (std::size_t i = 0; i < candidates.count; ++i) {
            const auto it = tiles_.find(candidates.tiles[i]);
            if (it != tiles_.end()) {
                const WorldPos origin = tile_origin(it->first);
                return it->second->height_at(pos.x - origin.x, pos.z - origin.z);
            }
        }
    }
    return map.dem_height(pos);
}

float WorldTerrain::height_at_local(const CityArea& city, LocalPos pos) const {
    return height_at(to_world(city, pos));
}

void WorldTerrain::gradient(WorldPos pos, float& dx, float& dz) const {
    constexpr double k_step = k_city_sample_spacing_m;
    const float east = height_at(WorldPos{pos.x + k_step, pos.z});
    const float west = height_at(WorldPos{pos.x - k_step, pos.z});
    const float north = height_at(WorldPos{pos.x, pos.z + k_step});
    const float south = height_at(WorldPos{pos.x, pos.z - k_step});
    dx = (east - west) / static_cast<float>(2.0 * k_step);
    dz = (north - south) / static_cast<float>(2.0 * k_step);
}

float WorldTerrain::slope_at(WorldPos pos) const {
    float dx = 0.0F;
    float dz = 0.0F;
    gradient(pos, dx, dz);
    return std::sqrt(dx * dx + dz * dz);
}

TerrainNormal WorldTerrain::normal_at(WorldPos pos) const {
    float dx = 0.0F;
    float dz = 0.0F;
    gradient(pos, dx, dz);
    // The surface y = h(x, z) has the upward normal (-dh/dx, 1, -dh/dz), normalized.
    const float length = std::sqrt(dx * dx + 1.0F + dz * dz);
    return TerrainNormal{-dx / length, 1.0F / length, -dz / length};
}

SavedTerrain WorldTerrain::saved_state() const {
    SavedTerrain saved;
    saved.map_id = map_id();
    saved.package_hash = package_hash();
    saved.generator_version = generator_version_;
    saved.active_tiles = active_tiles();
    saved.deltas = deltas_.to_saved();
    return saved;
}

void WorldTerrain::restore_deltas(std::vector<SavedTileDelta> saved) {
    deltas_ = TerrainDeltas::from_saved(std::move(saved));
    bases_.clear();
}

void WorldTerrain::apply_delta(TileCoord coord, SampleRect rect, const HeightFn& fn) {
    CS_PROFILE_ZONE("world.apply_delta");
    const LoadedMap& map = require_map();
    if (!rect.valid()) {
        throw std::invalid_argument("the edit rectangle is not inside the tile");
    }
    const auto found = tiles_.find(coord);
    if (found == tiles_.end()) {
        throw std::logic_error("tile (" + std::to_string(coord.tx) + ", " + std::to_string(coord.ty) +
                               ") is not active, so it cannot be edited");
    }
    const std::shared_ptr<const CityTile> current = found->second;
    const auto own_base = bases_.find(coord);
    const CityTile& base = own_base != bases_.end() ? *own_base->second : *current;

    // 1. Decide every new offset before changing anything, so a bad height leaves the terrain as it was.
    const std::uint32_t width = rect.x1 - rect.x0 + 1;
    std::vector<float> offsets(static_cast<std::size_t>(width) * (rect.y1 - rect.y0 + 1));
    for (std::uint32_t sy = rect.y0; sy <= rect.y1; ++sy) {
        for (std::uint32_t sx = rect.x0; sx <= rect.x1; ++sx) {
            const float height = fn(sx, sy, current->sample(sx, sy));
            const float offset = height - base.sample(sx, sy);
            if (!std::isfinite(height) || !std::isfinite(offset)) {
                throw std::invalid_argument("the terrain edit gave a height that is not a finite number");
            }
            offsets[static_cast<std::size_t>(sy - rect.y0) * width + (sx - rect.x0)] = offset;
        }
    }

    // 2. Store them, in this tile and in every neighbor that shares the sample (an edge, or a corner shared
    //    by four tiles): the same offset on the same base height keeps the shared samples bit-equal.
    std::set<TileCoord> touched{coord};
    for (std::uint32_t sy = rect.y0; sy <= rect.y1; ++sy) {
        for (std::uint32_t sx = rect.x0; sx <= rect.x1; ++sx) {
            const float offset = offsets[static_cast<std::size_t>(sy - rect.y0) * width + (sx - rect.x0)];
            deltas_.set_offset(coord, sx, sy, offset);
            struct Step {
                std::int32_t tile;
                std::uint32_t sample;
            };
            std::array<Step, 2> xs{Step{0, sx}, Step{0, sx}};
            std::array<Step, 2> ys{Step{0, sy}, Step{0, sy}};
            std::size_t x_count = 1;
            std::size_t y_count = 1;
            if (sx == 0) {
                xs[x_count++] = Step{-1, k_city_tile_samples - 1};
            } else if (sx == k_city_tile_samples - 1) {
                xs[x_count++] = Step{1, 0};
            }
            if (sy == 0) {
                ys[y_count++] = Step{-1, k_city_tile_samples - 1};
            } else if (sy == k_city_tile_samples - 1) {
                ys[y_count++] = Step{1, 0};
            }
            for (std::size_t xi = 0; xi < x_count; ++xi) {
                for (std::size_t yi = 0; yi < y_count; ++yi) {
                    if (xi == 0 && yi == 0) {
                        continue; // the sample itself
                    }
                    const TileCoord other{coord.tx + xs[xi].tile, coord.ty + ys[yi].tile};
                    if (map.contains_tile(other)) {
                        deltas_.set_offset(other, xs[xi].sample, ys[yi].sample, offset);
                        touched.insert(other);
                    }
                }
            }
        }
    }

    // 3. Replace each touched active tile by a new version (the others, and their pointers, stay).
    for (const TileCoord tile : touched) {
        deltas_.prune(tile);
        const auto active = tiles_.find(tile);
        if (active == tiles_.end()) {
            continue; // kept in the layer until the tile is activated
        }
        const std::shared_ptr<const CityTile> before = active->second;
        const auto recorded = bases_.find(tile);
        const std::shared_ptr<const CityTile> tile_base =
            recorded != bases_.end() ? recorded->second : before;
        if (const TileDelta* layer = deltas_.find(tile); layer != nullptr) {
            bases_[tile] = tile_base;
            active->second = compose(*tile_base, *layer, before->version() + 1);
        } else if (recorded != bases_.end()) { // every edit was undone: back to the synthesized heights
            std::vector<float> heights(tile_base->heights().begin(), tile_base->heights().end());
            bases_.erase(recorded);
            active->second =
                std::make_shared<const CityTile>(tile, before->version() + 1, std::move(heights));
        }
    }
}

std::optional<SavedTerrain> WorldTerrain::take_staged_restore() {
    std::optional<SavedTerrain> out = std::move(staged_restore_);
    staged_restore_.reset();
    return out;
}

} // namespace citysim::world
