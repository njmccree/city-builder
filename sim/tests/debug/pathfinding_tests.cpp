#include "citysim/debug/grid_pathfinder.hpp"
#include "citysim/debug/test_map.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <limits>
#include <queue>
#include <string>
#include <vector>

using namespace citysim::debug;

namespace {

// A map from rows of text: '.' walkable, '#' blocked.
TestMap map_from(const std::vector<std::string>& rows) {
    TestMap map(static_cast<std::uint32_t>(rows[0].size()), static_cast<std::uint32_t>(rows.size()), 4.0F);
    for (std::uint32_t y = 0; y < rows.size(); ++y) {
        for (std::uint32_t x = 0; x < rows[y].size(); ++x) {
            map.set_walkable(map.index_of(x, y), rows[y][x] == '.');
        }
    }
    return map;
}

std::uint32_t cell(const TestMap& map, std::uint32_t x, std::uint32_t y) {
    return map.index_of(x, y);
}

// The cost of a path in the pathfinder's integer costs, or nullopt if a step is not a legal move.
bool legal_cost(const TestMap& map, std::uint32_t start, const std::vector<std::uint32_t>& cells,
                std::uint32_t& cost) {
    cost = 0;
    std::uint32_t at = start;
    for (const std::uint32_t next : cells) {
        const int dx = static_cast<int>(map.x_of(next)) - static_cast<int>(map.x_of(at));
        const int dy = static_cast<int>(map.y_of(next)) - static_cast<int>(map.y_of(at));
        if (std::abs(dx) > 1 || std::abs(dy) > 1 || (dx == 0 && dy == 0) || !map.walkable(next)) {
            return false;
        }
        if (dx != 0 && dy != 0) {
            if (!map.walkable(static_cast<std::int64_t>(map.x_of(at)) + dx, map.y_of(at)) ||
                !map.walkable(map.x_of(at), static_cast<std::int64_t>(map.y_of(at)) + dy)) {
                return false; // cut a corner
            }
            cost += 14;
        } else {
            cost += 10;
        }
        at = next;
    }
    return true;
}

// Dijkstra with the same moves: the cheapest cost, or max() if unreachable. The reference for A*.
std::uint32_t cheapest(const TestMap& map, std::uint32_t start, std::uint32_t goal) {
    std::vector<std::uint32_t> best(map.cell_count(), std::numeric_limits<std::uint32_t>::max());
    using Entry = std::pair<std::uint32_t, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    best[start] = 0;
    queue.push({0, start});
    while (!queue.empty()) {
        const auto [cost, at] = queue.top();
        queue.pop();
        if (cost > best[at]) {
            continue;
        }
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const std::int64_t nx = static_cast<std::int64_t>(map.x_of(at)) + dx;
                const std::int64_t ny = static_cast<std::int64_t>(map.y_of(at)) + dy;
                if (!map.walkable(nx, ny)) {
                    continue;
                }
                if (dx != 0 && dy != 0 &&
                    (!map.walkable(static_cast<std::int64_t>(map.x_of(at)) + dx, map.y_of(at)) ||
                     !map.walkable(map.x_of(at), static_cast<std::int64_t>(map.y_of(at)) + dy))) {
                    continue;
                }
                const std::uint32_t next =
                    map.index_of(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
                const std::uint32_t next_cost = cost + (dx != 0 && dy != 0 ? 14U : 10U);
                if (next_cost < best[next]) {
                    best[next] = next_cost;
                    queue.push({next_cost, next});
                }
            }
        }
    }
    return best[goal];
}

} // namespace

TEST_CASE("a straight line is found, goal included and start excluded", "[debug][path]") {
    const TestMap map = map_from({".....", ".....", "....."});
    PathScratch scratch;
    const PathResult result = find_path(map, cell(map, 0, 1), cell(map, 4, 1), scratch, 1000);
    REQUIRE(result.found);
    CHECK(result.cells ==
          std::vector<std::uint32_t>{cell(map, 1, 1), cell(map, 2, 1), cell(map, 3, 1), cell(map, 4, 1)});
}

