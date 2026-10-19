#include "citysim/world/terrain_mesh.hpp"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <vector>

using namespace citysim::world;
using Catch::Approx;

namespace {

// A tile whose sample (sx, sy) is f(world x, world z), so neighbors built from the same f agree at their
// edges.
std::shared_ptr<const CityTile> make_tile(TileCoord coord, const std::function<float(double, double)>& f,
                                          std::uint64_t version = 1) {
    std::vector<float> heights(CityTile::k_sample_count);
    const WorldPos origin = tile_origin(coord);
    for (std::uint32_t sy = 0; sy < k_city_tile_samples; ++sy) {
        for (std::uint32_t sx = 0; sx < k_city_tile_samples; ++sx) {
            heights[static_cast<std::size_t>(sy) * k_city_tile_samples + sx] =
                f(origin.x + sx * k_city_sample_spacing_m, origin.z + sy * k_city_sample_spacing_m);
        }
    }
    return std::make_shared<const CityTile>(coord, version, std::move(heights));
}

float bumpy(double x, double z) {
    return static_cast<float>(20.0 + 0.01 * x + 8.0 * std::sin(x / 37.0) * std::cos(z / 53.0));
}

TileHeights alone(const CityTile& tile) {
    std::array<const CityTile*, 9> block{};
    block[4] = &tile;
    return TileHeights(block, nullptr);
}

std::array<float, 3> cross(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

std::array<float, 3> vertex(const ChunkVertices& v, std::int32_t i) {
    const auto at = static_cast<std::size_t>(i) * 3;
    return {v.positions[at], v.positions[at + 1], v.positions[at + 2]};
}

} // namespace

TEST_CASE("chunk keys and indices map one to one", "[world][terrain_mesh]") {
    std::set<std::uint32_t> seen;
    std::uint32_t count = 0;
    for (std::uint32_t lod = 0; lod < k_chunk_lod_count; ++lod) {
        for (std::uint32_t cy = 0; cy < chunks_per_side(lod); ++cy) {
            for (std::uint32_t cx = 0; cx < chunks_per_side(lod); ++cx) {
                const ChunkKey key{static_cast<std::uint8_t>(lod), static_cast<std::uint8_t>(cx),
                                   static_cast<std::uint8_t>(cy)};
                const std::uint32_t index = chunk_index(key);
                CHECK(index < k_tile_chunk_count);
                CHECK(chunk_key(index) == key);
                seen.insert(index);
                ++count;
            }
        }
    }
    CHECK(count == k_tile_chunk_count);
    CHECK(seen.size() == k_tile_chunk_count);
    CHECK(chunk_index(ChunkKey{3, 0, 0}) == 0); // the root comes first
    CHECK(chunk_size_m(0) == 256.0);
    CHECK(chunk_size_m(3) == 2048.0);
}

TEST_CASE("the geometric error is zero on a plane and grows with the LOD on rough ground",
          "[world][terrain_mesh]") {
    const auto plane = make_tile(
        TileCoord{1, 1}, [](double x, double z) { return static_cast<float>(5.0 + 0.02 * x - 0.01 * z); });
    const TileLodInfo flat = analyze_tile(*plane);
    for (std::uint32_t i = 0; i < k_tile_chunk_count; ++i) {
        CHECK(flat.error_m[i] == Approx(0.0F).margin(1e-3));
        CHECK(flat.min_height[i] <= flat.max_height[i]);
    }

    const auto rough = make_tile(TileCoord{1, 1}, bumpy);
    const TileLodInfo info = analyze_tile(*rough);
    CHECK(info.error_m[chunk_index(ChunkKey{0, 3, 3})] == 0.0F);
    const float e1 = info.error_m[chunk_index(ChunkKey{1, 1, 1})];
    const float e3 = info.error_m[chunk_index(ChunkKey{3, 0, 0})];
    CHECK(e1 > 0.0F);
    CHECK(e3 > e1);
    // The root's range is the tile's range.
    const auto [low, high] = std::minmax_element(rough->heights().begin(), rough->heights().end());
    CHECK(info.min_height[0] == *low);
    CHECK(info.max_height[0] == *high);
}

TEST_CASE("a chunk's grid is the tile's samples and its triangles face up", "[world][terrain_mesh]") {
    const auto tile = make_tile(TileCoord{0, 0}, bumpy);
    const TileHeights heights = alone(*tile);
    for (const ChunkKey key : {ChunkKey{0, 2, 5}, ChunkKey{2, 1, 0}, ChunkKey{3, 0, 0}}) {
        const ChunkVertices v = build_chunk_vertices(heights, key, 7.0F);
        REQUIRE(v.positions.size() == static_cast<std::size_t>(k_chunk_vertex_count) * 3);
        const std::uint32_t step = chunk_step(key.lod);
        for (std::uint32_t j = 0; j < k_chunk_side_vertices; j += 16) {
            for (std::uint32_t i = 0; i < k_chunk_side_vertices; i += 16) {
                const std::uint32_t sx = key.cx * k_chunk_quads * step + i * step;
                const std::uint32_t sy = key.cy * k_chunk_quads * step + j * step;
                const std::array<float, 3> p =
                    vertex(v, static_cast<std::int32_t>(j * k_chunk_side_vertices + i));
                CHECK(p[0] == static_cast<float>(sx * 4));
                CHECK(p[2] == static_cast<float>(sy * 4));
                CHECK(p[1] == tile->sample(sx, sy));
            }
        }
        // Grid triangles: clockwise seen from above, so the right-handed cross product points down. Skirt
        // triangles stand upright and face outwards, so it points into the chunk.
        const std::vector<std::int32_t>& indices = chunk_indices();
        REQUIRE(indices.size() == k_chunk_index_count);
        const float half = static_cast<float>(chunk_size_m(key.lod)) * 0.5F;
        const float center_x = static_cast<float>(key.cx * chunk_size_m(key.lod)) + half;
        const float center_z = static_cast<float>(key.cy * chunk_size_m(key.lod)) + half;
        const std::size_t grid_indices = static_cast<std::size_t>(k_chunk_quads) * k_chunk_quads * 6;
        for (std::size_t t = 0; t < indices.size(); t += 3) {
            const auto a = vertex(v, indices[t]);
            const auto b = vertex(v, indices[t + 1]);
            const auto c = vertex(v, indices[t + 2]);
            const auto n =
                cross({b[0] - a[0], b[1] - a[1], b[2] - a[2]}, {c[0] - a[0], c[1] - a[1], c[2] - a[2]});
            if (t < grid_indices) {
                REQUIRE(n[1] < 0.0F);
            } else {
                const float mid_x = (a[0] + b[0] + c[0]) / 3.0F - center_x;
                const float mid_z = (a[2] + b[2] + c[2]) / 3.0F - center_z;
                REQUIRE(n[0] * mid_x + n[2] * mid_z < 0.0F);
                REQUIRE(std::abs(n[1]) < 1e-3F * (std::abs(n[0]) + std::abs(n[2])));
            }
        }
        // The skirt hangs 7 m below the border.
        CHECK(v.positions[static_cast<std::size_t>(k_chunk_grid_vertices) * 3 + 1] ==
              Approx(v.positions[1] - 7.0F));
    }
}

TEST_CASE("chunks of neighboring tiles meet exactly and are lit alike at the seam", "[world][terrain_mesh]") {
    std::array<std::shared_ptr<const CityTile>, 9> owned;
    std::array<const CityTile*, 9> west_block{};
    std::array<const CityTile*, 9> east_block{};
    // Tiles (0..2, 0..2); the west tile is (1, 1), the east tile (2, 1) with no neighbors to its east.
    for (std::int32_t ty = 0; ty < 3; ++ty) {
        for (std::int32_t tx = 0; tx < 3; ++tx) {
            owned[static_cast<std::size_t>(ty * 3 + tx)] = make_tile(TileCoord{tx, ty}, bumpy);
        }
    }
    for (std::int32_t dy = -1; dy <= 1; ++dy) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            const auto at = static_cast<std::size_t>((dy + 1) * 3 + (dx + 1));
            west_block[at] = owned[static_cast<std::size_t>((1 + dy) * 3 + (1 + dx))].get();
            east_block[at] =
                2 + dx <= 2 ? owned[static_cast<std::size_t>((1 + dy) * 3 + (2 + dx))].get() : nullptr;
        }
    }
    const TileHeights west(west_block, nullptr);
    const TileHeights east(east_block, nullptr);
    const ChunkVertices a =
        build_chunk_vertices(west, ChunkKey{0, 7, 3}, 5.0F); // the west tile's east column
    const ChunkVertices b =
        build_chunk_vertices(east, ChunkKey{0, 0, 3}, 5.0F); // the east tile's west column
    for (std::uint32_t j = 0; j < k_chunk_side_vertices; ++j) {
        const std::size_t ia = (static_cast<std::size_t>(j) * k_chunk_side_vertices + k_chunk_quads) * 3;
        const std::size_t ib = (static_cast<std::size_t>(j) * k_chunk_side_vertices) * 3;
        CHECK(a.positions[ia] - 2048.0F == b.positions[ib]); // the same world point...
        CHECK(a.positions[ia + 1] == b.positions[ib + 1]);   // ...at the same height...
        CHECK(a.positions[ia + 2] == b.positions[ib + 2]);
        for (std::size_t c = 0; c < 3; ++c) {
            CHECK(a.normals[ia + c] == Approx(b.normals[ib + c]).margin(1e-6)); // ...with the same normal
        }
    }
}

