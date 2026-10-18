#include "citysim/core/job_system.hpp"
#include "citysim/world/world_terrain.hpp"
#include "mapgen/synthetic_map.hpp"
#include "support/synthetic_map.hpp"
#include "support/temp_dir.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace citysim;
using namespace citysim::world;
using citysim::test::TempDir;

namespace {

// The full-size synthetic map has detail tiles (16..30, 18..42). This city sits in the south-west corner of
// them, so its ring reaches tiles without detail (the region raster answers there).
const CityArea k_city{TileCoord{16, 18}};

std::shared_ptr<const LoadedMap> full_map() {
    return LoadedMap::open(citysim::test::synthetic_map_dir());
}

bool same_heights(const CityTile& a, const CityTile& b) {
    return std::memcmp(a.heights().data(), b.heights().data(), a.heights().size_bytes()) == 0;
}

} // namespace

TEST_CASE("activating a city with its ring synthesizes 36 tiles", "[world][terrain][synthetic_map]") {
    WorldTerrain terrain(full_map(), 1);
    core::JobSystem jobs(2);
    const std::vector<TileCoord> tiles = k_city.tiles(1);
    terrain.activate_tiles(jobs, tiles);
    CHECK(terrain.active_tiles() == tiles);
    for (const TileCoord coord : tiles) {
        const auto tile = terrain.tile(coord);
        REQUIRE(tile != nullptr);
        CHECK(tile->coord() == coord);
        CHECK(tile->version() == 1);
        CHECK(tile->heights().size() == 513u * 513u);
        CHECK(std::none_of(tile->heights().begin(), tile->heights().end(),
                           [](float h) { return std::isnan(h); }));
    }
    // Activating again changes nothing: the same tiles, the same pointers.
    const auto before = terrain.tile(tiles[5]);
    terrain.activate_tiles(jobs, tiles);
    CHECK(terrain.tile(tiles[5]) == before);
}

TEST_CASE("neighboring city tiles share equal edge samples and heights", "[world][terrain][synthetic_map]") {
    WorldTerrain terrain(full_map(), 1);
    core::JobSystem jobs(4);
    terrain.activate_tiles(jobs, k_city.tiles(1));

    std::size_t edges = 0;
    for (const auto& [coord, tile] : terrain.tiles()) {
        if (const auto east = terrain.tile(TileCoord{coord.tx + 1, coord.ty})) {
            ++edges;
            for (std::uint32_t row = 0; row < k_city_tile_samples; ++row) {
                REQUIRE(tile->sample(512, row) == east->sample(0, row));
            }
            // Anywhere along the shared edge, both tiles (and the service) give the same height.
            for (double z = 0.0; z <= 2048.0; z += 12.75) {
                const float west_side = tile->height_at(2048.0, z);
                const float east_side = east->height_at(0.0, z);
                REQUIRE(west_side == east_side);
                const WorldPos edge{tile_origin(east->coord()).x, tile_origin(coord).z + z};
                REQUIRE(terrain.height_at(edge) == west_side);
            }
        }
        if (const auto north = terrain.tile(TileCoord{coord.tx, coord.ty + 1})) {
            ++edges;
            for (std::uint32_t col = 0; col < k_city_tile_samples; ++col) {
                REQUIRE(tile->sample(col, 512) == north->sample(col, 0));
            }
        }
    }
    CHECK(edges == 2 * 6 * 5); // a 6 x 6 block of tiles

    // Crossing an edge, the height moves continuously (no step between tiles).
    const WorldPos edge = tile_origin(TileCoord{18, 20});
    for (double z = 1.0; z < 2048.0; z += 97.0) {
        const float west = terrain.height_at(WorldPos{edge.x - 0.01, edge.z + z});
        const float east = terrain.height_at(WorldPos{edge.x + 0.01, edge.z + z});
        CHECK(std::abs(west - east) < 0.05F);
    }
}

TEST_CASE("parallel activation gives identical tiles with 1 and 8 workers",
          "[world][terrain][synthetic_map]") {
    WorldTerrain one(full_map(), 1);
    WorldTerrain eight(full_map(), 1);
    core::JobSystem serial(1);
    core::JobSystem parallel(8);
    const std::vector<TileCoord> tiles = k_city.tiles(1);
    one.activate_tiles(serial, tiles);
    eight.activate_tiles(parallel, tiles);
    REQUIRE(one.active_tiles() == eight.active_tiles());
    for (const TileCoord coord : tiles) {
        CHECK(same_heights(*one.tile(coord), *eight.tile(coord)));
    }
}

TEST_CASE("the placeholder synthesis samples the 32 m DEM at every 4 m point",
          "[world][terrain][synthetic_map]") {
    const auto map = full_map();
    WorldTerrain terrain(map, 1);
    core::JobSystem jobs(1);
    const TileCoord inside{17, 19};  // has detail
    const TileCoord outside{15, 17}; // ring corner without detail
    terrain.activate_tiles(jobs, std::vector<TileCoord>{inside, outside});
    for (const TileCoord coord : {inside, outside}) {
        const auto tile = terrain.tile(coord);
        const WorldPos origin = tile_origin(coord);
        for (std::uint32_t sy = 0; sy < k_city_tile_samples; sy += 37) {
            for (std::uint32_t sx = 0; sx < k_city_tile_samples; sx += 41) {
                const WorldPos pos{origin.x + sx * 4.0, origin.z + sy * 4.0};
                REQUIRE(tile->sample(sx, sy) == map->dem_height(pos));
            }
        }
    }
}

