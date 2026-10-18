#include "citysim/world/flood_map.hpp"

#include "citysim/profiling/profile.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace citysim::world {

namespace {

constexpr std::uint8_t k_flooded = 1;
constexpr std::uint8_t k_visited = 2;
constexpr std::uint8_t k_channel = 4;

constexpr std::int64_t k_tile_cells = 512; // intervals per tile; a tile holds 513 samples per side
constexpr double k_cell_m = 4.0;
constexpr double k_cell_area_m2 = k_cell_m * k_cell_m;
constexpr double k_scan_margin_m = 8.0; // beyond the half width, so no channel sample is missed

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

// A sample of the global 4 m grid (sample gx is at x = gx * 4) inside one tile, by tile index.
struct Slot {
    std::int32_t tile = 0;
    std::size_t index = 0;
};

// The active tiles (up to 4: a sample on a shared edge belongs to each neighbor) that hold the global sample.
template <class Layout> int slots_of(const Layout& layout, std::int64_t gx, std::int64_t gz, Slot (&out)[4]) {
    const std::int64_t tx = floor_div(gx, k_tile_cells);
    const std::int64_t tz = floor_div(gz, k_tile_cells);
    const std::int64_t sx = gx - tx * k_tile_cells;
    const std::int64_t sz = gz - tz * k_tile_cells;
    int count = 0;
    const auto add = [&](std::int64_t cx, std::int64_t cz, std::int64_t px, std::int64_t pz) {
        const std::int64_t column = cx - layout.tx0;
        const std::int64_t row = cz - layout.ty0;
        if (column < 0 || row < 0 || column >= layout.columns || row >= layout.rows) {
            return;
        }
        const std::int32_t tile = layout.index[static_cast<std::size_t>(row * layout.columns + column)];
        if (tile >= 0) {
            out[count++] = Slot{tile, static_cast<std::size_t>(pz * k_city_tile_samples + px)};
        }
    };
    add(tx, tz, sx, sz);
    if (sx == 0) {
        add(tx - 1, tz, k_tile_cells, sz);
    }
    if (sz == 0) {
        add(tx, tz - 1, sx, k_tile_cells);
    }
    if (sx == 0 && sz == 0) {
        add(tx - 1, tz - 1, k_tile_cells, k_tile_cells);
    }
    return count;
}

// The x coordinates where the edges of the rings cross the line at height z, sorted. A point (x, z) is inside
// the rings if an odd number of crossings lie to its east (the crossing-number rule), in plain double
// arithmetic.
std::vector<double> crossings_at(const std::vector<std::vector<WorldPos>>& rings, double z) {
    std::vector<double> xs;
    for (const std::vector<WorldPos>& ring : rings) {
        for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
            const WorldPos& a = ring[i];
            const WorldPos& b = ring[j];
            if ((a.z > z) != (b.z > z)) {
                xs.push_back((b.x - a.x) * (z - a.z) / (b.z - a.z) + a.x);
            }
        }
    }
    std::sort(xs.begin(), xs.end());
    return xs;
}

// True if the bounding box of the reach's floodplain overlaps (or touches) one of the tiles.
bool floodplain_touches(const Reach& reach, const std::vector<TileCoord>& tiles) {
    if (reach.floodplain.empty() || tiles.empty()) {
        return false;
    }
    double min_x = std::numeric_limits<double>::max();
    double min_z = min_x;
    double max_x = std::numeric_limits<double>::lowest();
    double max_z = max_x;
    for (const std::vector<WorldPos>& ring : reach.floodplain) {
        for (const WorldPos& point : ring) {
            min_x = std::min(min_x, point.x);
            max_x = std::max(max_x, point.x);
            min_z = std::min(min_z, point.z);
            max_z = std::max(max_z, point.z);
        }
    }
    return std::any_of(tiles.begin(), tiles.end(), [&](TileCoord tile) {
        const WorldPos origin = tile_origin(tile);
        return origin.x <= max_x && origin.x + k_tile_size_m >= min_x && origin.z <= max_z &&
               origin.z + k_tile_size_m >= min_z;
    });
}

} // namespace

std::shared_ptr<const FloodMap::Grid> FloodMap::grid(TileCoord tile) const {
    const auto it = grids_.find(tile);
    return it == grids_.end() ? nullptr : it->second;
}

double FloodMap::flooded_area_m2(std::uint32_t reach) const {
    return reach < done_.size() ? done_[reach].area_m2 : 0.0;
}

