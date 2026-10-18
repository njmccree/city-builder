#include "citysim/content/content_database.hpp"
#include "citysim/core/job_system.hpp"
#include "citysim/core/rng.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/world/river_network.hpp"
#include "citysim/world/terrain_synth.hpp"
#include "citysim/world/water_queries.hpp"
#include "citysim/world/water_state.hpp"
#include "citysim/world/world_module.hpp"
#include "support/map_content.hpp"
#include "support/synthetic_map.hpp"
#include "support/temp_dir.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

using namespace citysim;
using namespace citysim::world;
using citysim::test::MapContent;
using citysim::test::TempDir;
using nlohmann::json;

namespace {

std::shared_ptr<const LoadedMap> full_map() {
    return LoadedMap::open(citysim::test::synthetic_map_dir());
}

// A rivers.json with one river of two reaches, as a base for the error cases.
json valid_rivers() {
    return json::parse(R"({
      "format": 1,
      "rivers": [ {
        "id": "nile", "name": "Nile",
        "reaches": [
          { "id": "upper", "order": 2, "discharge_m3s": 100.0, "downstream": ["lower"],
            "vertices": [ { "x": 0, "z": 0, "width_m": 40, "depth_m": 4, "water_level_m": 10 },
                          { "x": 0, "z": 1000, "width_m": 60, "depth_m": 6, "water_level_m": 8 } ] },
          { "id": "lower", "order": 1, "discharge_m3s": 100.0, "downstream": [],
            "vertices": [ { "x": 0, "z": 1000, "width_m": 60, "depth_m": 6, "water_level_m": 8 },
                          { "x": 500, "z": 2000, "width_m": 80, "depth_m": 8, "water_level_m": 6 } ] }
        ] } ]
    })");
}

std::string parse_error(const json& rivers) {
    try {
        RiverNetwork::parse(rivers, "rivers.json");
    } catch (const MapFormatError& error) {
        return error.what();
    }
    return {};
}

// The distance from p to the segment a-b, computed independently of the network.
double brute_distance(const RiverVertex& a, const RiverVertex& b, double px, double pz) {
    const double dx = b.x - a.x;
    const double dz = b.z - a.z;
    const double t = std::clamp(((px - a.x) * dx + (pz - a.z) * dz) / (dx * dx + dz * dz), 0.0, 1.0);
    return std::hypot(a.x + t * dx - px, a.z + t * dz - pz);
}

// The main reach's vertex nearest a northing, a place the river certainly is.
const RiverVertex& main_vertex_near(const RiverNetwork& rivers, double z) {
    const auto& vertices = rivers.reaches().at(0).vertices;
    return *std::min_element(
        vertices.begin(), vertices.end(),
        [&](const RiverVertex& a, const RiverVertex& b) { return std::abs(a.z - z) < std::abs(b.z - z); });
}

struct Active {
    std::shared_ptr<const LoadedMap> map = full_map();
    WorldTerrain terrain{map, 1};
    core::JobSystem jobs{2};
    WaterState water{map->rivers().reach_count()};

    void activate_around(WorldPos pos) {
        const TileCoord home = tile_of(pos);
        std::vector<TileCoord> tiles;
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                tiles.push_back(TileCoord{home.tx + dx, home.ty + dy});
            }
        }
        std::sort(tiles.begin(), tiles.end());
        terrain.activate_tiles(jobs, tiles);
    }
};

WorldPos at(const RiverVertex& vertex) {
    return WorldPos{vertex.x, vertex.z};
}

} // namespace

TEST_CASE("a rivers.json becomes a network of numbered reaches", "[world][rivers]") {
    const RiverNetwork network = RiverNetwork::parse(valid_rivers(), "rivers.json");
    REQUIRE(network.reach_count() == 2);
    CHECK(network.reaches()[0].id == "upper");
    CHECK(network.reaches()[1].id == "lower");
    CHECK(network.reaches()[0].river_name == "Nile");
    CHECK(network.reaches()[0].downstream == std::vector<std::uint32_t>{1});
    CHECK(network.reaches()[1].downstream.empty());
    CHECK(network.reaches()[1].vertices[1].level_m == 6.0F);
    CHECK(network.max_half_width_m() == 40.0);
    CHECK(network.find_reach("nile", "lower") == std::optional<std::uint32_t>(1));
    CHECK(!network.find_reach("nile", "none").has_value());
    CHECK(!network.find_reach("tigris", "lower").has_value());
    CHECK(network.reaches_of_river("Nile") == std::vector<std::uint32_t>{0, 1});
    CHECK(network.reaches_of_river("Tigris").empty());
}

