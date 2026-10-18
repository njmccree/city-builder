#pragma once

#include "citysim/world/city_tile.hpp"
#include "citysim/world/frames.hpp"
#include "citysim/world/tile_coord.hpp"
#include "citysim/world/water_state.hpp"
#include "citysim/world/world_terrain.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace citysim::world {

// Which land a rising river covers (architecture §15.6): one byte per 4 m terrain sample of every active
// tile, 1 where the sample is flooded land and 0 elsewhere (the channel itself is water already and is not
// marked). Derived state: it follows from the terrain, the river data and WaterState, is never saved and is
// not part of the state hash. After a load it is simply recomputed from the loaded WaterState.
//
// A reach floods when its level offset is above 0. The fill starts from the reach's channel and spreads over
// the 4-connected samples that lie inside the reach's floodplain polygon and below the water level it carries
// from where it started (the reach's level at that point of the channel plus the offset). It is time-sliced:
// advance() looks at no more than the sample budget per call (plus the rest of one row of a scan) and carries
// on from where it stopped, so the budget changes when a flood appears and never where it ends up. Reaches
// are worked one at a time in reach order; a change of offset while a reach is being filled starts it again.
// The old extent of a reach stays visible until the new one is complete, so a falling river does not flicker.
// When the active tiles change (a city is activated, the terrain is edited) every reach is filled again.
//
// Only the 4 m samples of active tiles are examined: the flood ends where the active terrain does.
//
// Clock: tick (it is advanced once per tick by the flood system, PostUpdate). Single-threaded and
// deterministic: the result depends only on its inputs, never on the budget or on worker counts.
class FloodMap {
  public:
    // The marks of one tile: k_city_tile_samples * k_city_tile_samples bytes, row-major like CityTile.
    using Grid = std::vector<std::uint8_t>;

    // A reach whose fill has finished during an advance() call.
    struct Finished {
        std::uint32_t reach = 0;
        double flooded_area_m2 = 0.0; // 0 when the water went back into its channel
    };

    FloodMap() = default;

    // Works towards the extents that `water` and `terrain` ask for, spending about `sample_budget` samples
    // (at least 1 unit of work is always done). Returns the reaches that reached their final extent in this
    // call, in the order they finished.
    std::vector<Finished> advance(const WorldTerrain& terrain, const WaterState& water,
                                  std::size_t sample_budget);

    // True when every reach has the extent its level asks for (as of the last advance()).
    bool settled() const { return !job_ && !dirty_; }

    // The marks of a tile that has any flooded sample, or null. The pointer changes only when the marks do,
    // so a renderer can compare pointers (the SnapshotGrid pattern, architecture §7.3).
    std::shared_ptr<const Grid> grid(TileCoord tile) const;
    const std::map<TileCoord, std::shared_ptr<const Grid>>& grids() const { return grids_; }

    // The flooded land of one reach, in square meters (0 for a reach with no flood).
    double flooded_area_m2(std::uint32_t reach) const;

    // True if the sample of an active tile that contains `pos` (its south-west sample) is flooded.
    bool flooded_at(WorldPos pos) const;

  private:
    // The active tiles in a dense lookup: the bounding box of their coordinates, each cell holding the index
    // of the tile there or -1. Rebuilt whenever the active tiles change.
    struct Layout {
        std::int32_t tx0 = 0;
        std::int32_t ty0 = 0;
        std::int32_t columns = 0;
        std::int32_t rows = 0;
        std::vector<std::int32_t> index;
        std::vector<TileCoord> coords;                      // by tile index
        std::vector<std::shared_ptr<const CityTile>> tiles; // by tile index
    };
    struct Cell {
        std::int64_t gx = 0;
        std::int64_t gz = 0;
        float level = 0.0F;
    };
    // The fill of one reach in progress. `flags` has bits per sample: flooded, visited, channel.
    struct Job {
        std::uint32_t reach = 0;
        float offset = 0.0F;
        std::vector<Grid> flags; // by tile index; empty until the tile is touched
        // The x where each row of samples crosses the floodplain's edges, sorted, made when first needed.
        std::unordered_map<std::int64_t, std::vector<double>> crossings;
        std::uint32_t segment = 0; // the segment being scanned for channel samples
        bool segment_open = false;
        std::int64_t scan_gx0 = 0, scan_gx1 = 0, scan_gz = 0, scan_gz1 = 0;
        bool scanned = false;
        std::deque<Cell> queue;
        std::uint64_t cells = 0; // flooded land samples found so far
    };
    struct ReachFlood {
        double area_m2 = 0.0;
        std::map<TileCoord, Grid> layers; // only tiles with a flooded sample, marks 0/1
    };

    void sync(const WorldTerrain& terrain, std::size_t reach_count);
    bool run(Job& job, const RiverNetwork& rivers, std::size_t& budget);
    void commit(Job& job);
    void clear_reach(std::uint32_t reach, float applied, std::vector<Finished>& finished);
    void rebuild_grid(TileCoord tile);

    std::vector<std::pair<TileCoord, std::uint64_t>> signature_;
    Layout layout_;
    std::vector<ReachFlood> done_;
    std::vector<float> applied_; // the offset each reach's done_ extent was made for (0 = none)
    std::vector<bool> stale_;    // the extent must be made again (the active tiles changed)
    bool dirty_ = false;
    std::optional<Job> job_;
    std::map<TileCoord, std::shared_ptr<const Grid>> grids_;
};

} // namespace citysim::world
