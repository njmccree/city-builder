#include "citysim/content/content_database.hpp"
#include "citysim/content/map_def.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace citysim::content;
using citysim::test::TempDir;
using nlohmann::json;

namespace {

json valid_map() {
    return json::parse(R"({
      "type": "map",
      "id": "test:map.tiny",
      "name_key": "map.tiny.name",
      "desc_key": "map.tiny.desc",
      "package": "tiny_map",
      "bounds": { "min_lon": 25.0, "min_lat": 20.0, "max_lon": 55.0, "max_lat": 40.0 },
      "region_cell_size_m": 2000,
      "map_seed": 18446744073709551615,
      "detail_regions": [
        [[29.5, 29.0], [32.5, 29.0], [32.5, 31.8], [29.5, 31.8], [29.5, 29.0]]
      ],
      "river_regimes": ["test:river.nile", "test:river.tigris"]
    })");
}

struct Loaded {
    std::unique_ptr<ContentDatabase> db = std::make_unique<ContentDatabase>();
    LoadResult result;
    std::filesystem::path mod_dir;
};

// Loads a mod folder holding one map definition (after `edit` changed the valid one).
template <class Edit> Loaded load_map(const TempDir& dir, Edit&& edit) {
    json map = valid_map();
    edit(map);
    Loaded loaded;
    loaded.mod_dir = dir / "mod";
    dir.write("mod/defs/maps.json", json{{"defs", json::array({map})}}.dump(2));
    loaded.result = loaded.db->load({loaded.mod_dir});
    return loaded;
}

std::string all_errors(const LoadResult& result) {
    std::string text;
    for (const ContentIssue& issue : result.errors) {
        text += issue.format() + "\n";
    }
    return text;
}