TEST_CASE("rivers.json problems name the field", "[world][rivers]") {
    const auto expect = [](json rivers, const std::string& needle) {
        const std::string message = parse_error(rivers);
        INFO(message);
        CHECK(message.find(needle) != std::string::npos);
    };
    json bad = valid_rivers();
    bad["format"] = 2;
    expect(bad, "/format: unsupported rivers format");
    bad = valid_rivers();
    bad["rivers"][0]["reaches"][0]["vertices"][1]["width_m"] = 0;
    expect(bad, "/rivers/0/reaches/0/vertices/1/width_m: must be greater than 0");
    bad = valid_rivers();
    bad["rivers"][0]["reaches"][1]["vertices"][0]["depth_m"] = -1;
    expect(bad, "/rivers/0/reaches/1/vertices/0/depth_m: must be greater than 0");
    bad = valid_rivers();
    bad["rivers"][0]["reaches"][0]["vertices"].erase(1);
    expect(bad, "/rivers/0/reaches/0/vertices: a reach needs at least 2 vertices");
    bad = valid_rivers();
    bad["rivers"][0]["reaches"][0]["downstream"] = json::array({"nowhere"});
    expect(bad, "/rivers/0/reaches/0/downstream: the reach 'nowhere' does not exist");
    bad = valid_rivers();
    bad["rivers"][0]["reaches"][0]["downstream"] = json::array({"upper"});
    expect(bad, "a reach cannot flow into itself");
    bad = valid_rivers();
    bad["rivers"][0]["reaches"][1]["id"] = "upper";
    expect(bad, "the reach id 'upper' is used twice");
    bad = valid_rivers();
    bad["rivers"].push_back(bad["rivers"][0]);
    expect(bad, "the river id 'nile' is used twice");
    bad = valid_rivers();
    bad["rivers"][0].erase("name");
    expect(bad, "/rivers/0/name: missing field");
    bad = valid_rivers();
    bad["rivers"][0]["reaches"][0]["vertices"][0]["x"] = "west";
    expect(bad, "/rivers/0/reaches/0/vertices/0/x: expected a number");
    CHECK(parse_error(valid_rivers()).empty());
}

TEST_CASE("the nearest-channel search agrees with a brute-force search", "[world][rivers][synthetic_map]") {
    const auto map = full_map();
    const RiverNetwork& rivers = map->rivers();
    REQUIRE(rivers.reach_count() == 4);
    const double radius = rivers.max_half_width_m() + 24.0;
    int found = 0;
    for (std::uint64_t i = 0; i < 400; ++i) {
        // Points near random vertices of random reaches, up to 600 m off, so most are within the search
        // radius.
        const Reach& pick = rivers.reaches()[rng::hash64(1, 11, 0, i, 2) % rivers.reach_count()];
        const RiverVertex& v = pick.vertices[rng::hash64(1, 11, 0, i, 3) % pick.vertices.size()];
        const WorldPos pos{v.x + 1200.0 * (rng::uniform_double01(rng::hash64(1, 11, 0, i, 0)) - 0.5),
                           v.z + 1200.0 * (rng::uniform_double01(rng::hash64(1, 11, 0, i, 1)) - 0.5)};
        double expected = 1e18;
        for (const Reach& reach : rivers.reaches()) {
            for (std::size_t vi = 0; vi + 1 < reach.vertices.size(); ++vi) {
                expected = std::min(expected,
                                    brute_distance(reach.vertices[vi], reach.vertices[vi + 1], pos.x, pos.z));
            }
        }
        double nearest = 1e18;
        std::size_t visits = 0;
        rivers.for_each_near(pos, radius, [&](const ChannelSample& sample) {
            nearest = std::min(nearest, static_cast<double>(sample.distance_m));
            ++visits;
        });
        if (expected <= radius) {
            ++found;
            CHECK(std::abs(nearest - expected) < 0.01); // float rounding of the distance only
        }
        CHECK(visits < 64); // a bounded search, not a scan of the whole river
    }
    CHECK(found > 40);
}