TEST_CASE("start equal to goal is an empty path", "[debug][path]") {
    const TestMap map = map_from({"..."});
    PathScratch scratch;
    const PathResult result = find_path(map, 1, 1, scratch, 10);
    CHECK(result.found);
    CHECK(result.cells.empty());
}

TEST_CASE("diagonals are used when they are cheaper", "[debug][path]") {
    const TestMap map = map_from({"....", "....", "....", "...."});
    PathScratch scratch;
    const PathResult result = find_path(map, cell(map, 0, 0), cell(map, 3, 3), scratch, 1000);
    REQUIRE(result.found);
    CHECK(result.cells.size() == 3);
}

TEST_CASE("paths go around walls", "[debug][path]") {
    const TestMap map = map_from({
        ".....",
        ".###.",
        ".#...",
        ".#.#.",
        "...#.",
    });
    PathScratch scratch;
    const PathResult result = find_path(map, cell(map, 2, 2), cell(map, 4, 0), scratch, 1000);
    REQUIRE(result.found);
    std::uint32_t cost = 0;
    REQUIRE(legal_cost(map, cell(map, 2, 2), result.cells, cost));
    CHECK(cost == cheapest(map, cell(map, 2, 2), cell(map, 4, 0)));
}

TEST_CASE("an unreachable goal is reported, not guessed", "[debug][path]") {
    const TestMap map = map_from({
        "..#..",
        "..#..",
        "..#..",
    });
    PathScratch scratch;
    const PathResult result = find_path(map, cell(map, 0, 0), cell(map, 4, 2), scratch, 100000);
    CHECK_FALSE(result.found);
    CHECK_FALSE(result.budget_exceeded); // there really is no path
    CHECK(result.cells.empty());
}

TEST_CASE("a diagonal never cuts a corner", "[debug][path]") {
    // The only straight diagonal from (0,0) to (1,1) squeezes between two blocked cells.
    const TestMap map = map_from({
        ".#",
        "#.",
    });
    PathScratch scratch;
    CHECK_FALSE(find_path(map, cell(map, 0, 0), cell(map, 1, 1), scratch, 100).found);

    // With one side open the path must take the orthogonal route.
    const TestMap open_side = map_from({
        "..",
        "#.",
    });
    const PathResult result =
        find_path(open_side, cell(open_side, 0, 0), cell(open_side, 1, 1), scratch, 100);
    REQUIRE(result.found);
    CHECK(result.cells == std::vector<std::uint32_t>{cell(open_side, 1, 0), cell(open_side, 1, 1)});
}

TEST_CASE("a search that runs out of budget says so", "[debug][path]") {
    const TestMap map = map_from(std::vector<std::string>(20, std::string(20, '.')));
    PathScratch scratch;
    const PathResult result = find_path(map, cell(map, 0, 0), cell(map, 19, 19), scratch, 3);
    CHECK_FALSE(result.found);
    CHECK(result.budget_exceeded);
}

TEST_CASE("A* finds the cheapest legal path on random maps", "[debug][path]") {
    PathScratch reused; // one scratch for every search below: leftovers must not matter
    int found_count = 0;
    int unreachable_count = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        TestMap map = make_test_map(seed, 28, 24, 4.0F);
        // A wall of scattered blocks, plus on every third map a full wall across the middle, so that detours,
        // dead ends and unreachable targets all happen.
        if (seed % 3 == 0) {
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                map.set_walkable(map.index_of(map.width() / 2, y), false);
            }
        }
        for (std::uint32_t i = 0; i < map.cell_count(); i += 9) {
            map.set_walkable((i * 7 + static_cast<std::uint32_t>(seed)) % map.cell_count(), false);
        }
        for (std::uint64_t pair = 0; pair < 6; ++pair) {
            const auto start = pick_walkable_cell(map, seed * 1000 + pair * 2);
            const auto goal = pick_walkable_cell(map, seed * 1000 + pair * 2 + 1);
            if (!start || !goal) {
                continue;
            }
            const PathResult result = find_path(map, *start, *goal, reused, 1'000'000);
            PathScratch fresh;
            const PathResult again = find_path(map, *start, *goal, fresh, 1'000'000);
            CHECK(result.cells == again.cells); // the scratch's history never changes the answer

            const std::uint32_t expected = cheapest(map, *start, *goal);
            if (expected == std::numeric_limits<std::uint32_t>::max()) {
                CHECK_FALSE(result.found);
                ++unreachable_count;
                continue;
            }
            REQUIRE(result.found);
            std::uint32_t cost = 0;
            REQUIRE(legal_cost(map, *start, result.cells, cost));
            CHECK(cost == expected);
            if (*start != *goal) {
                CHECK(result.cells.back() == *goal);
            }
            ++found_count;
        }
    }
    CHECK(found_count > 100);
    CHECK(unreachable_count > 0);
}

