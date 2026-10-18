#pragma once

#include "citysim/debug/test_map.hpp"

#include <cstdint>
#include <vector>

namespace citysim::debug {

// Reusable working memory for find_path, so a search allocates nothing after the first. It is only a cache:
// the result of a search never depends on what it held before. Not thread-safe; use one per thread.
struct PathScratch {
    // Internal to find_path; public only so it can be a plain struct.
    struct Node {
        std::uint32_t f;
        std::uint32_t h;
        std::uint32_t cell;
    };
    std::vector<std::uint32_t> g;        // best known cost from the start
    std::vector<std::uint32_t> parent;   // cell we came from
    std::vector<std::uint32_t> open_gen; // == generation when g/parent are valid for this search
    std::vector<std::uint32_t> closed_gen;
    std::uint32_t generation = 0;
    std::vector<Node> open;
};

struct PathResult {
    bool found = false;
    bool budget_exceeded = false; // not found because the search gave up, not because there is no path
    std::vector<std::uint32_t>
        cells;                  // from the cell after `start` to `goal`, inclusive; empty if start == goal
    std::uint32_t expanded = 0; // cells taken off the open list
};

// A* on the grid: 8 neighbours, octile distance (orthogonal step 10, diagonal 14, integer costs so nothing
// depends on float rounding), no cutting corners (a diagonal step needs both orthogonal cells walkable).
// Ties are broken by lowest f, then lowest h, then lowest cell index, so the path is the same on every run
// and every machine. Gives up after `node_budget` expansions. Start and goal must be walkable cells of the
// map.
PathResult find_path(const TestMap& map, std::uint32_t start, std::uint32_t goal, PathScratch& scratch,
                     std::uint32_t node_budget);

} // namespace citysim::debug