TEST_CASE("each segment is visited once per search", "[world][rivers][synthetic_map]") {
    const auto map = full_map();
    const RiverNetwork& rivers = map->rivers();
    const RiverVertex& vertex = main_vertex_near(rivers, 45000.0);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> seen;
    rivers.for_each_near(at(vertex), 3000.0, [&](const ChannelSample& sample) {
        seen.emplace_back(sample.reach, sample.segment);
    });
    REQUIRE(!seen.empty());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sorted = seen;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    // And a box test: a tile on the river is near it, a tile in the desert is not.
    CHECK(rivers.any_near_box(vertex.x - 100, vertex.z - 100, vertex.x + 100, vertex.z + 100, 10.0));
    CHECK(!rivers.any_near_box(1000.0, 1000.0, 3000.0, 3000.0, 400.0));
}

TEST_CASE("the channel is carved to the documented bed and bank profile",
          "[world][terrain][water][synthetic_map]") {
    Active g;
    const RiverVertex& vertex = main_vertex_near(g.map->rivers(), 50000.0);
    REQUIRE(vertex.z > 40000.0); // inside the detail region, so the bed is carved in city tiles
    g.activate_around(at(vertex));
    const TerrainDefines defines;
    const double half = 0.5 * static_cast<double>(vertex.width_m);
    // The middle of the channel is at level - depth (the sample next to the vertex, up to 3 m off the line).
    const float middle = g.terrain.height_at(at(vertex));
    CHECK(std::abs(static_cast<double>(middle) - (vertex.level_m - vertex.depth_m)) < 0.6);

    // Along a line across the channel the bed rises from the middle to the edge, and the bank climbs on to
    // the surrounding ground, without a step: neighboring 4 m samples never differ by more than the steepest
    // slope the profile allows (a bank of 24 m climbing a few meters).
    const auto& vertices = g.map->rivers().reaches().at(0).vertices;
    const std::size_t index = static_cast<std::size_t>(&vertex - vertices.data());
    const RiverVertex& next = vertices[index + 1];
    const double along_x = next.x - vertex.x;
    const double along_z = next.z - vertex.z;
    const double length = std::hypot(along_x, along_z);
    const double across_x = -along_z / length;
    const double across_z = along_x / length;
    float previous = middle;
    float edge_height = 0.0F;
    float ground = 0.0F;
    for (double d = 0.0; d <= half + 80.0; d += 4.0) {
        const float h = g.terrain.height_at(WorldPos{vertex.x + across_x * d, vertex.z + across_z * d});
        if (d > 0.0) {
            CHECK(std::abs(h - previous) < 2.0F);
        }
        if (d <= half - 0.01 && d + 4.0 > half) {
            edge_height = h;
        }
        if (d >= half + 70.0) {
            ground = h;
        }
        previous = h;
    }
    // At the channel edge about edge_fraction of the depth is left below the level; the bank ends at the
    // ground, which lies above the water.
    CHECK(std::abs(static_cast<double>(edge_height) -
                   (vertex.level_m - vertex.depth_m * defines.channel_edge_depth_fraction)) < 0.9);
    CHECK(ground > vertex.level_m);
    CHECK(middle < edge_height);
    CHECK(edge_height < ground);
}

