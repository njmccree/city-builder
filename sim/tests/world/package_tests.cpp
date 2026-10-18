#include "citysim/world/map_package.hpp"
#include "citysim/world/raster.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>

using namespace citysim::world;
using citysim::test::TempDir;
using Catch::Matchers::ContainsSubstring;
using nlohmann::json;

namespace {

// The example from the header comment of map_package.hpp, as a json value.
json example_manifest() {
    return json::parse(R"({
      "format": 1,
      "map_id": "base:map.ancient_near_east",
      "pipeline_version": "1.0.0",
      "sources": [ { "name": "Copernicus DEM GLO-30", "version": "2021_1", "license": "Copernicus DEM licence" } ],
      "projection": { "type": "laea", "lat_0": 30.0, "lon_0": 38.0 },
      "bounds_m": { "width": 3000000.0, "height": 2000000.0 },
      "region": {
        "cell_size_m": 1000.0, "width": 3000, "height": 2000,
        "layers": { "elevation": "region/elevation.csr", "land_mask": "region/land_mask.csr" }
      },
      "detail": { "tile_size_m": 2048.0, "sample_spacing_m": 32.0, "tiles": [[10, 20], [11, 20]] },
      "rivers": "rivers.json",
      "lakes": "lakes.json",
      "files": { "detail/10_20.csr": "0123456789abcdef", "region/elevation.csr": "89abcdef01234567" },
      "package_hash": "fedcba9876543210"
    })");
}

// Parses a manifest after `edit` changed the example, and returns the error text ("" if it parsed).
template <class Edit> std::string error_after(Edit&& edit) {
    json manifest = example_manifest();
    edit(manifest);
    try {
        parse_manifest(manifest, "package.json");
    } catch (const MapFormatError& error) {
        return error.what();
    }
    return "";
}

// A small valid package on disk: two region layers, one detail tile, rivers and lakes.
PackageManifest make_package(const std::filesystem::path& dir) {
    write_csr(dir / "region/elevation.csr", Raster<std::int16_t>(4, 3, 0.25F));
    write_csr(dir / "region/land_mask.csr", Raster<std::uint8_t>(4, 3));
    write_csr(dir / "detail/0_0.csr", Raster<std::int16_t>(65, 65, 0.25F));
    {
        std::ofstream(dir / "rivers.json") << "{\"rivers\": []}";
        std::ofstream(dir / "lakes.json") << "{\"lakes\": []}";
    }
    PackageManifest manifest;
    manifest.map_id = "test:map.tiny";
    manifest.pipeline_version = "test";
    manifest.sources = {{"synthetic", "1", ""}};
    manifest.projection = {"laea", 30.0, 31.0};
    manifest.bounds_width_m = 4000.0;
    manifest.bounds_height_m = 3000.0;
    manifest.region.cell_size_m = 1000.0;
    manifest.region.width = 4;
    manifest.region.height = 3;
    manifest.region.layers = {{"elevation", "region/elevation.csr"}, {"land_mask", "region/land_mask.csr"}};
    manifest.detail = {2048.0, 32.0, {{0, 0}}};
    manifest.rivers_file = "rivers.json";
    manifest.lakes_file = "lakes.json";
    for (const char* file : {"region/elevation.csr", "region/land_mask.csr", "detail/0_0.csr", "rivers.json", "lakes.json"}) {
        manifest.files[file] = hash_file(dir / file);
    }
    manifest.package_hash = compute_package_hash(manifest.files);
    std::ofstream(dir / "package.json") << manifest_to_text(manifest);
    return manifest;
}

void rewrite(const std::filesystem::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

} // namespace

TEST_CASE("hashes print as 16 hex digits and read back", "[world][package]") {
    CHECK(hash_to_hex(0) == "0000000000000000");
    CHECK(hash_to_hex(0x0123456789ABCDEFULL) == "0123456789abcdef");
    std::uint64_t value = 0;
    REQUIRE(parse_hex_hash("0123456789abcdef", value));
    CHECK(value == 0x0123456789ABCDEFULL);
    CHECK_FALSE(parse_hex_hash("0123456789ABCDEF", value)); // upper case is refused
    CHECK_FALSE(parse_hex_hash("123", value));
    CHECK_FALSE(parse_hex_hash("0123456789abcdeg", value));
}