TEST_CASE("heights fall back to the DEM outside the active tiles", "[world][terrain][synthetic_map]") {
    const auto map = full_map();
    WorldTerrain terrain(map, 1);
    core::JobSystem jobs(1);
    const WorldPos inside_detail{17 * 2048.0 + 777.3, 19 * 2048.0 + 1500.9};
    const WorldPos desert{3000.0, 3000.0};
    CHECK(terrain.height_at(inside_detail) == map->dem_height(inside_detail));
    CHECK(terrain.height_at(desert) == map->region_height(desert));

    terrain.activate_tiles(jobs, std::vector<TileCoord>{TileCoord{17, 19}});
    const float synthesized = terrain.height_at(inside_detail);
    CHECK(synthesized == terrain.tile(TileCoord{17, 19})->height_at(777.3, 1500.9));
    // The placeholder is bilinear in 4 m samples of a bilinear 32 m surface, so they stay close.
    CHECK(std::abs(synthesized - map->dem_height(inside_detail)) < 0.5F);
}

TEST_CASE("height in a city's local frame matches the world frame", "[world][terrain][synthetic_map]") {
    WorldTerrain terrain(full_map(), 1);
    core::JobSystem jobs(2);
    terrain.activate_tiles(jobs, k_city.tiles());
    for (const LocalPos local : {LocalPos{0.0F, 0.0F}, LocalPos{100.25F, 7000.5F}, LocalPos{8192.0F, 8192.0F},
                                 LocalPos{4096.0F, 2048.0F}}) {
        CHECK(terrain.height_at_local(k_city, local) == terrain.height_at(to_world(k_city, local)));
    }
}

TEST_CASE("slope and normal come from the same height differences", "[world][terrain][synthetic_map]") {
    WorldTerrain terrain(full_map(), 1);
    core::JobSystem jobs(2);
    terrain.activate_tiles(jobs, k_city.tiles(1));
    float steepest = 0.0F;
    const WorldPos origin = k_city.origin();
    for (double x = 10.0; x < 8192.0; x += 211.0) {
        const WorldPos pos{origin.x + x, origin.z + 3000.0};
        const float slope = terrain.slope_at(pos);
        const TerrainNormal normal = terrain.normal_at(pos);
        const float dx = (terrain.height_at(WorldPos{pos.x + 4.0, pos.z}) -
                          terrain.height_at(WorldPos{pos.x - 4.0, pos.z})) /
                         8.0F;
        const float dz = (terrain.height_at(WorldPos{pos.x, pos.z + 4.0}) -
                          terrain.height_at(WorldPos{pos.x, pos.z - 4.0})) /
                         8.0F;
        CHECK(std::abs(slope - std::sqrt(dx * dx + dz * dz)) < 1e-5F);
        CHECK(std::abs(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z - 1.0F) < 1e-5F);
        CHECK(std::abs(normal.y - 1.0F / std::sqrt(1.0F + slope * slope)) < 1e-5F);
        CHECK(normal.y > 0.0F);
        // The normal leans downhill: against the gradient.
        CHECK(normal.x * dx <= 0.0F);
        CHECK(normal.z * dz <= 0.0F);
        steepest = std::max(steepest, slope);
    }
    CHECK(steepest > 0.0F);
}

TEST_CASE("tiles outside the map are refused and nothing changes", "[world][terrain][synthetic_map]") {
    WorldTerrain terrain(full_map(), 1);
    core::JobSystem jobs(1);
    CHECK_THROWS_AS(terrain.activate_tiles(jobs, std::vector<TileCoord>{TileCoord{20, 20}, TileCoord{-1, 3}}),
                    std::out_of_range);
    CHECK_THROWS_AS(terrain.activate_tiles(jobs, std::vector<TileCoord>{TileCoord{47, 0}}),
                    std::out_of_range);
    CHECK(terrain.active_tiles().empty());
    // The last, partial tile row and column are on the map.
    terrain.activate_tiles(jobs, std::vector<TileCoord>{TileCoord{46, 46}});
    CHECK(terrain.is_active(TileCoord{46, 46}));
}

TEST_CASE("deactivating drops tiles, but holders keep theirs", "[world][terrain][synthetic_map]") {
    WorldTerrain terrain(full_map(), 1);
    core::JobSystem jobs(2);
    terrain.activate_tiles(jobs, k_city.tiles());
    const auto held = terrain.tile(TileCoord{17, 19});
    terrain.deactivate_tiles(std::vector<TileCoord>{TileCoord{17, 19}, TileCoord{0, 0}});
    CHECK(!terrain.is_active(TileCoord{17, 19}));
    CHECK(terrain.active_tiles().size() == 15);
    CHECK(held->heights().size() == 513u * 513u); // still valid
    terrain.deactivate_all();
    CHECK(terrain.tiles().empty());
}

TEST_CASE("a terrain without a map refuses queries", "[world][terrain]") {
    WorldTerrain terrain;
    core::JobSystem jobs(1);
    CHECK(!terrain.has_map());
    CHECK(terrain.map_id().empty());
    CHECK(terrain.package_hash() == 0);
    CHECK_THROWS_AS(terrain.height_at(WorldPos{}), std::logic_error);
    CHECK_THROWS_AS(terrain.activate_tiles(jobs, std::vector<TileCoord>{TileCoord{}}), std::logic_error);
    CHECK_THROWS_AS(WorldTerrain(nullptr, 1), std::invalid_argument);
}
