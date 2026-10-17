#include "citysim/debug/grid_pathfinder.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace citysim::debug {

namespace {

constexpr std::uint32_t k_orthogonal_cost = 10;
constexpr std::uint32_t k_diagonal_cost = 14;

struct Step {
    int dx;
    int dy;
};
// A fixed order, part of the tie-breaking.
constexpr std::array<Step, 8> k_steps{{{0, -1}, {-1, 0}, {1, 0}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}}};

// Min-heap order: lowest f, then lowest h, then lowest cell index.
bool later(const PathScratch::Node& a, const PathScratch::Node& b) {
    if (a.f != b.f) {
        return a.f > b.f;
    }
    if (a.h != b.h) {
        return a.h > b.h;
    }
    return a.cell > b.cell;
}

// Octile distance in the integer costs above.
std::uint32_t heuristic(const TestMap& map, std::uint32_t cell, std::uint32_t goal) {
    const auto dx = static_cast<std::uint32_t>(
        std::abs(static_cast<std::int64_t>(map.x_of(cell)) - static_cast<std::int64_t>(map.x_of(goal))));
    const auto dy = static_cast<std::uint32_t>(
        std::abs(static_cast<std::int64_t>(map.y_of(cell)) - static_cast<std::int64_t>(map.y_of(goal))));
    const std::uint32_t longer = std::max(dx, dy);
    const std::uint32_t shorter = std::min(dx, dy);
    return k_orthogonal_cost * longer + (k_diagonal_cost - k_orthogonal_cost) * shorter;
}

} // namespace

PathResult find_path(const TestMap& map, std::uint32_t start, std::uint32_t goal, PathScratch& scratch,
                     std::uint32_t node_budget) {
    PathResult result;
    if (start == goal) {
        result.found = true;
        return result;
    }

    const std::uint32_t cells = map.cell_count();
    if (scratch.g.size() != cells) {
        scratch.g.assign(cells, 0);
        scratch.parent.assign(cells, 0);
        scratch.open_gen.assign(cells, 0);
        scratch.closed_gen.assign(cells, 0);
        scratch.generation = 0;
    }
    if (++scratch.generation == 0) { // wrapped: old stamps could match again
        std::fill(scratch.open_gen.begin(), scratch.open_gen.end(), 0);
        std::fill(scratch.closed_gen.begin(), scratch.closed_gen.end(), 0);
        scratch.generation = 1;
    }
    const std::uint32_t generation = scratch.generation;
    scratch.open.clear();

    scratch.g[start] = 0;
    scratch.parent[start] = start;
    scratch.open_gen[start] = generation;
    const std::uint32_t start_h = heuristic(map, start, goal);
    scratch.open.push_back({start_h, start_h, start});

    while (!scratch.open.empty()) {
        std::pop_heap(scratch.open.begin(), scratch.open.end(), later);
        const PathScratch::Node node = scratch.open.back();
        scratch.open.pop_back();
        if (scratch.closed_gen[node.cell] == generation) {
            continue; // a stale entry: the cell was reached more cheaply and expanded already
        }
        scratch.closed_gen[node.cell] = generation;

        if (node.cell == goal) {
            for (std::uint32_t cell = goal; cell != start; cell = scratch.parent[cell]) {
                result.cells.push_back(cell);
            }
            std::reverse(result.cells.begin(), result.cells.end());
            result.found = true;
            return result;
        }
        if (++result.expanded > node_budget) {
            result.budget_exceeded = true;
            return result;
        }

        const auto x = static_cast<std::int64_t>(map.x_of(node.cell));
        const auto y = static_cast<std::int64_t>(map.y_of(node.cell));
        for (const Step step : k_steps) {
            const std::int64_t nx = x + step.dx;
            const std::int64_t ny = y + step.dy;
            if (!map.walkable(nx, ny)) {
                continue;
            }
            const bool diagonal = step.dx != 0 && step.dy != 0;
            if (diagonal && (!map.walkable(x + step.dx, y) || !map.walkable(x, y + step.dy))) {
                continue; // no cutting corners
            }
            const std::uint32_t next =
                map.index_of(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
            if (scratch.closed_gen[next] == generation) {
                continue;
            }
            const std::uint32_t cost =
                scratch.g[node.cell] + (diagonal ? k_diagonal_cost : k_orthogonal_cost);
            if (scratch.open_gen[next] == generation && cost >= scratch.g[next]) {
                continue;
            }
            scratch.g[next] = cost;
            scratch.parent[next] = node.cell;
            scratch.open_gen[next] = generation;
            const std::uint32_t h = heuristic(map, next, goal);
            scratch.open.push_back({cost + h, h, next});
            std::push_heap(scratch.open.begin(), scratch.open.end(), later);
        }
    }
    return result; // the open list ran dry: no path
}

} // namespace citysim::debug