TEST_CASE("carving is local and follows the defines", "[world][terrain][water][synthetic_map]") {
    const auto map = full_map();
    const RiverVertex& vertex = main_vertex_near(map->rivers(), 50000.0);
    const TileCoord coord = tile_of(at(vertex));
    const auto standard = TerrainSynth::make_tile(*map, coord, 1);
    TerrainDefines wide;
    wide.bank_width_m = 80.0F;
    wide.channel_edge_depth_fraction = 1.0F; // a bed that is flat at the water level
    const auto changed = TerrainSynth::make_tile(*map, coord, 1, wide);
    REQUIRE(standard->heights().size() == changed->heights().size());

    const double radius = map->rivers().max_half_width_m() + 80.0;
    const WorldPos origin = tile_origin(coord);
    std::size_t differing = 0;
    std::size_t far_differing = 0;
    for (std::uint32_t sy = 0; sy < k_city_tile_samples; ++sy) {
        for (std::uint32_t sx = 0; sx < k_city_tile_samples; ++sx) {
            const WorldPos pos{origin.x + sx * 4.0, origin.z + sy * 4.0};
            const bool same = standard->sample(sx, sy) == changed->sample(sx, sy);
            differing += same ? 0 : 1;
            if (!same && !map->rivers().any_near_box(pos.x, pos.z, pos.x, pos.z, radius)) {
                ++far_differing;
            }
            // The bed never rises above the ground: a flatter bed and a wider bank can only lower or keep a
            // sample relative to the ground, so both versions are at or below the same ground.
        }
    }
    CHECK(differing > 100);    // the profile does change the heights near the channel
    CHECK(far_differing == 0); // and only there
}

TEST_CASE("channel banks are continuous across tile borders", "[world][terrain][water][synthetic_map]") {
    Active g;
    const RiverVertex& vertex =
        main_vertex_near(g.map->rivers(), 49152.0); // near a tile row border (24 x 2048)
    g.activate_around(at(vertex));
    const TileCoord home = tile_of(at(vertex));
    std::size_t carved_edge_samples = 0;
    for (std::int32_t dx = -1; dx <= 0; ++dx) {
        for (std::int32_t dy = -1; dy <= 0; ++dy) {
            const auto a = g.terrain.tile(TileCoord{home.tx + dx, home.ty + dy});
            const auto east = g.terrain.tile(TileCoord{home.tx + dx + 1, home.ty + dy});
            const auto north = g.terrain.tile(TileCoord{home.tx + dx, home.ty + dy + 1});
            REQUIRE(a != nullptr);
            for (std::uint32_t i = 0; i < k_city_tile_samples; ++i) {
                REQUIRE(a->sample(k_city_tile_samples - 1, i) == east->sample(0, i));
                REQUIRE(a->sample(i, k_city_tile_samples - 1) == north->sample(i, 0));
                // Count the border samples the channel itself touches (within its half width), which are
                // carved.
                const WorldPos origin = tile_origin(a->coord());
                const WorldPos on_east{origin.x + 2048.0, origin.z + i * 4.0};
                const WorldPos on_north{origin.x + i * 4.0, origin.z + 2048.0};
                for (const WorldPos& edge : {on_east, on_north}) {
                    bool inside = false;
                    g.map->rivers().for_each_near(edge, 0.0, [&](const ChannelSample& sample) {
                        inside = inside || sample.distance_m <= sample.half_width_m;
                    });
                    carved_edge_samples += inside ? 1U : 0U;
                }
            }
        }
    }
    CHECK(carved_edge_samples > 0); // the test did cross the channel
}

TEST_CASE("water queries find the river, the sea and dry land", "[world][water][synthetic_map]") {
    Active g;
    const RiverVertex& vertex = main_vertex_near(g.map->rivers(), 50000.0);
    g.activate_around(at(vertex));

    // On the middle of the channel: water at the vertex's level, about its depth.
    const std::optional<float> level = water_level_at(g.terrain, g.water, at(vertex));
    REQUIRE(level.has_value());
    CHECK(std::abs(*level - vertex.level_m) < 0.05F);
    CHECK(is_water(g.terrain, g.water, at(vertex)));
    CHECK(std::abs(water_depth(g.terrain, g.water, at(vertex)) - vertex.depth_m) < 0.6F);

    // On the desert plateau: dry.
    const WorldPos plateau{vertex.x + 9000.0, vertex.z};
    g.activate_around(plateau);
    CHECK(!water_level_at(g.terrain, g.water, plateau).has_value());
    CHECK(!is_water(g.terrain, g.water, plateau));
    CHECK(water_depth(g.terrain, g.water, plateau) == 0.0F);

    // Out at sea, past the delta: the surface is the sea level, and it is water.
    const WorldPos sea{48000.0, 95000.0};
    const std::optional<float> sea_level = water_level_at(g.terrain, g.water, sea);
    REQUIRE(sea_level.has_value());
    CHECK(*sea_level == k_sea_level_m);
    CHECK(is_water(g.terrain, g.water, sea));
    CHECK(water_depth(g.terrain, g.water, sea) > 10.0F);

    // Just beside the channel, on its bank: no water (the ground is above the surface there).
    const auto& vertices = g.map->rivers().reaches().at(0).vertices;
    const RiverVertex& next = vertices[static_cast<std::size_t>(&vertex - vertices.data()) + 1];
    const double length = std::hypot(next.x - vertex.x, next.z - vertex.z);
    const double half = 0.5 * static_cast<double>(vertex.width_m);
    const WorldPos bank{vertex.x - (next.z - vertex.z) / length * (half + 30.0),
                        vertex.z + (next.x - vertex.x) / length * (half + 30.0)};
    CHECK(!is_water(g.terrain, g.water, bank));
}