TEST_CASE("ties between equal paths are broken the same way every time", "[debug][path]") {
    // An open field has many equally cheap paths; the chosen one is fixed.
    const TestMap map = map_from(std::vector<std::string>(12, std::string(12, '.')));
    PathScratch scratch;
    const PathResult first = find_path(map, cell(map, 1, 1), cell(map, 10, 6), scratch, 10000);
    REQUIRE(first.found);
    for (int i = 0; i < 5; ++i) {
        CHECK(find_path(map, cell(map, 1, 1), cell(map, 10, 6), scratch, 10000).cells == first.cells);
    }
}

TEST_CASE("test maps are reproducible and have obstacles", "[debug][map]") {
    const TestMap a = make_test_map(7, 64, 48, 4.0F);
    const TestMap b = make_test_map(7, 64, 48, 4.0F);
    const TestMap c = make_test_map(8, 64, 48, 4.0F);
    CHECK(a.words() == b.words());
    CHECK(a.words() != c.words());
    CHECK(a.width() == 64);
    CHECK(a.height() == 48);

    std::uint32_t blocked = 0;
    for (std::uint32_t i = 0; i < a.cell_count(); ++i) {
        blocked += a.walkable(i) ? 0U : 1U;
    }
    CHECK(blocked > 0);
    CHECK(blocked < a.cell_count() / 2);
}

TEST_CASE("cells and ground positions convert both ways", "[debug][map]") {
    const TestMap map(10, 8, 4.0F);
    const std::uint32_t index = map.index_of(3, 5);
    CHECK(map.x_of(index) == 3);
    CHECK(map.y_of(index) == 5);
    CHECK(map.center_x(index) == 14.0F);
    CHECK(map.center_z(index) == 22.0F);
    CHECK(map.cell_at(14.0F, 22.0F) == index);
    CHECK(map.cell_at(12.0F, 20.0F) == index);              // the south-west corner belongs to the cell
    CHECK(map.cell_at(-5.0F, -5.0F) == map.index_of(0, 0)); // clamped
    CHECK(map.cell_at(1000.0F, 1000.0F) == map.index_of(9, 7));
    CHECK_FALSE(map.walkable(-1, 0)); // outside is blocked
    CHECK_FALSE(map.walkable(10, 0));
}

TEST_CASE("a random walkable cell is walkable and fixed by the hash", "[debug][map]") {
    TestMap map = make_test_map(3, 30, 30, 4.0F);
    for (std::uint64_t hash = 0; hash < 500; ++hash) {
        const auto cell = pick_walkable_cell(map, hash);
        REQUIRE(cell.has_value());
        CHECK(map.walkable(*cell));
        CHECK(pick_walkable_cell(map, hash) == cell);
    }
    // A nearly full map still finds its one free cell; an entirely blocked map finds none.
    TestMap tiny(8, 8, 4.0F);
    for (std::uint32_t i = 0; i < tiny.cell_count(); ++i) {
        tiny.set_walkable(i, i == 37);
    }
    CHECK(pick_walkable_cell(tiny, 12345) == 37U);
    tiny.set_walkable(37, false);
    CHECK_FALSE(pick_walkable_cell(tiny, 12345).has_value());
    CHECK_FALSE(pick_walkable_cell(TestMap(), 1).has_value());
}