TEST_CASE("the package hash covers file names and checksums in sorted order", "[world][package]") {
    const std::map<std::string, std::uint64_t> a{{"a.csr", 1}, {"b.csr", 2}};
    const std::map<std::string, std::uint64_t> same{{"b.csr", 2}, {"a.csr", 1}}; // insertion order is irrelevant
    CHECK(compute_package_hash(a) == compute_package_hash(same));
    CHECK(compute_package_hash(a) != compute_package_hash({{"a.csr", 1}, {"b.csr", 3}}));   // another checksum
    CHECK(compute_package_hash(a) != compute_package_hash({{"a.csr", 1}, {"c.csr", 2}}));   // another name
    CHECK(compute_package_hash(a) != compute_package_hash({{"a.csr", 1}}));                 // a file fewer
}

TEST_CASE("the documented example manifest parses", "[world][package][manifest]") {
    const PackageManifest manifest = parse_manifest(example_manifest(), "package.json");
    CHECK(manifest.map_id == "base:map.ancient_near_east");
    CHECK(manifest.pipeline_version == "1.0.0");
    REQUIRE(manifest.sources.size() == 1);
    CHECK(manifest.sources[0].name == "Copernicus DEM GLO-30");
    CHECK(manifest.projection.type == "laea");
    CHECK(manifest.projection.lat_0 == 30.0);
    CHECK(manifest.projection.lon_0 == 38.0);
    CHECK(manifest.bounds_width_m == 3000000.0);
    CHECK(manifest.region.cell_size_m == 1000.0);
    CHECK(manifest.region.width == 3000);
    CHECK(manifest.region.layers.at("land_mask") == "region/land_mask.csr");
    CHECK(manifest.detail.tile_size_m == 2048.0);
    CHECK(manifest.detail.sample_spacing_m == 32.0);
    REQUIRE(manifest.detail.tiles.size() == 2);
    CHECK(manifest.detail.tiles[1] == TileCoord{11, 20});
    CHECK(manifest.rivers_file == "rivers.json");
    CHECK(manifest.files.at("detail/10_20.csr") == 0x0123456789ABCDEFULL);
    CHECK(manifest.package_hash == 0xFEDCBA9876543210ULL);
}

TEST_CASE("a manifest round-trips through its JSON text", "[world][package][manifest]") {
    const PackageManifest manifest = parse_manifest(example_manifest(), "package.json");
    const std::string text = manifest_to_text(manifest);
    CHECK(text.back() == '\n');
    CHECK(parse_manifest(json::parse(text), "again") == manifest);
    CHECK(manifest_to_text(parse_manifest(json::parse(text), "again")) == text); // and the text is stable
}

TEST_CASE("manifest errors name the file and the JSON pointer", "[world][package][manifest]") {
    CHECK(error_after([](json&) {}).empty());

    CHECK_THAT(error_after([](json& m) { m.erase("map_id"); }),
               ContainsSubstring("package.json: /: missing required field 'map_id'"));
    CHECK_THAT(error_after([](json& m) { m["map_id"] = 5; }), ContainsSubstring("/map_id: expected a string"));
    CHECK_THAT(error_after([](json& m) { m["format"] = 2; }), ContainsSubstring("/format: unsupported package format"));
    CHECK_THAT(error_after([](json& m) { m["projection"]["type"] = "mercator"; }),
               ContainsSubstring("/projection/type: only \"laea\""));
    CHECK_THAT(error_after([](json& m) { m["projection"]["lat_0"] = 95.0; }), ContainsSubstring("/projection/lat_0"));
    CHECK_THAT(error_after([](json& m) { m["bounds_m"]["width"] = -1; }), ContainsSubstring("/bounds_m/width: expected a number above 0"));
    CHECK_THAT(error_after([](json& m) { m["region"]["width"] = 0; }), ContainsSubstring("/region/width: expected a positive whole number"));
    CHECK_THAT(error_after([](json& m) { m["region"]["height"] = 1.5; }), ContainsSubstring("/region/height"));
    CHECK_THAT(error_after([](json& m) { m["region"]["layers"] = json::object(); }), ContainsSubstring("/region/layers: needs at least one layer"));
    CHECK_THAT(error_after([](json& m) { m["region"]["layers"]["elevation"] = "../up.csr"; }),
               ContainsSubstring("/region/layers/elevation: expected a relative path"));
    CHECK_THAT(error_after([](json& m) { m["rivers"] = "/abs/rivers.json"; }), ContainsSubstring("/rivers: expected a relative path"));
    CHECK_THAT(error_after([](json& m) { m["detail"]["sample_spacing_m"] = 30.0; }),
               ContainsSubstring("/detail/sample_spacing_m: the tile size must be a whole number of samples"));
    CHECK_THAT(error_after([](json& m) { m["detail"]["tiles"] = json::array({json::array({1, 2}), json::array({1, 2})}); }),
               ContainsSubstring("/detail/tiles/1: tile [1, 2] is listed twice"));
    CHECK_THAT(error_after([](json& m) { m["detail"]["tiles"][0] = json::array({1}); }),
               ContainsSubstring("/detail/tiles/0: expected [tx, ty]"));
    CHECK_THAT(error_after([](json& m) { m["files"]["detail/10_20.csr"] = "NOT-A-HASH"; }),
               ContainsSubstring("/files/detail~110_20.csr: expected 16 lower-case hex digits"));
    CHECK_THAT(error_after([](json& m) { m["package_hash"] = "abc"; }), ContainsSubstring("/package_hash"));
    CHECK_THAT(error_after([](json& m) { m["sources"][0].erase("version"); }),
               ContainsSubstring("/sources/0: missing required field 'version'"));
    CHECK_THAT(error_after([](json& m) { m = json::array(); }), ContainsSubstring("expected an object"));
}

