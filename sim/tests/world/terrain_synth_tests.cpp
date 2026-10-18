#include "citysim/core/job_system.hpp"
#include "citysim/core/state_hasher.hpp"
#include "citysim/world/terrain_synth.hpp"
#include "citysim/world/world_terrain.hpp"
#include "support/synthetic_map.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace citysim;
using namespace citysim::world;

namespace {

// The full-size synthetic map: 96 km, detail tiles (16..30, 18..42), the coast at about 82-88% of the way
// north.
std::shared_ptr<const LoadedMap> full_map() {
    return LoadedMap::open(citysim::test::synthetic_map_dir());
}

std::shared_ptr<const CityTile> make(const LoadedMap& map, TileCoord tile, std::uint64_t seed = 1) {
    return TerrainSynth::make_tile(map, tile, seed);
}

std::uint64_t hash_of(const CityTile& tile) {
    core::StateHasher hasher;
    hasher.add(std::as_bytes(tile.heights()));
    return hasher.digest();
}

// True if a river channel (or its bank) could reach this point: terrain there is carved, not just upsampled.
bool near_river(const LoadedMap& map, WorldPos pos) {
    const double radius = map.rivers().max_half_width_m() + 64.0;
    return map.rivers().any_near_box(pos.x, pos.z, pos.x, pos.z, radius);
}

} // namespace

TEST_CASE("the generator version is 2", "[world][terrain][synth]") {
    CHECK(k_terrain_generator_version == 2);
}

// The golden values: the heights of the synthetic package (seed 1, 96 km) for a fixed map seed. Changing
// the generator changes these; do that only with a bump of k_terrain_generator_version and a note in the PR.
TEST_CASE("synthesized heights match the recorded golden hashes", "[world][terrain][synth][synthetic_map]") {
    const auto map = full_map();
    // Tile (3, 5) lies in the desert, outside the detail regions: region elevation and region fertility feed
    // it.
    CHECK(hash_of(*make(*map, TileCoord{3, 5})) == 0x447676E8BFFE1E39ULL);
    // Tile (23, 30) lies in the valley, with detail data.
    CHECK(hash_of(*make(*map, TileCoord{23, 30})) == 0x8A223FA5611342B4ULL);
}

TEST_CASE("adjacent tiles agree on their shared edge, bit for bit",
          "[world][terrain][synth][synthetic_map]") {
    const auto map = full_map();
    // Detail tiles, the desert, the coast, the sea, and the border between detail and no detail.
    for (const TileCoord a : {TileCoord{23, 30}, TileCoord{23, 38}, TileCoord{15, 17}, TileCoord{3, 5},
                              TileCoord{23, 43}, TileCoord{16, 18}}) {
        const auto tile = make(*map, a);
        const auto east = make(*map, TileCoord{a.tx + 1, a.ty});
        const auto north = make(*map, TileCoord{a.tx, a.ty + 1});
        for (std::uint32_t i = 0; i < k_city_tile_samples; ++i) {
            REQUIRE(tile->sample(k_city_tile_samples - 1, i) == east->sample(0, i));
            REQUIRE(tile->sample(i, k_city_tile_samples - 1) == north->sample(i, 0));
        }
    }
}