bool FloodMap::flooded_at(WorldPos pos) const {
    const TileCoord tile = tile_of(pos);
    const auto it = grids_.find(tile);
    if (it == grids_.end()) {
        return false;
    }
    const WorldPos origin = tile_origin(tile);
    const auto sx = static_cast<std::size_t>(std::floor((pos.x - origin.x) / k_cell_m));
    const auto sz = static_cast<std::size_t>(std::floor((pos.z - origin.z) / k_cell_m));
    return (*it->second)[sz * k_city_tile_samples + sx] != 0;
}

void FloodMap::sync(const WorldTerrain& terrain, std::size_t reach_count) {
    std::vector<std::pair<TileCoord, std::uint64_t>> signature;
    signature.reserve(terrain.tiles().size());
    for (const auto& [coord, tile] : terrain.tiles()) {
        signature.emplace_back(coord, tile->version());
    }
    if (done_.size() != reach_count) { // a different river layout: nothing carries over
        done_.assign(reach_count, ReachFlood{});
        applied_.assign(reach_count, 0.0F);
        stale_.assign(reach_count, false);
        grids_.clear();
        job_.reset();
        signature_.clear();
    }
    if (signature == signature_) {
        return;
    }
    // Which tiles changed their heights (an edit) while the set of active tiles stayed the same? Then only
    // the reaches whose floodplain reaches such a tile need filling again. Any other change starts every
    // reach over.
    bool same_tiles = signature.size() == signature_.size();
    std::vector<TileCoord> edited;
    for (std::size_t i = 0; same_tiles && i < signature.size(); ++i) {
        same_tiles = signature[i].first == signature_[i].first;
        if (same_tiles && signature[i].second != signature_[i].second) {
            edited.push_back(signature[i].first);
        }
    }
    signature_ = std::move(signature);
    layout_ = Layout{};
    if (!terrain.tiles().empty()) {
        std::int32_t tx1 = terrain.tiles().begin()->first.tx;
        std::int32_t ty1 = terrain.tiles().begin()->first.ty;
        layout_.tx0 = tx1;
        layout_.ty0 = ty1;
        for (const auto& entry : terrain.tiles()) {
            layout_.tx0 = std::min(layout_.tx0, entry.first.tx);
            layout_.ty0 = std::min(layout_.ty0, entry.first.ty);
            tx1 = std::max(tx1, entry.first.tx);
            ty1 = std::max(ty1, entry.first.ty);
        }
        layout_.columns = tx1 - layout_.tx0 + 1;
        layout_.rows = ty1 - layout_.ty0 + 1;
        layout_.index.assign(
            static_cast<std::size_t>(layout_.columns) * static_cast<std::size_t>(layout_.rows), -1);
        for (const auto& [coord, tile] : terrain.tiles()) {
            const auto position =
                static_cast<std::size_t>(coord.ty - layout_.ty0) * static_cast<std::size_t>(layout_.columns) +
                static_cast<std::size_t>(coord.tx - layout_.tx0);
            layout_.index[position] = static_cast<std::int32_t>(layout_.coords.size());
            layout_.coords.push_back(coord);
            layout_.tiles.push_back(tile);
        }
    }
    // Tiles that are gone lose their marks; the rest keep theirs until the reach has been filled again.
    for (ReachFlood& flood : done_) {
        for (auto it = flood.layers.begin(); it != flood.layers.end();) {
            it = terrain.tiles().count(it->first) != 0 ? std::next(it) : flood.layers.erase(it);
        }
    }
    for (auto it = grids_.begin(); it != grids_.end();) {
        it = terrain.tiles().count(it->first) != 0 ? std::next(it) : grids_.erase(it);
    }
    if (!same_tiles) {
        std::fill(stale_.begin(), stale_.end(), true);
    } else {
        const RiverNetwork& rivers = terrain.map()->rivers();
        for (std::uint32_t r = 0; r < rivers.reach_count(); ++r) {
            stale_[r] = stale_[r] || floodplain_touches(rivers.reaches()[r], edited);
        }
    }
    job_.reset(); // it would go on reading heights from tiles that have been replaced
}

void FloodMap::rebuild_grid(TileCoord tile) {
    Grid merged;
    for (const ReachFlood& flood : done_) {
        const auto it = flood.layers.find(tile);
        if (it == flood.layers.end()) {
            continue;
        }
        if (merged.empty()) {
            merged = it->second;
        } else {
            for (std::size_t i = 0; i < merged.size(); ++i) {
                merged[i] = static_cast<std::uint8_t>(merged[i] | it->second[i]);
            }
        }
    }
    const auto old = grids_.find(tile);
    if (merged.empty()) {
        if (old != grids_.end()) {
            grids_.erase(old);
        }
        return;
    }
    if (old != grids_.end() && *old->second == merged) {
        return; // same marks: keep the pointer
    }
    grids_[tile] = std::make_shared<const Grid>(std::move(merged));
}