TEST_CASE("normals follow the slope", "[world][terrain_mesh]") {
    // Rises 1 m per 10 m east: the normal leans west.
    const auto tile =
        make_tile(TileCoord{0, 0}, [](double x, double) { return static_cast<float>(0.1 * x); });
    const ChunkVertices v = build_chunk_vertices(alone(*tile), ChunkKey{1, 1, 1}, 1.0F);
    const float expected_x = static_cast<float>(-0.1 / std::sqrt(1.01));
    const std::size_t middle = (32 * k_chunk_side_vertices + 32) * 3;
    CHECK(v.normals[middle] == Approx(expected_x).margin(1e-5));
    CHECK(v.normals[middle + 1] == Approx(1.0 / std::sqrt(1.01)).margin(1e-5));
    CHECK(v.normals[middle + 2] == Approx(0.0).margin(1e-6));
}

TEST_CASE("a changed sample marks its chunk and the chunks it borders", "[world][terrain_mesh]") {
    const auto before = make_tile(TileCoord{0, 0}, bumpy);
    std::vector<float> heights(before->heights().begin(), before->heights().end());
    CHECK_FALSE(compare_tiles(*before, *before).any());

    // Inside chunk (2, 1): sample (150, 100).
    heights[100 * k_city_tile_samples + 150] += 1.0F;
    const auto inside = std::make_shared<const CityTile>(TileCoord{0, 0}, 2, heights);
    TileChanges changes = compare_tiles(*before, *inside);
    CHECK(changes.lod0[1 * 8 + 2]);
    CHECK(std::count(changes.lod0.begin(), changes.lod0.end(), true) == 1);
    CHECK(changes.chunk_changed(ChunkKey{1, 1, 0}));
    CHECK(changes.chunk_changed(ChunkKey{3, 0, 0}));
    CHECK_FALSE(changes.chunk_changed(ChunkKey{1, 0, 1}));

    // On the border between chunks (0, 0) and (1, 0): sample (64, 10) is shared; (65, 10) is next to it.
    heights = std::vector<float>(before->heights().begin(), before->heights().end());
    heights[10 * k_city_tile_samples + 65] += 1.0F;
    changes = compare_tiles(*before, CityTile(TileCoord{0, 0}, 2, heights));
    CHECK(changes.lod0[0]);
    CHECK(changes.lod0[1]);
    CHECK(std::count(changes.lod0.begin(), changes.lod0.end(), true) == 2);
}