TEST_CASE("the surface has no seam: slopes across a border are as smooth as inside a tile",
          "[world][terrain][synth][synthetic_map]") {
    const auto map = full_map();
    const TileCoord a{23, 30};
    const auto west = make(*map, a);
    const auto east = make(*map, TileCoord{a.tx + 1, a.ty});
    const auto south = west;
    const auto north = make(*map, TileCoord{a.tx, a.ty + 1});
    constexpr std::uint32_t last = k_city_tile_samples - 1;

    // The change of the first difference (the second difference) at every interior sample of the west tile
    // and at the border samples: the border may not be rougher than the interior.
    float interior = 0.0F;
    for (std::uint32_t row = 0; row < k_city_tile_samples; ++row) {
        for (std::uint32_t col = 1; col < last; ++col) {
            interior =
                std::max(interior, std::abs(west->sample(col + 1, row) - 2.0F * west->sample(col, row) +
                                            west->sample(col - 1, row)));
            interior =
                std::max(interior, std::abs(west->sample(row, col + 1) - 2.0F * west->sample(row, col) +
                                            west->sample(row, col - 1)));
        }
    }
    REQUIRE(interior > 0.0F);
    float border = 0.0F;
    for (std::uint32_t i = 0; i < k_city_tile_samples; ++i) {
        // across the east border (columns last - 1, last == east 0, east 1) and the north border
        border = std::max(
            border, std::abs(east->sample(1, i) - 2.0F * west->sample(last, i) + west->sample(last - 1, i)));
        border = std::max(border, std::abs(north->sample(i, 1) - 2.0F * south->sample(i, last) +
                                           south->sample(i, last - 1)));
    }
    CHECK(border <= interior * 1.05F);
}

TEST_CASE("the bicubic surface passes through the 32 m nodes within the detail amplitude",
          "[world][terrain][synth][synthetic_map]") {
    const auto map = full_map();
    // Tile (23, 44) is open sea: no detail at all, so the nodes come back exactly.
    {
        const TileCoord sea{23, 44};
        const auto tile = make(*map, sea);
        const WorldPos origin = tile_origin(sea);
        for (std::uint32_t sy = 0; sy < k_city_tile_samples; sy += 8) {
            for (std::uint32_t sx = 0; sx < k_city_tile_samples; sx += 8) {
                const WorldPos pos{origin.x + sx * 4.0, origin.z + sy * 4.0};
                REQUIRE(tile->sample(sx, sy) <= 0.0F);
                REQUIRE(tile->sample(sx, sy) == map->dem_height(pos));
            }
        }
    }
    // On land the detail is at most 3 m, and the samples at node positions stay within that of the node.
    for (const TileCoord coord : {TileCoord{23, 30}, TileCoord{18, 25}, TileCoord{3, 5}}) {
        const auto tile = make(*map, coord);
        const WorldPos origin = tile_origin(coord);
        float worst = 0.0F;
        for (std::uint32_t sy = 0; sy < k_city_tile_samples; sy += 8) {
            for (std::uint32_t sx = 0; sx < k_city_tile_samples; sx += 8) {
                const WorldPos pos{origin.x + sx * 4.0, origin.z + sy * 4.0};
                if (near_river(*map, pos)) {
                    continue; // carved
                }
                worst = std::max(worst, std::abs(tile->sample(sx, sy) - map->dem_height(pos)));
            }
        }
        CHECK(worst <= 3.0F);
    }
}

