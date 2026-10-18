#pragma once

#include "citysim/core/job_system.hpp"
#include "citysim/core/world.hpp"
#include "citysim/debug/walk_grid.hpp"
#include "citysim/world/city_tile.hpp"
#include "citysim/world/flood_map.hpp"
#include "citysim/world/frames.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace citysim::debug {

// Where the debug agents may walk on the real terrain of the active city: 2,048 x 2,048 cells of 4 m over the
// city's 8 x 8 km area, in the city-local frame (architecture §15.3). Cell (x, y) is the square between the
// terrain samples (x, y) and (x + 1, y + 1) of its tile, so a cell is walkable when
//   - its middle is not water: the ground at the middle (the mean of the four corner samples) is above sea
//     level, the middle is not inside a river channel that holds water there (the reach's level plus its
//     WaterState offset above the ground), and none of its four corner samples is flooded land (FloodMap);
//     and
//   - its slope is gentler than TerrainDefines::walk_max_slope (rise over run of the bilinear surface at the
//     middle of the cell).
//
// Derived state, like the FloodMap: a World ctx() singleton that is deliberately not registered with the
// ComponentRegistry, so it is neither hashed nor saved and is rebuilt from the terrain after a load. It is
// kept right by the TerrainWalkSystem (PostUpdate, tick clock), which calls sync() every tick: a tile of the
// area is recomputed (a "dirty rectangle" of 512 x 512 cells) when its terrain tile was replaced (a terrain
// edit), when its flood marks changed (the effect of a WaterChanged event) or when a river level changed near
// it; a different active city rebuilds everything. Every change of a cell counts in version(). Single
// writer, no locks: it is read by the systems that run after it in a tick and by the publish hooks between
// ticks.
class TerrainWalkGrid : public WalkGrid {
  public:
    static constexpr std::uint32_t k_cells = 2048;
    static constexpr std::uint32_t k_tiles_per_side = 4;
    static constexpr std::uint32_t k_cells_per_tile = k_cells / k_tiles_per_side;

    TerrainWalkGrid() = default;

    // The city the grid is for, 0 while there is none (the grid is then empty, 0 x 0 cells).
    std::uint32_t city_id() const { return city_id_; }
    const world::CityArea& area() const { return area_; }

    struct SyncResult {
        bool city_changed = false;     // the grid was started again for another city (or for none)
        std::size_t tiles_rebuilt = 0; // dirty rectangles recomputed
        std::size_t cells_changed = 0; // cells whose walkability is different now
    };
    // Brings the grid to the state of the world for `city_id` / `area` (0 and any area for none). Reads the
    // WorldTerrain, WaterState and FloodMap of the world and recomputes the dirty tiles in
    // parallel, one chunk per tile (the tiles' bits live in different words, so chunks never share one).
    SyncResult sync(core::World& world, core::JobSystem& jobs, std::uint32_t city_id,
                    const world::CityArea& area);

    // Which stretch of walkable ground a cell belongs to: 0 for a blocked cell, otherwise the number of its
    // connected region (cells joined by steps between walkable neighbours, found in cell order, so the
    // numbers depend only on the cells). A river with no ford splits the area into regions, and an agent can
    // only walk to cells of its own: the planner picks destinations from it so no search is wasted on a goal
    // that cannot be reached. The labels are made by update_regions() and are stale until it is called again
    // after a change (version()); they are never saved.
    void update_regions();
    std::uint32_t region_of(std::uint32_t cell) const { return cell < regions_.size() ? regions_[cell] : 0; }
    bool regions_current() const { return regions_version_ == version(); }

  private:
    struct Stamp {
        std::shared_ptr<const world::CityTile> tile;
        std::shared_ptr<const world::FloodMap::Grid> flood;
        bool built = false;
    };

    std::uint32_t city_id_ = 0;
    world::CityArea area_;
    std::array<Stamp, k_tiles_per_side * k_tiles_per_side> stamps_;
    std::vector<float> offsets_; // WaterState as of the last sync
    std::vector<std::uint32_t> regions_;
    std::uint64_t regions_version_ = 0; // the version() the labels were made for
    std::vector<std::uint32_t> queue_;  // scratch of update_regions()
};

} // namespace citysim::debug