TEST_CASE("the selection covers the tile once, finer near the camera", "[world][terrain_mesh]") {
    const auto tile = make_tile(TileCoord{0, 0}, bumpy);
    const TileLodInfo info = analyze_tile(*tile);
    const auto all_ready = [](std::uint32_t) { return true; };
    LodSelection selection;

    // Covered exactly once: the drawn chunks' areas add up to the tile and none contains another.
    const auto check_cover = [&](const LodSelection& s) {
        double area = 0.0;
        for (const std::uint32_t index : s.draw) {
            area += chunk_size_m(chunk_key(index).lod) * chunk_size_m(chunk_key(index).lod);
        }
        CHECK(area == Approx(2048.0 * 2048.0));
        CHECK(std::set<std::uint32_t>(s.draw.begin(), s.draw.end()).size() == s.draw.size());
    };

    LodCamera close;
    close.x = 100.0;
    close.y = 60.0;
    close.z = 100.0;
    select_chunks(info, close, all_ready, selection);
    check_cover(selection);
    CHECK(selection.wanted.empty());
    CHECK(std::count(selection.draw.begin(), selection.draw.end(), chunk_index(ChunkKey{0, 0, 0})) == 1);
    // Far corner coarser than the near one.
    bool far_is_coarse = false;
    for (const std::uint32_t index : selection.draw) {
        const ChunkKey key = chunk_key(index);
        if (key.lod > 0 && (key.cx + 1) * chunk_size_m(key.lod) == 2048.0 &&
            (key.cy + 1) * chunk_size_m(key.lod) == 2048.0) {
            far_is_coarse = true;
        }
    }
    CHECK(far_is_coarse);

    // Far above and away: the whole tile in one chunk. min_lod stops splitting.
    LodCamera far;
    far.x = 1024.0;
    far.y = 50000.0;
    far.z = 1024.0;
    select_chunks(info, far, all_ready, selection);
    CHECK(selection.draw == std::vector<std::uint32_t>{0});
    LodCamera ring = close;
    ring.min_lod = 2;
    select_chunks(info, ring, all_ready, selection);
    check_cover(selection);
    for (const std::uint32_t index : selection.draw) {
        CHECK(chunk_key(index).lod >= 2);
    }

    // Nothing built yet: nothing drawn, the root wanted.
    select_chunks(info, close, [](std::uint32_t) { return false; }, selection);
    CHECK(selection.draw.empty());
    CHECK(std::count(selection.wanted.begin(), selection.wanted.end(), 0U) == 1);
    // Only the root built: it stands in, and the finer chunks are wanted.
    select_chunks(info, close, [](std::uint32_t index) { return index == 0; }, selection);
    CHECK(selection.draw == std::vector<std::uint32_t>{0});
    CHECK(std::count(selection.wanted.begin(), selection.wanted.end(), chunk_index(ChunkKey{0, 0, 0})) == 1);
}