void FloodMap::clear_reach(std::uint32_t reach, float applied, std::vector<Finished>& finished) {
    ReachFlood& flood = done_[reach];
    const bool had_flood = flood.area_m2 > 0.0;
    std::vector<TileCoord> touched;
    for (const auto& entry : flood.layers) {
        touched.push_back(entry.first);
    }
    flood = ReachFlood{};
    applied_[reach] = applied;
    stale_[reach] = false;
    for (const TileCoord tile : touched) {
        rebuild_grid(tile);
    }
    if (had_flood) {
        finished.push_back(Finished{reach, 0.0});
    }
}

void FloodMap::commit(Job& job) {
    ReachFlood next;
    next.area_m2 = static_cast<double>(job.cells) * k_cell_area_m2;
    for (std::size_t t = 0; t < job.flags.size(); ++t) {
        const Grid& flags = job.flags[t];
        if (flags.empty()) {
            continue;
        }
        const TileCoord tile = layout_.coords[t];
        Grid marks(flags.size(), 0);
        bool any = false;
        for (std::size_t i = 0; i < flags.size(); ++i) {
            marks[i] = static_cast<std::uint8_t>(flags[i] & k_flooded);
            any = any || marks[i] != 0;
        }
        if (any) {
            next.layers.emplace(tile, std::move(marks));
        }
    }
    std::vector<TileCoord> touched;
    for (const auto& entry : done_[job.reach].layers) {
        touched.push_back(entry.first);
    }
    for (const auto& entry : next.layers) {
        touched.push_back(entry.first);
    }
    done_[job.reach] = std::move(next);
    applied_[job.reach] = job.offset;
    stale_[job.reach] = false;
    std::sort(touched.begin(), touched.end());
    touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
    for (const TileCoord tile : touched) {
        rebuild_grid(tile);
    }
}

