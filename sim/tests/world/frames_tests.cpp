#include "citysim/world/frames.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace citysim::world;

TEST_CASE("world points map to the tile whose half-open square holds them", "[world][frames]") {
    CHECK(tile_of(WorldPos{0.0, 0.0}) == TileCoord{0, 0});
    CHECK(tile_of(WorldPos{2047.999, 4095.0}) == TileCoord{0, 1});
    CHECK(tile_of(WorldPos{2048.0, 4096.0}) == TileCoord{1, 2}); // an edge belongs to the tile east / north
    CHECK(tile_of(WorldPos{-0.5, -2048.0}) == TileCoord{-1, -1});
    CHECK(tile_of(WorldPos{-2048.5, 10.0}) == TileCoord{-2, 0});
    CHECK(tile_origin(TileCoord{3, -2}) == WorldPos{6144.0, -4096.0});
    // Wild input never overflows.
    CHECK(tile_of(WorldPos{1e300, -1e300}).tx > 1000000);
    CHECK(tile_of(WorldPos{1e300, -1e300}).ty < -1000000);
    CHECK(tile_index_of(std::numeric_limits<double>::quiet_NaN()) == 0);
}

TEST_CASE("the constants describe 4 m city tiles of 2,048 m", "[world][frames]") {
    CHECK(k_tile_size_m == 2048.0);
    CHECK(k_city_sample_spacing_m == 4.0);
    CHECK(k_city_tile_samples == 513);
    CHECK(k_city_area_tiles == 4);
    CHECK(k_city_area_size_m == 8192.0);
}

TEST_CASE("a city area is 4 x 4 tiles with its origin at the south-west tile", "[world][frames]") {
    const CityArea city{TileCoord{10, 20}};
    CHECK(city.origin() == WorldPos{20480.0, 40960.0});
    CHECK(city.contains_tile(TileCoord{10, 20}));
    CHECK(city.contains_tile(TileCoord{13, 23}));
    CHECK(!city.contains_tile(TileCoord{14, 23}));
    CHECK(!city.contains_tile(TileCoord{9, 20}));

    const auto tiles = city.tiles();
    REQUIRE(tiles.size() == 16);
    CHECK(std::is_sorted(tiles.begin(), tiles.end()));
    CHECK(tiles.front() == TileCoord{10, 20});
    CHECK(tiles.back() == TileCoord{13, 23});
    CHECK(std::all_of(tiles.begin(), tiles.end(), [&](TileCoord t) { return city.contains_tile(t); }));

    const auto ringed = city.tiles(1);
    REQUIRE(ringed.size() == 36);
    CHECK(std::is_sorted(ringed.begin(), ringed.end()));
    CHECK(ringed.front() == TileCoord{9, 19});
    CHECK(ringed.back() == TileCoord{14, 24});
    CHECK(city.tiles(-1).size() == 16);
}

TEST_CASE("city-local and world positions convert exactly through the area origin", "[world][frames]") {
    const CityArea city{TileCoord{37, 12}};
    const LocalPos local{1234.5625F, 8191.999F};
    const WorldPos world = to_world(city, local);
    CHECK(world.x == 37.0 * 2048.0 + static_cast<double>(local.x));
    CHECK(world.z == 12.0 * 2048.0 + static_cast<double>(local.z));
    CHECK(to_local(city, world) == local);
    CHECK(to_local(city, city.origin()) == LocalPos{0.0F, 0.0F});

    // A float keeps millimeters across a whole 8 km city frame.
    const LocalPos far{8191.0F, 8191.0F};
    CHECK(std::nextafter(far.x, 1e9F) - far.x < 0.001F);
}
