#pragma once

#include "citysim/debug/walk_grid.hpp"

#include <cstdint>

namespace citysim::debug {

// The flat test map of the walking skeleton (architecture §2 "Walking Skeleton"): a grid of square cells,
// each walkable or blocked. Singleton state in the World context, registered so it is hashed and saved.
// Throwaway gameplay, production engineering: it is what proves pathfinding, jobs, snapshots and saves work
// together.
//
// Cell (x, y) has index y * width + x. Ground coordinates: the cell's centre is at
// ((x + 0.5) * cell_size, (y + 0.5) * cell_size) in the simulation's (x, z) plane, in meters, with the world
// origin at the south-west corner (architecture §11).
class TestMap : public WalkGrid {
  public:
    TestMap() = default;
    // All cells walkable. `width` and `height` of 0 give an empty map.
    TestMap(std::uint32_t width, std::uint32_t height, float cell_size)
        : WalkGrid(width, height, cell_size, true) {}

    // Used when loading: sets the size and resizes the cells, leaving them all blocked.
    void resize_for_load(std::uint32_t width, std::uint32_t height, float cell_size) {
        reset(width, height, cell_size, false);
    }
};

// A map with random rectangular obstacles. The same (seed, width, height) always gives the same map: it uses
// only citysim::rng. Obstacles are 2 to 10 cells on a side, about one per 600 cells (at least one).
TestMap make_test_map(std::uint64_t seed, std::uint32_t width, std::uint32_t height, float cell_size = 4.0F);

} // namespace citysim::debug
