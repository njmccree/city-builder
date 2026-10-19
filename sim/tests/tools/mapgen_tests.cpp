#include "citysim/content/content_database.hpp"
#include "citysim/content/map_def.hpp"
#include "citysim/world/map_package.hpp"
#include "citysim/world/raster.hpp"
#include "mapgen/synthetic_map.hpp"
#include "support/synthetic_map.hpp"
#include "support/temp_dir.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

using namespace citysim;
using citysim::test::TempDir;
using nlohmann::json;

namespace {

mapgen::SyntheticSpec small_spec(std::uint64_t seed = 1) {
    return mapgen::SyntheticSpec{seed, 16.0};
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

json read_json(const std::filesystem::path& path) {
    return json::parse(read_text(path));
}

// Region layers by name, read from a package folder.
template <class T> world::Raster<T> layer(const std::filesystem::path& dir, const std::string& name) {
    return world::read_csr<T>(dir / "region" / (name + ".csr"));
}

} // namespace

TEST_CASE("a spec is checked", "[mapgen]") {
    CHECK(mapgen::check_spec(mapgen::SyntheticSpec{}).empty());
    CHECK(mapgen::check_spec(mapgen::SyntheticSpec{1, 16.0}).empty());
    CHECK(!mapgen::check_spec(mapgen::SyntheticSpec{1, 15.0}).empty());
    CHECK(!mapgen::check_spec(mapgen::SyntheticSpec{1, 1001.0}).empty());
    CHECK(!mapgen::check_spec(mapgen::SyntheticSpec{1, 20.5}).empty());
    const TempDir dir("mapgen_bad_spec");
    CHECK_THROWS_AS(mapgen::write_synthetic_package(mapgen::SyntheticSpec{1, 4.0}, dir / "out"),
                    std::invalid_argument);
}

TEST_CASE("the synthetic package verifies and lists what it contains", "[mapgen]") {
    const TempDir dir("mapgen_verify");
    const world::PackageManifest written = mapgen::write_synthetic_package(small_spec(), dir / "pkg");
    const world::PackageManifest verified = world::verify_package(dir / "pkg");
    CHECK(verified == written);
    CHECK(verified.map_id == mapgen::k_synthetic_map_id);
    CHECK(verified.region.width == 16);
    CHECK(verified.region.height == 16);
    CHECK(verified.region.cell_size_m == 1000.0);
    CHECK(verified.bounds_width_m == 16000.0);
    CHECK(verified.detail.tile_size_m == 2048.0);
    CHECK(verified.detail.sample_spacing_m == 32.0);
    CHECK(!verified.detail.tiles.empty());
    CHECK(std::is_sorted(verified.detail.tiles.begin(), verified.detail.tiles.end()));
    for (const char* name :
         {"elevation", "land_mask", "fertility", "vegetation", "wetness", "stone_limestone", "ore_copper"}) {
        CHECK(verified.region.layers.count(name) == 1);
    }
    // 7 region layers, the detail tiles, rivers.json and lakes.json.
    CHECK(verified.files.size() == 7 + verified.detail.tiles.size() + 2);
    CHECK(std::filesystem::exists(dir / "pkg" / "map_def.json")); // next to the package, not part of it
    CHECK(verified.files.count("map_def.json") == 0);
}

TEST_CASE("one seed gives byte-identical files and another seed does not", "[mapgen]") {
    const TempDir dir("mapgen_determinism");
    const world::PackageManifest a = mapgen::write_synthetic_package(small_spec(7), dir / "a");
    const world::PackageManifest b = mapgen::write_synthetic_package(small_spec(7), dir / "b");
    const world::PackageManifest c = mapgen::write_synthetic_package(small_spec(8), dir / "c");
    CHECK(a == b);
    CHECK(a.package_hash == b.package_hash);
    for (const auto& [path, hash] : a.files) {
        CHECK(read_text(dir / "a" / path) == read_text(dir / "b" / path));
    }
    CHECK(read_text(dir / "a" / "package.json") == read_text(dir / "b" / "package.json"));
    CHECK(read_text(dir / "a" / "map_def.json") == read_text(dir / "b" / "map_def.json"));
    CHECK(a.package_hash != c.package_hash);
    CHECK(a.files.at("region/elevation.csr") != c.files.at("region/elevation.csr"));
}

TEST_CASE("the generated map definition loads and finds its package", "[mapgen]") {
    const TempDir dir("mapgen_content");
    const mapgen::SyntheticSpec spec = small_spec();
    // A content root as a test assembles it: the definition in defs/, the package in maps/synthetic/.
    mapgen::write_synthetic_package(spec, dir / "pkg");
    dir.write("mod/defs/maps.json", read_text(dir / "pkg" / "map_def.json"));
    std::filesystem::create_directories(dir / "mod" / "maps");
    std::filesystem::copy(dir / "pkg", dir / "mod" / "maps" / mapgen::k_synthetic_package_folder,
                          std::filesystem::copy_options::recursive);

    content::ContentDatabase db;
    const content::LoadResult result = db.load({dir / "mod"});
    REQUIRE(result.ok());
    const auto& maps = db.registry<content::MapDef>();
    REQUIRE(maps.size() == 1);
    const auto handle = *maps.find(mapgen::k_synthetic_map_id);
    const content::MapDef& map = maps.get(handle);
    CHECK(map.map_seed == spec.seed);
    CHECK(map.region_cell_size_m == 1000.0);
    REQUIRE(map.detail_regions.size() == 1);

    const std::filesystem::path package_dir = content::map_package_dir(db.origin(handle), map);
    const world::PackageManifest manifest = world::verify_package(package_dir);
    CHECK(manifest.map_id == map.id.str());
}

TEST_CASE("a mod written by mapgen loads next to the base game", "[mapgen]") {
    const TempDir dir("mapgen_mod");
    const world::PackageManifest written = mapgen::write_synthetic_mod(small_spec(), dir / "content_dev");

    content::ContentDatabase db;
    content::ModLoadRequest request;
    request.roots = {std::filesystem::path(CITYSIM_GAME_CONTENT_DIR), dir / "content_dev"};
    request.order.enabled = {"base", mapgen::k_synthetic_mod_id};
    request.order.require_base = true;
    const content::LoadResult result = db.load_mods(request); // localization keys checked too
    INFO((result.errors.empty() ? std::string() : result.errors.front().format_full()));
    REQUIRE(result.ok());
    const auto handle = db.registry<content::MapDef>().find(mapgen::k_synthetic_map_id);
    REQUIRE(handle.has_value());
    const content::MapDef& map = db.registry<content::MapDef>().get(*handle);
    const world::PackageManifest manifest =
        world::verify_package(content::map_package_dir(db.origin(*handle), map));
    CHECK(manifest.package_hash == written.package_hash);
}

namespace {

// Checks that hold for any synthetic package, and that the full-size one makes easy to see.
void check_terrain_shape(const std::filesystem::path& dir) {
    const world::PackageManifest manifest = world::verify_package(dir);
    const auto elevation = layer<std::int16_t>(dir, "elevation");
    const auto land = layer<std::uint8_t>(dir, "land_mask");
    const auto fertility = layer<std::uint8_t>(dir, "fertility");
    const std::uint32_t n = elevation.width();
    REQUIRE(elevation.height() == n);

    // The north edge is sea, the south edge is land, and the land mask agrees with the elevation.
    for (std::uint32_t x = 0; x < n; ++x) {
        CHECK(elevation.value(x, n - 1) < 0.0F);
        CHECK(land.at(x, n - 1) == 0);
        CHECK(elevation.value(x, 0) > 0.0F);
        CHECK(land.at(x, 0) == 1);
    }
    for (std::uint32_t y = 0; y < n; ++y) {
        for (std::uint32_t x = 0; x < n; ++x) {
            CHECK((land.at(x, y) == 1) == (elevation.value(x, y) > 0.0F));
        }
    }

    // In the south the valley floor lies between plateaus that are 60-120 m higher. Row 1 is far from the
    // river mouth, so the valley is narrow and the plateau cells at the edges are desert.
    const std::uint32_t row = 1;
    float lowest = 1e9F;
    std::uint32_t valley_x = 0;
    for (std::uint32_t x = 0; x < n; ++x) {
        if (elevation.value(x, row) < lowest) {
            lowest = elevation.value(x, row);
            valley_x = x;
        }
    }
    CHECK(valley_x > n / 4);
    CHECK(valley_x < n - n / 4);
    CHECK(elevation.value(0, row) - lowest > 40.0F);
    CHECK(elevation.value(n - 1, row) - lowest > 40.0F);
    CHECK(fertility.value(valley_x, row) > 0.8F);
    CHECK(fertility.value(0, row) < 0.2F);
    CHECK(fertility.value(n - 1, row) < 0.2F);

    // Deposits exist, and only on land.
    for (const char* name : {"stone_limestone", "ore_copper"}) {
        const auto deposit = layer<std::uint8_t>(dir, name);
        bool any = false;
        for (std::uint32_t y = 0; y < n; ++y) {
            for (std::uint32_t x = 0; x < n; ++x) {
                any = any || deposit.at(x, y) > 0;
                if (deposit.at(x, y) > 0) {
                    CHECK(land.at(x, y) == 1);
                }
            }
        }
        CHECK(any);
    }
}

} // namespace

TEST_CASE("a small synthetic map has a valley, a coast and deposits", "[mapgen]") {
    const TempDir dir("mapgen_shape");
    mapgen::write_synthetic_package(small_spec(), dir / "pkg");
    check_terrain_shape(dir / "pkg");
}

TEST_CASE("the shared synthetic package has the same shape", "[mapgen][synthetic_map]") {
    check_terrain_shape(test::synthetic_map_dir());
}

TEST_CASE("the shared synthetic package verifies and has a full-size detail region",
          "[mapgen][synthetic_map]") {
    const world::PackageManifest manifest = world::verify_package(test::synthetic_map_dir());
    CHECK(manifest.region.width == 96);
    CHECK(manifest.detail.tiles.size() == 375);
    // A city area is 4 x 4 tiles, so the detail region has to hold some: find a full 4 x 4 block of tiles.
    std::set<std::pair<int, int>> tiles;
    for (const world::TileCoord& tile : manifest.detail.tiles) {
        tiles.insert({tile.tx, tile.ty});
    }
    bool found = false;
    for (const world::TileCoord& origin : manifest.detail.tiles) {
        bool all = true;
        for (int dy = 0; dy < 4 && all; ++dy) {
            for (int dx = 0; dx < 4 && all; ++dx) {
                all = tiles.count({origin.tx + dx, origin.ty + dy}) == 1;
            }
        }
        found = found || all;
    }
    CHECK(found);
}

TEST_CASE("detail tiles agree with the region elevation and with each other", "[mapgen][synthetic_map]") {
    const std::filesystem::path dir = test::synthetic_map_dir();
    const world::PackageManifest manifest = world::verify_package(dir);
    const auto region = layer<std::int16_t>(dir, "elevation");

    std::map<std::pair<int, int>, world::Raster<std::int16_t>> tiles;
    for (const world::TileCoord& tile : manifest.detail.tiles) {
        tiles.emplace(std::make_pair(tile.tx, tile.ty),
                      world::read_csr<std::int16_t>(dir / world::detail_tile_file(tile)));
    }
    for (const auto& [coord, tile] : tiles) {
        REQUIRE(tile.width() == 65);
        REQUIRE(tile.height() == 65);
        // The edge samples are shared with the east and north neighbors.
        if (const auto east = tiles.find({coord.first + 1, coord.second}); east != tiles.end()) {
            for (std::uint32_t row = 0; row < 65; ++row) {
                CHECK(tile.at(64, row) == east->second.at(0, row));
            }
        }
        if (const auto north = tiles.find({coord.first, coord.second + 1}); north != tiles.end()) {
            for (std::uint32_t col = 0; col < 65; ++col) {
                CHECK(tile.at(col, 64) == north->second.at(col, 0));
            }
        }
    }

    // The mean of the detail samples inside a region cell matches the region value of that cell.
    int compared = 0;
    for (const auto& [coord, tile] : tiles) {
        std::map<std::pair<int, int>, std::pair<double, int>> sums;
        for (std::uint32_t row = 0; row < 64; ++row) {
            for (std::uint32_t col = 0; col < 64; ++col) {
                const double x = coord.first * 2048.0 + col * 32.0;
                const double z = coord.second * 2048.0 + row * 32.0;
                auto& sum = sums[{static_cast<int>(x / 1000.0), static_cast<int>(z / 1000.0)}];
                sum.first += static_cast<double>(tile.value(col, row));
                sum.second += 1;
            }
        }
        for (const auto& [cell, sum] : sums) {
            if (sum.second < 900) { // only cells that lie almost entirely inside this tile (a 1 km cell: 961)
                continue;
            }
            const double mean = sum.first / sum.second;
            CHECK(std::abs(mean -
                           static_cast<double>(region.value(static_cast<std::uint32_t>(cell.first),
                                                            static_cast<std::uint32_t>(cell.second)))) < 8.0);
            ++compared;
        }
    }
    CHECK(compared > 100);
}

TEST_CASE("the river is one channel that splits into three branches", "[mapgen][synthetic_map]") {
    const std::filesystem::path dir = test::synthetic_map_dir();
    const json rivers = read_json(dir / "rivers.json");
    REQUIRE(rivers["rivers"].size() == 1);
    const json& reaches = rivers["rivers"][0]["reaches"];
    REQUIRE(reaches.size() == 4);
    REQUIRE(reaches[0]["id"] == "main");
    CHECK(reaches[0]["downstream"] == json::array({"west", "center", "east"}));

    const auto elevation = layer<std::int16_t>(dir, "elevation");
    for (const json& reach : reaches) {
        const bool is_main = reach["id"] == "main";
        CHECK(reach["discharge_m3s"].get<double>() > 0.0);
        const json& vertices = reach["vertices"];
        REQUIRE(vertices.size() > 10);
        double previous_level = 1e9;
        for (const json& v : vertices) {
            const double x = v["x"].get<double>();
            const double z = v["z"].get<double>();
            CHECK(x >= 0.0);
            CHECK(x <= 96000.0);
            CHECK(z >= 0.0);
            CHECK(z <= 96000.0);
            const double width = v["width_m"].get<double>();
            if (is_main) {
                CHECK(width >= 200.0);
                CHECK(width <= 600.0);
            } else {
                CHECK(width >= 80.0);
                CHECK(width <= 200.0);
            }
            CHECK(v["depth_m"].get<double>() > 0.0);
            const double level = v["water_level_m"].get<double>();
            CHECK(level <= previous_level); // water runs downhill
            CHECK(level >= 0.0);
            previous_level = level;
            if (is_main) { // the channel lies below the surrounding ground
                const float ground = elevation.value(static_cast<std::uint32_t>(x / 1000.0),
                                                     static_cast<std::uint32_t>(z / 1000.0));
                CHECK(level <= static_cast<double>(ground) + 1.0);
            }
        }
    }
    // The branches start where the main channel ends.
    const json& end = reaches[0]["vertices"].back();
    for (std::size_t i = 1; i < reaches.size(); ++i) {
        CHECK(reaches[i]["vertices"][0]["x"] == end["x"]);
        CHECK(reaches[i]["vertices"][0]["z"] == end["z"]);
    }
    // And lakes.json is empty.
    CHECK(read_json(dir / "lakes.json")["lakes"].empty());
}