// Does some work on the job within the budget; true when the fill is complete.
bool FloodMap::run(Job& job, const RiverNetwork& rivers, std::size_t& budget) {
    const Reach& reach = rivers.reaches()[job.reach];
    const auto spend = [&](std::size_t units) { budget = budget > units ? budget - units : 0; };
    Slot slots[4];

    if (job.flags.empty()) {
        job.flags.resize(layout_.coords.size());
    }
    const auto flags_of = [&](std::int32_t tile) -> Grid& {
        Grid& flags = job.flags[static_cast<std::size_t>(tile)];
        if (flags.empty()) {
            flags.assign(CityTile::k_sample_count, 0);
        }
        return flags;
    };
    const auto mark = [&](const Slot(&where)[4], int count, std::uint8_t bits) {
        for (int i = 0; i < count; ++i) {
            flags_of(where[i].tile)[where[i].index] |= bits;
        }
    };
    // The crossings of the row last asked about; neighbors of one sample mostly share a row.
    std::int64_t cached_row = 0;
    const std::vector<double>* cached_crossings = nullptr;
    const auto inside_floodplain = [&](std::int64_t gx, std::int64_t gz) {
        if (cached_crossings == nullptr || cached_row != gz) {
            auto found = job.crossings.find(gz);
            if (found == job.crossings.end()) {
                found = job.crossings
                            .emplace(gz, crossings_at(reach.floodplain, static_cast<double>(gz) * k_cell_m))
                            .first;
            }
            cached_row = gz;
            cached_crossings = &found->second; // a node of the map: stays valid while rows are added
        }
        const double x = static_cast<double>(gx) * k_cell_m;
        const auto& xs = *cached_crossings;
        const auto east = static_cast<std::size_t>(xs.end() - std::upper_bound(xs.begin(), xs.end(), x));
        return (east & 1U) != 0;
    };

    // 1. Scan: every sample inside the channel of one of the reach's segments is a source of the fill, with
    // the
    //    reach's level at that point of the channel plus the offset.
    const std::uint32_t segment_count = static_cast<std::uint32_t>(reach.vertices.size()) - 1;
    while (!job.scanned && budget > 0) {
        if (!job.segment_open) {
            const RiverVertex& a = reach.vertices[job.segment];
            const RiverVertex& b = reach.vertices[job.segment + 1];
            const double margin = 0.5 * static_cast<double>(std::max(a.width_m, b.width_m)) + k_scan_margin_m;
            job.scan_gx0 = static_cast<std::int64_t>(std::floor((std::min(a.x, b.x) - margin) / k_cell_m));
            job.scan_gx1 = static_cast<std::int64_t>(std::ceil((std::max(a.x, b.x) + margin) / k_cell_m));
            job.scan_gz = static_cast<std::int64_t>(std::floor((std::min(a.z, b.z) - margin) / k_cell_m));
            job.scan_gz1 = static_cast<std::int64_t>(std::ceil((std::max(a.z, b.z) + margin) / k_cell_m));
            job.segment_open = true;
        }
        if (job.scan_gz > job.scan_gz1) {
            job.segment_open = false;
            if (++job.segment >= segment_count) {
                job.scanned = true;
            }
            continue;
        }
        const std::uint32_t index = rivers.first_segment(job.reach) + job.segment;
        for (std::int64_t gx = job.scan_gx0; gx <= job.scan_gx1; ++gx) {
            const int count = slots_of(layout_, gx, job.scan_gz, slots);
            if (count == 0) {
                continue;
            }
            const WorldPos pos{static_cast<double>(gx) * k_cell_m,
                               static_cast<double>(job.scan_gz) * k_cell_m};
            const ChannelSample sample = rivers.measure_segment(index, pos);
            if (sample.distance_m > sample.half_width_m) {
                continue;
            }
            if ((flags_of(slots[0].tile)[slots[0].index] & k_visited) != 0) {
                continue;
            }
            mark(slots, count, k_visited | k_channel);
            job.queue.push_back(Cell{gx, job.scan_gz, sample.level_m + job.offset});
        }
        spend(static_cast<std::size_t>(job.scan_gx1 - job.scan_gx0 + 1));
        ++job.scan_gz;
    }
    if (!job.scanned) {
        return false;
    }

    // 2. Fill: breadth-first over the 4-connected neighbors, in a fixed order.
    constexpr std::int64_t k_steps[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    while (!job.queue.empty() && budget > 0) {
        const Cell cell = job.queue.front();
        job.queue.pop_front();
        spend(1);
        for (const auto& step : k_steps) {
            const std::int64_t nx = cell.gx + step[0];
            const std::int64_t nz = cell.gz + step[1];
            const int count = slots_of(layout_, nx, nz, slots);
            if (count == 0) {
                continue;
            }
            std::uint8_t& first = flags_of(slots[0].tile)[slots[0].index];
            if ((first & k_visited) != 0) {
                continue;
            }
            mark(slots, count, k_visited);
            if (!inside_floodplain(nx, nz)) {
                continue;
            }
            const float height =
                layout_.tiles[static_cast<std::size_t>(slots[0].tile)]->heights()[slots[0].index];
            if (!(height < cell.level)) {
                continue;
            }
            mark(slots, count, k_flooded);
            ++job.cells;
            job.queue.push_back(Cell{nx, nz, cell.level});
        }
    }
    return job.queue.empty();
}

std::vector<FloodMap::Finished> FloodMap::advance(const WorldTerrain& terrain, const WaterState& water,
                                                  std::size_t sample_budget) {
    CS_PROFILE_ZONE("world.flood_advance");
    std::vector<Finished> finished;
    const RiverNetwork& rivers = terrain.map()->rivers();
    sync(terrain, rivers.reach_count());

    // Only a rising river floods; at or below its base level it keeps to its channel.
    const auto target = [&](std::uint32_t reach) { return std::max(water.level_offset(reach), 0.0F); };
    if (job_ && target(job_->reach) != job_->offset) {
        job_.reset();
    }
    std::size_t budget = std::max<std::size_t>(sample_budget, 1);
    while (true) {
        if (!job_) {
            std::optional<std::uint32_t> next;
            for (std::uint32_t r = 0; r < rivers.reach_count() && !next; ++r) {
                if (stale_[r] || target(r) != applied_[r]) {
                    next = r;
                }
            }
            if (!next) {
                break;
            }
            // No flood: a reach at or below its base level, or one without a floodplain (nothing to spread
            // over).
            if (target(*next) == 0.0F || rivers.reaches()[*next].floodplain.empty()) {
                clear_reach(*next, target(*next), finished);
                continue;
            }
            Job job;
            job.reach = *next;
            job.offset = target(*next);
            job_ = std::move(job);
        }
        if (!run(*job_, rivers, budget)) {
            break; // out of budget; carry on next call
        }
        commit(*job_);
        finished.push_back(Finished{job_->reach, done_[job_->reach].area_m2});
        job_.reset();
        if (budget == 0) {
            break;
        }
    }
    dirty_ = job_.has_value() || std::any_of(stale_.begin(), stale_.end(), [](bool s) { return s; });
    if (!dirty_) {
        for (std::uint32_t r = 0; r < rivers.reach_count(); ++r) {
            dirty_ = dirty_ || target(r) != applied_[r];
        }
    }
    return finished;
}

} // namespace citysim::world