TEST_CASE("a package verifies, and every kind of damage is found", "[world][package][verify]") {
    const TempDir dir("package");
    const PackageManifest written = make_package(dir.path());
    CHECK(verify_package(dir.path()) == written);
    CHECK(read_manifest(dir.path()) == written);

    SECTION("a changed file fails its checksum") {
        write_csr(dir / "region/land_mask.csr", Raster<std::uint8_t>(4, 3, 2.0F)); // different bytes
        CHECK_THROWS_WITH(verify_package(dir.path()), ContainsSubstring("region/land_mask.csr") && ContainsSubstring("checksum"));
    }
    SECTION("a missing file") {
        std::filesystem::remove(dir / "detail/0_0.csr");
        CHECK_THROWS_WITH(verify_package(dir.path()), ContainsSubstring("'detail/0_0.csr' is missing"));
    }
    SECTION("a detail tile that the manifest does not list") {
        write_csr(dir / "detail/5_5.csr", Raster<std::int16_t>(65, 65));
        CHECK_THROWS_WITH(verify_package(dir.path()), ContainsSubstring("detail/5_5.csr") && ContainsSubstring("not in \"files\""));
    }
    SECTION("a referenced file that is not in the file list") {
        json manifest = json::parse(std::ifstream(dir / "package.json"));
        manifest["files"].erase("lakes.json");
        rewrite(dir / "package.json", manifest.dump(2));
        CHECK_THROWS_WITH(verify_package(dir.path()), ContainsSubstring("lakes file 'lakes.json' is not in \"files\""));
    }
    SECTION("a package hash that does not match the list") {
        json manifest = json::parse(std::ifstream(dir / "package.json"));
        manifest["package_hash"] = "0000000000000001";
        rewrite(dir / "package.json", manifest.dump(2));
        CHECK_THROWS_WITH(verify_package(dir.path()), ContainsSubstring("package_hash"));
    }
    SECTION("several problems are counted") {
        std::filesystem::remove(dir / "rivers.json");
        std::filesystem::remove(dir / "lakes.json");
        CHECK_THROWS_WITH(verify_package(dir.path()), ContainsSubstring("2 problem(s)"));
    }
    SECTION("no package.json, or one that is not JSON") {
        std::filesystem::remove(dir / "package.json");
        CHECK_THROWS_WITH(verify_package(dir.path()), ContainsSubstring("cannot open"));
        rewrite(dir / "package.json", "{ not json");
        CHECK_THROWS_WITH(verify_package(dir.path()), ContainsSubstring("not valid JSON"));
    }
}

TEST_CASE("detail tiles are named by their coordinates", "[world][package]") {
    CHECK(detail_tile_file({10, 20}) == "detail/10_20.csr");
    CHECK(detail_tile_file({-1, 0}) == "detail/-1_0.csr");
}

TEST_CASE("tile coordinates order by row, then column", "[world][package]") {
    CHECK(TileCoord{5, 1} < TileCoord{0, 2});
    CHECK(TileCoord{1, 1} < TileCoord{2, 1});
    CHECK(TileCoord{3, 3} == TileCoord{3, 3});
}