TEST_CASE("a reach's level offset raises and lowers its water", "[world][water][synthetic_map]") {
    Active g;
    const RiverVertex& vertex = main_vertex_near(g.map->rivers(), 50000.0);
    g.activate_around(at(vertex));
    const float base_level = *water_level_at(g.terrain, g.water, at(vertex));
    const float base_depth = water_depth(g.terrain, g.water, at(vertex));

    g.water.set_level_offset(0, 2.5F); // the main reach in flood
    CHECK(std::abs(*water_level_at(g.terrain, g.water, at(vertex)) - (base_level + 2.5F)) < 1e-4F);
    CHECK(std::abs(water_depth(g.terrain, g.water, at(vertex)) - (base_depth + 2.5F)) < 1e-3F);

    g.water.set_level_offset(0, -100.0F); // a river that has run dry
    CHECK(!is_water(g.terrain, g.water, at(vertex)));
    CHECK(water_depth(g.terrain, g.water, at(vertex)) == 0.0F);

    g.water.set_level_offset(0, 0.0F);
    CHECK(water_depth(g.terrain, g.water, at(vertex)) == base_depth);

    // Another reach's offset does not move this one.
    g.water.set_level_offset(2, 5.0F);
    CHECK(water_depth(g.terrain, g.water, at(vertex)) == base_depth);
}

TEST_CASE("the water state checks its input", "[world][water]") {
    WaterState water(3);
    CHECK(water.reach_count() == 3);
    CHECK(water.level_offset(1) == 0.0F);
    CHECK(water.level_offset(99) == 0.0F);
    water.set_level_offset(1, 1.5F);
    CHECK(water.level_offset(1) == 1.5F);
    water.set_level_offset(1, -0.0F);
    CHECK(!std::signbit(water.level_offset(1)));
    CHECK_THROWS_AS(water.set_level_offset(3, 1.0F), std::out_of_range);
    CHECK_THROWS_AS(water.set_level_offset(0, std::numeric_limits<float>::quiet_NaN()),
                    std::invalid_argument);
    CHECK_THROWS_AS(water.restore({1.0F, std::numeric_limits<float>::infinity()}), std::invalid_argument);
    water.restore({1.0F, 2.0F});
    CHECK(water.reach_count() == 2);
    water.reset(4);
    CHECK(water.reach_count() == 4);
    CHECK(water.level_offset(0) == 0.0F);
}

