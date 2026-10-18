#include "citysim/world/loaded_map.hpp"
#include "mapgen/synthetic_map.hpp"
#include "support/synthetic_map.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fstream>
#include <thread>
#include <vector>

using namespace citysim;
using namespace citysim::world;
using citysim::test::TempDir;

namespace {

std::shared_ptr<const LoadedMap> small_map(const TempDir& dir) {
    mapgen::write_synthetic_package(mapgen::SyntheticSpec{1, 16.0}, dir / "pkg");
    return LoadedMap::open(dir / "pkg");
}

} // namespace

TEST_CASE("a package opens with its manifest and decoded region layers", "[world][map]") {
    const TempDir dir("loaded_map_open");
    const auto map = small_map(dir);
    CHECK(map->map_id() == mapgen::k_synthetic_map_id);
    CHECK(map->package_hash() == read_manifest(dir / "pkg").package_hash);
    CHECK(map->width_m() == 16000.0);
    CHECK(map->tiles_x() == 8); // 16,000 m / 2,048 m, rounded up
    CHECK(map->tiles_y() == 8);
    CHECK(map->contains_tile(TileCoord{7, 7}));
    CHECK(!map->contains_tile(TileCoord{8, 0}));
    CHECK(!map->contains_tile(TileCoord{0, -1}));
    CHECK(map->detail_samples() == 65);
    CHECK(map->detail_spacing_m() == 32.0);

    const auto stored = read_csr<std::int16_t>(dir / "pkg" / "region" / "elevation.csr");
    const Raster<float>& elevation = map->region_elevation();
    REQUIRE(elevation.width() == stored.width());
    CHECK(elevation.at(5, 7) == stored.value(5, 7));
    REQUIRE(map->region_layer("fertility") != nullptr);
    CHECK(map->region_layer("no_such_layer") == nullptr);
    // A region cell's value sits at its center.
    CHECK(map->region_height(WorldPos{5500.0, 7500.0}) == stored.value(5, 7));
}

TEST_CASE("opening checks the package and the map id", "[world][map]") {
    const TempDir dir("loaded_map_checks");
    mapgen::write_synthetic_package(mapgen::SyntheticSpec{1, 16.0}, dir / "pkg");
    CHECK_THROWS_AS(LoadedMap::open(dir / "pkg", "test:map.other"), MapFormatError);
    CHECK(LoadedMap::open(dir / "pkg", mapgen::k_synthetic_map_id) != nullptr);
    {
        std::ofstream damage(dir / "pkg" / detail_tile_file(TileCoord{3, 4}),
                             std::ios::binary | std::ios::app);
        damage << "x";
    }
    CHECK_THROWS_AS(LoadedMap::open(dir / "pkg"), MapFormatError);
    CHECK_THROWS_AS(LoadedMap::open(dir / "missing"), MapFormatError);
}

TEST_CASE("detail tiles load on first use and are shared afterwards", "[world][map]") {
    const TempDir dir("loaded_map_detail");
    const auto map = small_map(dir);
    CHECK(map->has_detail_tile(TileCoord{2, 3}));
    CHECK(!map->has_detail_tile(TileCoord{0, 0}));
    CHECK(map->detail_tile(TileCoord{0, 0}) == nullptr);

    const auto tile = map->detail_tile(TileCoord{2, 3});
    REQUIRE(tile != nullptr);
    CHECK(tile->width() == 65);
    CHECK(map->detail_tile(TileCoord{2, 3}) == tile);
    const auto stored = read_csr<std::int16_t>(dir / "pkg" / detail_tile_file(TileCoord{2, 3}));
    CHECK(tile->at(10, 20) == stored.value(10, 20));

    // Many threads asking at once all get the same tile.
    std::vector<std::shared_ptr<const Raster<float>>> seen(8);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < seen.size(); ++i) {
        threads.emplace_back([&, i] { seen[i] = map->detail_tile(TileCoord{4, 5}); });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (const auto& one : seen) {
        CHECK(one == map->detail_tile(TileCoord{4, 5}));
    }
}

TEST_CASE("the DEM height uses the detail tiles where they exist and the region elsewhere", "[world][map]") {
    const TempDir dir("loaded_map_dem");
    const auto map = small_map(dir);
    const auto tile = map->detail_tile(TileCoord{3, 4});
    REQUIRE(tile != nullptr);
    // At a detail sample the height is that sample.
    const WorldPos sample{3 * 2048.0 + 10 * 32.0, 4 * 2048.0 + 7 * 32.0};
    CHECK(map->dem_height(sample) == tile->at(10, 7));
    // Halfway between two samples, their mean.
    const WorldPos between{sample.x + 16.0, sample.z};
    CHECK(std::abs(map->dem_height(between) - 0.5F * (tile->at(10, 7) + tile->at(11, 7))) < 1e-4F);
    // Shared edges agree, so the tile chosen on an edge does not matter.
    const auto east = map->detail_tile(TileCoord{4, 4});
    for (std::uint32_t row = 0; row < 65; ++row) {
        CHECK(tile->at(64, row) == east->at(0, row));
    }
    // Outside the detail tiles: the region raster.
    const WorldPos desert{500.0, 500.0};
    CHECK(map->dem_height(desert) == map->region_height(desert));
    // On the edge between a detail tile and one without detail, the detail tile answers.
    const WorldPos edge{2 * 2048.0, 4 * 2048.0 + 7 * 32.0};
    CHECK(map->dem_height(edge) == map->detail_tile(TileCoord{2, 4})->at(0, 7));
    CHECK(std::isnan(map->dem_height(WorldPos{std::nan(""), 0.0})));
}

TEST_CASE("the full-size synthetic package opens", "[world][map][synthetic_map]") {
    const auto map = LoadedMap::open(citysim::test::synthetic_map_dir());
    CHECK(map->tiles_x() == 47);
    CHECK(map->manifest().detail.tiles.size() == 375);
    CHECK(map->has_detail_tile(TileCoord{16, 18}));
    CHECK(!map->has_detail_tile(TileCoord{15, 18}));
}