TEST_CASE("micro-detail is gentle on fertile flat ground and rougher on barren ground",
          "[world][terrain][synth][synthetic_map]") {
    const auto map = full_map();
    const Raster<float>* fertility = map->region_layer("fertility");
    REQUIRE(fertility != nullptr);
    // Over the delta and valley tiles, look at nodes on fertile ground that the 32 m DEM says is flat, and at
    // nodes on barren desert.
    float flat_worst = 0.0F;
    float desert_worst = 0.0F;
    int flat_nodes = 0;
    int desert_nodes = 0;
    for (std::int32_t ty = 24; ty <= 32; ty += 4) {
        for (std::int32_t tx = 20; tx <= 28; tx += 2) {
            const TileCoord coord{tx, ty};
            const auto tile = make(*map, coord);
            const WorldPos origin = tile_origin(coord);
            for (std::uint32_t sy = 8; sy + 8 < k_city_tile_samples; sy += 8) {
                for (std::uint32_t sx = 8; sx + 8 < k_city_tile_samples; sx += 8) {
                    const WorldPos pos{origin.x + sx * 4.0, origin.z + sy * 4.0};
                    if (near_river(*map, pos)) {
                        continue; // carved
                    }
                    const float node = map->dem_height(pos);
                    const float deviation = std::abs(tile->sample(sx, sy) - node);
                    // flat: the 32 m neighbors in four directions are within 0.05 m of this node
                    const float slope = std::max({std::abs(map->dem_height({pos.x + 32.0, pos.z}) - node),
                                                  std::abs(map->dem_height({pos.x - 32.0, pos.z}) - node),
                                                  std::abs(map->dem_height({pos.x, pos.z + 32.0}) - node),
                                                  std::abs(map->dem_height({pos.x, pos.z - 32.0}) - node)});
                    const float fertile = map->region_value(*fertility, pos);
                    if (fertile > 0.95F && slope < 0.05F && node > 1.5F) {
                        flat_worst = std::max(flat_worst, deviation);
                        ++flat_nodes;
                    } else if (fertile < 0.15F && node > 5.0F) {
                        desert_worst = std::max(desert_worst, deviation);
                        ++desert_nodes;
                    }
                }
            }
        }
    }
    REQUIRE(flat_nodes > 50);
    REQUIRE(desert_nodes > 50);
    CHECK(flat_worst <= 0.3F);
    CHECK(desert_worst <= 3.0F);
    CHECK(desert_worst > 1.0F); // and the desert really is rougher
}

TEST_CASE("the map seed decides the micro-detail and the same seed gives the same tile",
          "[world][terrain][synth][synthetic_map]") {
    const auto map = full_map();
    const TileCoord coord{18, 25};
    const auto a = make(*map, coord, 1);
    const auto again = make(*map, coord, 1);
    const auto other = make(*map, coord, 2);
    CHECK(hash_of(*a) == hash_of(*again));
    CHECK(hash_of(*a) != hash_of(*other));
    // Another seed moves heights by at most the detail amplitude on land: 2 x 3 m.
    float worst = 0.0F;
    for (std::size_t i = 0; i < a->heights().size(); ++i) {
        worst = std::max(worst, std::abs(a->heights()[i] - other->heights()[i]));
    }
    CHECK(worst <= 6.0F);
    CHECK(worst > 0.0F);
}

TEST_CASE("tiles are the same for any number of workers", "[world][terrain][synth][synthetic_map]") {
    const auto map = full_map();
    const CityArea city{TileCoord{20, 28}};
    const std::vector<TileCoord> tiles = city.tiles(1);
    std::vector<std::uint64_t> reference;
    for (const std::size_t workers : {1U, 2U, 7U}) {
        WorldTerrain terrain(map, 1);
        core::JobSystem jobs(workers);
        terrain.activate_tiles(jobs, tiles);
        std::vector<std::uint64_t> hashes;
        for (const TileCoord coord : tiles) {
            hashes.push_back(hash_of(*terrain.tile(coord)));
        }
        if (reference.empty()) {
            reference = hashes;
        }
        CHECK(hashes == reference);
    }
}

// Hidden by default: run with `citysim_tests "[.bench]"`. A tile must take under 30 ms in a Release build.
TEST_CASE("synthesizing one tile is fast", "[.bench][synthetic_map]") {
    const auto map = full_map();
    const TileCoord coord{23, 30};
    make(*map, coord); // warm the detail-tile cache
    constexpr int runs = 20;
    double best_ms = 1e9;
    double total_ms = 0.0;
    for (int i = 0; i < runs; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const auto tile = make(*map, coord);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        best_ms = std::min(best_ms, ms);
        total_ms += ms;
        REQUIRE(tile->heights().size() == CityTile::k_sample_count);
    }
    std::printf("tile synthesis: best %.2f ms, mean %.2f ms over %d runs\n", best_ms, total_ms / runs, runs);
#ifdef NDEBUG
    CHECK(best_ms < 30.0);
#endif
}