TEST_CASE("the level offsets are hashed and saved, and nothing derived is", "[world][water][synthetic_map]") {
    TempDir dir("water_save");
    const MapContent content_map(dir, citysim::test::synthetic_map_dir());
    content::ContentDatabase db;
    REQUIRE(db.load({content_map.mod_dir()}).ok());
    const auto shared = open_map(db, MapContent::map_id());

    const auto make = [&] {
        auto sim = std::make_unique<core::Simulation>(3, 2);
        install(*sim, db, WorldConfig{MapContent::map_id(), shared});
        return sim;
    };
    auto a = make();
    auto b = make();
    CHECK(a->world().ctx().get<WaterState>().reach_count() == 4);
    CHECK(a->world().state_hash() == b->world().state_hash());
    a->world().ctx().get<WaterState>().set_level_offset(2, 1.25F);
    CHECK(a->world().state_hash() != b->world().state_hash());
    b->world().ctx().get<WaterState>().set_level_offset(2, 1.25F);
    CHECK(a->world().state_hash() == b->world().state_hash());

    save::SaveOptions save_options;
    save_options.content = &db;
    const std::vector<std::byte> bytes = save::save_simulation_bytes(*a, save_options);
    auto loaded = make();
    save::LoadOptions load_options;
    load_options.content = &db;
    const save::LoadOutcome outcome = save::load_simulation_bytes(*loaded, bytes, load_options);
    CHECK(outcome.warnings.empty());
    CHECK(loaded->world().ctx().get<WaterState>() == a->world().ctx().get<WaterState>());
    CHECK(loaded->world().state_hash() == a->world().state_hash());
    // Offsets and nothing else: a save with no active terrain is far smaller than any height layer.
    CHECK(bytes.size() < 4096);
}

TEST_CASE("defines/terrain.json sets the channel profile and is checked", "[world][water][content]") {
    const auto load = [](const char* label, const std::string& text) {
        const TempDir dir(label);
        dir.write("mod/defines/terrain.json", text);
        auto db = std::make_unique<content::ContentDatabase>();
        const content::LoadResult result = db->load({dir / "mod"});
        std::string errors;
        for (const content::ContentIssue& issue : result.errors) {
            errors += issue.format() + "\n";
        }
        return std::make_pair(std::move(db), errors);
    };

    const auto [defaults, none] = load("terrain_defines_default", "{}");
    CHECK(none.empty());
    CHECK(defaults->terrain_defines().channel_edge_depth_fraction ==
          TerrainDefines{}.channel_edge_depth_fraction);
    CHECK(defaults->terrain_defines().bank_width_m == TerrainDefines{}.bank_width_m);

    const auto [set, ok] =
        load("terrain_defines_set", R"({ "channel_edge_depth_fraction": 0.5, "bank_width_m": 40 })");
    CHECK(ok.empty());
    CHECK(set->terrain_defines().channel_edge_depth_fraction == 0.5F);
    CHECK(set->terrain_defines().bank_width_m == 40.0F);

    CHECK(load("terrain_defines_bad_fraction", R"({ "channel_edge_depth_fraction": 1.5 })")
              .second.find("/channel_edge_depth_fraction: must be between 0 and 1") != std::string::npos);
    CHECK(load("terrain_defines_bad_bank", R"({ "bank_width_m": 0 })")
              .second.find("/bank_width_m: must be between 1 and 200") != std::string::npos);
}

// The file in fixtures/maps/ was written by the Python pipeline's own rivers writer
// (tools/terrain/scripts/make_cpp_fixture.py): a river of two arms that meet into a trunk.
TEST_CASE("a rivers.json written by the Python pipeline parses in C++", "[world][rivers]") {
    const std::filesystem::path file =
        std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "maps" / "python_rivers.json";
    std::ifstream stream(file);
    REQUIRE(stream.good());
    const RiverNetwork network = RiverNetwork::parse(json::parse(stream), "python_rivers.json");

    REQUIRE(network.reach_count() == 3);
    const auto a = network.find_reach("trunk", "a_1");
    const auto b = network.find_reach("trunk", "b_1");
    const auto trunk = network.find_reach("trunk", "trunk_1");
    REQUIRE((a && b && trunk));
    CHECK(network.reaches()[*a].downstream == std::vector<std::uint32_t>{*trunk});
    CHECK(network.reaches()[*b].downstream == std::vector<std::uint32_t>{*trunk});
    CHECK(network.reaches()[*trunk].downstream.empty());
    CHECK(network.reaches()[*trunk].order == 2);
    CHECK(network.reaches()[*trunk].discharge_m3s == 600.0);
    CHECK(network.reaches_of_river("Trunk").size() == 3);
    for (const Reach& reach : network.reaches()) {
        for (std::size_t i = 1; i < reach.vertices.size(); ++i) {
            CHECK(reach.vertices[i].level_m <= reach.vertices[i - 1].level_m); // water never rises downstream
        }
    }
    CHECK(network.reaches()[*a].vertices.front().width_m > 100.0F);
}