TEST_CASE("a ray finds the ground", "[world][terrain_mesh]") {
    // Flat ground at 10 m over x, z in [0, 1000]; nothing elsewhere.
    const auto ground = [](double x, double z) -> std::optional<float> {
        if (x < 0.0 || z < 0.0 || x > 1000.0 || z > 1000.0) {
            return std::nullopt;
        }
        return 10.0F;
    };
    const auto down = raycast_heightfield({100.0, 100.0, 200.0}, {0.0, -1.0, 0.0}, 1000.0, 2.0, ground);
    REQUIRE(down.has_value());
    CHECK((*down)[0] == Approx(100.0));
    CHECK((*down)[1] == Approx(10.0));
    CHECK((*down)[2] == Approx(200.0));

    // Slanted: from (0, 110, 500) towards +x and down at 45 degrees, it lands at x = 100.
    const auto slanted = raycast_heightfield({0.0, 110.0, 500.0}, {1.0, -1.0, 0.0}, 1000.0, 2.0, ground);
    REQUIRE(slanted.has_value());
    CHECK((*slanted)[0] == Approx(100.0).margin(0.01));

    // Up, or off the ground: no hit.
    CHECK_FALSE(raycast_heightfield({100.0, 100.0, 200.0}, {0.0, 1.0, 0.0}, 1000.0, 2.0, ground).has_value());
    CHECK_FALSE(
        raycast_heightfield({-500.0, 100.0, -500.0}, {-1.0, -0.1, 0.0}, 1000.0, 2.0, ground).has_value());
}

TEST_CASE("terrain colors stay in range and turn to rock on steep slopes", "[world][terrain_mesh]") {
    for (const float h : {-20.0F, 0.0F, 40.0F, 200.0F}) {
        for (const float s : {0.0F, 0.3F, 2.0F}) {
            for (const float c : terrain_color(h, s)) {
                CHECK(c >= 0.0F);
                CHECK(c <= 1.0F);
            }
        }
    }
    CHECK(terrain_color(10.0F, 2.0F) != terrain_color(10.0F, 0.0F));
}