bool has_error(const LoadResult& result, const std::string& needle) {
    return all_errors(result).find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("a map definition loads with all its fields", "[content][map]") {
    const TempDir dir("mapdef");
    const Loaded loaded = load_map(dir, [](json&) {});
    REQUIRE(loaded.result.ok());
    const auto& maps = loaded.db->registry<MapDef>();
    REQUIRE(maps.size() == 1);
    const MapDef& map = maps.get(*maps.find("test:map.tiny"));
    CHECK(map.name_key == "map.tiny.name");
    CHECK(map.desc_key == "map.tiny.desc");
    CHECK(map.package == "tiny_map");
    CHECK(map.min_lon == 25.0);
    CHECK(map.min_lat == 20.0);
    CHECK(map.max_lon == 55.0);
    CHECK(map.max_lat == 40.0);
    CHECK(map.region_cell_size_m == 2000.0);
    CHECK(map.map_seed == 18446744073709551615ULL); // all 64 bits survive
    REQUIRE(map.detail_regions.size() == 1);
    REQUIRE(map.detail_regions[0].size() == 5);
    CHECK(map.detail_regions[0][1] == GeoPoint{32.5, 29.0});
    CHECK(map.river_regimes == std::vector<std::string>{"test:river.nile", "test:river.tigris"});
}

TEST_CASE("the cell size defaults to 1,000 m and rivers are optional", "[content][map]") {
    const TempDir dir("mapdef_defaults");
    const Loaded loaded = load_map(dir, [](json& m) {
        m.erase("region_cell_size_m");
        m.erase("river_regimes");
    });
    REQUIRE(loaded.result.ok());
    const auto& maps = loaded.db->registry<MapDef>();
    const MapDef& map = maps.get(*maps.find("test:map.tiny"));
    CHECK(map.region_cell_size_m == 1000.0);
    CHECK(map.river_regimes.empty());
}

TEST_CASE("the package folder is under the mod's maps/ folder", "[content][map]") {
    const TempDir dir("mapdef_dir");
    const Loaded loaded = load_map(dir, [](json&) {});
    REQUIRE(loaded.result.ok());
    const auto& maps = loaded.db->registry<MapDef>();
    const auto handle = *maps.find("test:map.tiny");
    const std::filesystem::path package = map_package_dir(loaded.db->origin(handle), maps.get(handle));
    CHECK(package.generic_string() == (loaded.mod_dir / "maps" / "tiny_map").generic_string());
}

TEST_CASE("map definition problems are reported where they are", "[content][map]") {
    const TempDir dir("mapdef_errors");

    SECTION("a missing seed") {
        const Loaded loaded = load_map(dir, [](json& m) { m.erase("map_seed"); });
        CHECK(has_error(loaded.result, "missing required field 'map_seed'"));
    }
    SECTION("a negative seed") {
        const Loaded loaded = load_map(dir, [](json& m) { m["map_seed"] = -3; });
        CHECK(has_error(loaded.result, "/map_seed: expected a non-negative whole number"));
    }
    SECTION("missing bounds") {
        const Loaded loaded = load_map(dir, [](json& m) { m.erase("bounds"); });
        CHECK(has_error(loaded.result, "missing required field 'bounds'"));
    }
    SECTION("a bound of the wrong type") {
        const Loaded loaded = load_map(dir, [](json& m) { m["bounds"]["max_lat"] = "north"; });
        CHECK(has_error(loaded.result, "/bounds/max_lat: expected a number"));
    }
    SECTION("longitudes outside the globe or reversed") {
        CHECK(has_error(load_map(dir, [](json& m) { m["bounds"]["max_lon"] = 190.0; }).result, "longitudes must satisfy"));
        CHECK(has_error(load_map(dir, [](json& m) { m["bounds"]["min_lon"] = 60.0; }).result, "longitudes must satisfy"));
    }
    SECTION("latitudes outside the globe or reversed") {
        CHECK(has_error(load_map(dir, [](json& m) { m["bounds"]["min_lat"] = -91.0; }).result, "latitudes must satisfy"));
        CHECK(has_error(load_map(dir, [](json& m) { m["bounds"]["max_lat"] = 20.0; }).result, "latitudes must satisfy"));
    }
    SECTION("a cell size that is not positive") {
        CHECK(has_error(load_map(dir, [](json& m) { m["region_cell_size_m"] = 0; }).result, "/region_cell_size_m: must be greater than 0"));
    }
    SECTION("an unsafe package folder name") {
        for (const char* name : {"", "../up", "a/b", "with space", ".."}) {
            CHECK(has_error(load_map(dir, [&](json& m) { m["package"] = name; }).result, "/package"));
        }
    }
    SECTION("a polygon that is not closed") {
        const Loaded loaded = load_map(dir, [](json& m) {
            m["detail_regions"][0] = json::parse("[[29.5, 29.0], [32.5, 29.0], [32.5, 31.8], [29.5, 31.8]]");
        });
        CHECK(has_error(loaded.result, "/detail_regions/0: the polygon is not closed"));
    }
    SECTION("a polygon with fewer than three corners") {
        const Loaded loaded = load_map(dir, [](json& m) {
            m["detail_regions"][0] = json::parse("[[29.5, 29.0], [32.5, 29.0], [29.5, 29.0]]");
        });
        CHECK(has_error(loaded.result, "/detail_regions/0: a polygon needs at least three corners"));
    }
    SECTION("a polygon point that is not a pair of numbers") {
        const Loaded loaded = load_map(dir, [](json& m) { m["detail_regions"][0][2] = json::array({1.0}); });
        CHECK(has_error(loaded.result, "/detail_regions/0/2: expected a point as [lon, lat]"));
    }
    SECTION("a polygon point outside the bounds") {
        const Loaded loaded = load_map(dir, [](json& m) {
            m["detail_regions"][0] = json::parse("[[29.5, 29.0], [80.0, 29.0], [32.5, 31.8], [29.5, 29.0]]");
        });
        CHECK(has_error(loaded.result, "/detail_regions/0/1: the point lies outside the map bounds"));
    }
    SECTION("detail regions of the wrong shape") {
        CHECK(has_error(load_map(dir, [](json& m) { m["detail_regions"] = 5; }).result, "expected an array of polygons"));
        CHECK(has_error(load_map(dir, [](json& m) { m["detail_regions"] = json::array({5}); }).result, "/detail_regions/0: expected an array of [lon, lat] points"));
    }
    SECTION("a river regime that is not text") {
        const Loaded loaded = load_map(dir, [](json& m) { m["river_regimes"][1] = 7; });
        CHECK(has_error(loaded.result, "/river_regimes/1: expected an id"));
    }
    SECTION("an unknown field is only a warning") {
        const Loaded loaded = load_map(dir, [](json& m) { m["colour"] = "red"; });
        CHECK(loaded.result.ok());
        CHECK_FALSE(loaded.result.warnings.empty());
    }
}

TEST_CASE("a later mod can patch a map", "[content][map]") {
    const TempDir dir("mapdef_patch");
    dir.write("a/mod.json", R"({"id": "a", "name_key": "x", "version": "1.0.0"})");
    dir.write("a/defs/maps.json", json{{"defs", json::array({valid_map()})}}.dump());
    dir.write("b/mod.json", R"({"id": "b", "name_key": "x", "version": "1.0.0", "depends_on": ["a"]})");
    dir.write("b/defs/maps.json", R"({"defs": [{"type": "map", "id": "test:map.tiny", "patch": true, "map_seed": 99}]})");
    ContentDatabase db;
    const LoadResult result = db.load({dir / "a", dir / "b"});
    REQUIRE(result.ok());
    const auto& maps = db.registry<MapDef>();
    CHECK(maps.get(*maps.find("test:map.tiny")).map_seed == 99);
    CHECK(maps.get(*maps.find("test:map.tiny")).package == "tiny_map"); // untouched fields stay
}
