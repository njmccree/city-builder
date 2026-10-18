#include "citysim/content/content_database.hpp"
#include "citysim/core/commands.hpp"
#include "citysim/core/isystem.hpp"
#include "citysim/core/job_system.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/core/tick_context.hpp"
#include "citysim/core/ui_notice.hpp"
#include "citysim/runner/render_snapshot.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/flood_map.hpp"
#include "citysim/world/river_network.hpp"
#include "citysim/world/terrain_publisher.hpp"
#include "citysim/world/water_commands.hpp"
#include "citysim/world/water_events.hpp"
#include "citysim/world/water_state.hpp"
#include "citysim/world/world_module.hpp"
#include "citysim/world/world_terrain.hpp"
#include "support/map_content.hpp"
#include "support/synthetic_map.hpp"
#include "support/temp_dir.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
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

// The main reach's vertex nearest a northing, a place the river certainly is.
const RiverVertex& main_vertex_near(const RiverNetwork& rivers, double z) {
    const auto& vertices = rivers.reaches().at(0).vertices;
    return *std::min_element(
        vertices.begin(), vertices.end(),
        [&](const RiverVertex& a, const RiverVertex& b) { return std::abs(a.z - z) < std::abs(b.z - z); });
}

// The 3 x 3 tiles around a point.
std::vector<TileCoord> tiles_around(WorldPos pos) {
    const TileCoord home = tile_of(pos);
    std::vector<TileCoord> tiles;
    for (std::int32_t dy = -1; dy <= 1; ++dy) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            tiles.push_back(TileCoord{home.tx + dx, home.ty + dy});
        }
    }
    std::sort(tiles.begin(), tiles.end());
    return tiles;
}

// Terrain on the full synthetic map with the tiles around the main river active.
struct Valley {
    std::shared_ptr<const LoadedMap> map = full_map();
    WorldTerrain terrain{map, 1};
    WaterState water{map->rivers().reach_count()};
    WorldPos spot;

    explicit Valley(std::size_t workers = 2) {
        const RiverVertex& vertex = main_vertex_near(map->rivers(), 45000.0);
        spot = WorldPos{vertex.x, vertex.z};
        core::JobSystem jobs{workers};
        terrain.activate_tiles(jobs, tiles_around(spot));
    }
};

// The marks of every tile with a flood, by tile.
std::map<TileCoord, FloodMap::Grid> marks_of(const FloodMap& flood) {
    std::map<TileCoord, FloodMap::Grid> out;
    for (const auto& [tile, grid] : flood.grids()) {
        out.emplace(tile, *grid);
    }
    return out;
}

bool is_subset(const std::map<TileCoord, FloodMap::Grid>& small,
               const std::map<TileCoord, FloodMap::Grid>& big) {
    for (const auto& [tile, marks] : small) {
        const auto it = big.find(tile);
        if (it == big.end()) {
            return false;
        }
        for (std::size_t i = 0; i < marks.size(); ++i) {
            if (marks[i] != 0 && it->second[i] == 0) {
                return false;
            }
        }
    }
    return true;
}

// Runs advance() until nothing is left; returns how many calls it took.
int settle(FloodMap& flood, const Valley& valley, std::size_t budget,
           std::vector<FloodMap::Finished>* done = nullptr) {
    int calls = 0;
    do {
        auto finished = flood.advance(valley.terrain, valley.water, budget);
        if (done != nullptr) {
            done->insert(done->end(), finished.begin(), finished.end());
        }
        ++calls;
    } while (!flood.settled() && calls < 100000);
    return calls;
}

bool inside_ring(const std::vector<WorldPos>& ring, double x, double z) {
    bool inside = false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        if ((ring[i].z > z) != (ring[j].z > z) &&
            x < (ring[j].x - ring[i].x) * (z - ring[i].z) / (ring[j].z - ring[i].z) + ring[i].x) {
            inside = !inside;
        }
    }
    return inside;
}

json valid_rivers() {
    return json::parse(R"({"format": 1, "rivers": [ { "id": "nile", "name": "Nile", "reaches": [
        { "id": "only", "order": 1, "discharge_m3s": 100.0, "downstream": [],
          "vertices": [ { "x": 0, "z": 0, "width_m": 40, "depth_m": 4, "water_level_m": 10 },
                        { "x": 0, "z": 1000, "width_m": 60, "depth_m": 6, "water_level_m": 8 } ] } ] } ] })");
}

std::string parse_error(json rivers) {
    try {
        RiverNetwork::parse(rivers, "rivers.json");
    } catch (const MapFormatError& error) {
        return error.what();
    }
    return {};
}

} // namespace

TEST_CASE("a reach's floodplain is read from rivers.json", "[world][flood][rivers]") {
    json rivers = valid_rivers();
    CHECK(RiverNetwork::parse(rivers, "rivers.json").reaches()[0].floodplain.empty());

    // GeoJSON-style closed ring: the repeated last point is dropped.
    rivers["rivers"][0]["reaches"][0]["floodplain"] =
        json::parse("[ [[-100,0],[100,0],[100,1000],[-100,1000],[-100,0]] ]");
    const RiverNetwork network = RiverNetwork::parse(rivers, "rivers.json");
    REQUIRE(network.reaches()[0].floodplain.size() == 1);
    CHECK(network.reaches()[0].floodplain[0].size() == 4);

    const auto expect = [&](const char* floodplain, const char* needle) {
        json bad = rivers;
        bad["rivers"][0]["reaches"][0]["floodplain"] = json::parse(floodplain);
        const std::string message = parse_error(bad);
        INFO(message);
        CHECK(message.find(needle) != std::string::npos);
    };
    expect("{}", "/rivers/0/reaches/0/floodplain: expected an array of rings");
    expect("[ 5 ]", "/rivers/0/reaches/0/floodplain/0: expected an array of [x, z] points");
    expect("[ [[0,0],[1,1]] ]", "/rivers/0/reaches/0/floodplain/0: a ring needs at least 3 different points");
    expect("[ [[0,0],[1,1],[2,\"a\"]] ]", "/rivers/0/reaches/0/floodplain/0/2: expected a point [x, z]");
    expect("[ [[0,0],[1,1],[2,2,2]] ]", "/rivers/0/reaches/0/floodplain/0/2: expected a point [x, z]");
}

TEST_CASE("segments are numbered per reach", "[world][flood][rivers][synthetic_map]") {
    const auto map = full_map();
    const RiverNetwork& rivers = map->rivers();
    std::uint32_t expected = 0;
    for (std::uint32_t r = 0; r < rivers.reach_count(); ++r) {
        CHECK(rivers.first_segment(r) == expected);
        expected += static_cast<std::uint32_t>(rivers.reaches()[r].vertices.size()) - 1;
        CHECK(!rivers.reaches()[r].floodplain.empty()); // the synthetic map has them
    }
}

TEST_CASE("a rising river floods the low land beside it, inside its floodplain",
          "[world][flood][synthetic_map]") {
    Valley valley;
    valley.water.set_level_offset(0, 20.0F);
    FloodMap flood;
    std::vector<FloodMap::Finished> done;
    settle(flood, valley, 100000000, &done);

    REQUIRE(done.size() == 1);
    CHECK(done[0].reach == 0);
    CHECK(done[0].flooded_area_m2 > 1.0e6);
    CHECK(flood.flooded_area_m2(0) == done[0].flooded_area_m2);
    CHECK(flood.flooded_area_m2(1) == 0.0);
    REQUIRE(!flood.grids().empty());

    const auto& rings = valley.map->rivers().reaches()[0].floodplain;
    std::size_t marked = 0;
    bool all_inside = true;
    bool all_binary = true;
    for (const auto& [tile, grid] : flood.grids()) {
        REQUIRE(valley.terrain.is_active(tile));
        CHECK(grid->size() == CityTile::k_sample_count);
        const WorldPos origin = tile_origin(tile);
        for (std::size_t i = 0; i < grid->size(); ++i) {
            const std::uint8_t mark = (*grid)[i];
            all_binary = all_binary && mark <= 1;
            if (mark == 0) {
                continue;
            }
            ++marked;
            const double x = origin.x + 4.0 * static_cast<double>(i % k_city_tile_samples);
            const double z = origin.z + 4.0 * static_cast<double>(i / k_city_tile_samples);
            all_inside = all_inside && std::any_of(rings.begin(), rings.end(),
                                                   [&](const auto& ring) { return inside_ring(ring, x, z); });
        }
    }
    CHECK(all_binary);
    CHECK(all_inside); // never outside the floodplain
    CHECK(marked > 0);
    // Shared edge samples are marked in both tiles, so the area is a bit less than all the marks together.
    CHECK(done[0].flooded_area_m2 <= 16.0 * static_cast<double>(marked));
    CHECK(done[0].flooded_area_m2 >= 0.9 * 16.0 * static_cast<double>(marked));
    // flooded_at() reads the same marks.
    {
        const auto& [tile, grid] = *flood.grids().begin();
        const std::size_t first =
            static_cast<std::size_t>(std::find(grid->begin(), grid->end(), std::uint8_t{1}) - grid->begin());
        const WorldPos origin = tile_origin(tile);
        CHECK(flood.flooded_at(
            WorldPos{origin.x + 4.0 * static_cast<double>(first % k_city_tile_samples) + 0.5,
                     origin.z + 4.0 * static_cast<double>(first / k_city_tile_samples) + 0.5}));
    }
    CHECK(!flood.flooded_at(WorldPos{0.0, 0.0}));
}

TEST_CASE("a river at or below its base level floods nothing", "[world][flood][synthetic_map]") {
    Valley valley;
    FloodMap flood;
    CHECK(flood.advance(valley.terrain, valley.water, 100000000).empty());
    CHECK(flood.settled());
    CHECK(flood.grids().empty());
    valley.water.set_level_offset(0, -3.0F);
    CHECK(flood.advance(valley.terrain, valley.water, 100000000).empty());
    CHECK(flood.grids().empty());
}

TEST_CASE("a higher river floods at least what a lower one does", "[world][flood][synthetic_map]") {
    Valley valley;
    valley.water.set_level_offset(0, 0.5F);
    FloodMap low;
    settle(low, valley, 100000000);
    valley.water.set_level_offset(0, 2.0F);
    FloodMap high;
    settle(high, valley, 100000000);
    CHECK(low.flooded_area_m2(0) > 0.0);
    CHECK(high.flooded_area_m2(0) > low.flooded_area_m2(0));
    CHECK(is_subset(marks_of(low), marks_of(high)));
}

TEST_CASE("time slicing gives the same extent as an unbounded fill", "[world][flood][synthetic_map]") {
    Valley valley;
    valley.water.set_level_offset(0, 1.0F);
    valley.water.set_level_offset(2, 1.0F);
    FloodMap whole;
    std::vector<FloodMap::Finished> whole_done;
    CHECK(settle(whole, valley, 100000000, &whole_done) == 1);

    FloodMap sliced;
    std::vector<FloodMap::Finished> sliced_done;
    const int calls = settle(sliced, valley, 20000, &sliced_done);
    CHECK(calls > 10); // it really was spread out

    CHECK(marks_of(sliced) == marks_of(whole));
    REQUIRE(sliced_done.size() == whole_done.size());
    for (std::size_t i = 0; i < whole_done.size(); ++i) {
        CHECK(sliced_done[i].reach == whole_done[i].reach);
        CHECK(sliced_done[i].flooded_area_m2 == whole_done[i].flooded_area_m2);
    }
}

TEST_CASE("the extent is the same with 1 and 8 workers", "[world][flood][synthetic_map]") {
    Valley one(1);
    Valley eight(8);
    one.water.set_level_offset(0, 0.7F);
    eight.water.set_level_offset(0, 0.7F);
    FloodMap a;
    FloodMap b;
    settle(a, one, 50000);
    settle(b, eight, 50000);
    CHECK(a.flooded_area_m2(0) > 0.0);
    CHECK(marks_of(a) == marks_of(b));
}

TEST_CASE("a falling river leaves its channel again, and a new level mid-fill starts over",
          "[world][flood][synthetic_map]") {
    Valley valley;
    FloodMap flood;
    valley.water.set_level_offset(0, 0.7F);
    settle(flood, valley, 100000000);
    REQUIRE(flood.flooded_area_m2(0) > 0.0);

    valley.water.set_level_offset(0, 0.0F);
    const auto receded = flood.advance(valley.terrain, valley.water, 100000000);
    REQUIRE(receded.size() == 1);
    CHECK(receded[0].reach == 0);
    CHECK(receded[0].flooded_area_m2 == 0.0);
    CHECK(flood.grids().empty());
    CHECK(flood.settled());

    // Start a fill, change the level before it ends: the result is that of the last level.
    valley.water.set_level_offset(0, 0.7F);
    flood.advance(valley.terrain, valley.water, 5000);
    CHECK(!flood.settled());
    valley.water.set_level_offset(0, 0.5F);
    settle(flood, valley, 100000000);

    FloodMap direct;
    settle(direct, valley, 100000000);
    CHECK(marks_of(flood) == marks_of(direct));
}

TEST_CASE("a tile's marks keep their pointer until they change", "[world][flood][synthetic_map]") {
    Valley valley;
    FloodMap flood;
    valley.water.set_level_offset(0, 5.0F);
    settle(flood, valley, 100000000);
    REQUIRE(!flood.grids().empty());
    const auto before = flood.grids();
    const double area = flood.flooded_area_m2(0);

    flood.advance(valley.terrain, valley.water, 100000000);
    CHECK(flood.grids() == before); // nothing to do

    // Another level that floods the same land (the whole floodplain): same marks, same pointers.
    valley.water.set_level_offset(0, 20.0F);
    settle(flood, valley, 100000000);
    REQUIRE(flood.flooded_area_m2(0) == area);
    CHECK(flood.grids() == before);
}

TEST_CASE("the flood follows the active tiles", "[world][flood][synthetic_map]") {
    Valley valley;
    FloodMap flood;
    valley.water.set_level_offset(0, 0.7F);
    settle(flood, valley, 100000000);
    REQUIRE(!flood.grids().empty());
    const TileCoord gone = flood.grids().begin()->first;
    const auto kept = marks_of(flood);

    valley.terrain.deactivate_tiles(std::vector<TileCoord>{gone});
    flood.advance(valley.terrain, valley.water, 100000000);
    CHECK(flood.grid(gone) == nullptr); // dropped at once
    settle(flood, valley, 100000000);
    CHECK(flood.grid(gone) == nullptr);
    CHECK(is_subset(marks_of(flood), kept)); // refilled without it: what is left is part of the old extent
    CHECK(flood.settled());
}

namespace {

// Remembers the WaterChanged events it can read, so a test can see what the flood system emitted.
class EventRecorder final : public core::ISystem {
  public:
    explicit EventRecorder(std::vector<WaterChanged>* out) : out_(out) {}
    std::string_view name() const override { return "test.water_events"; }
    void update(core::TickContext& context) override {
        for (const core::Event& event : context.events.readable()) {
            if (const auto* changed = std::get_if<WaterChanged>(&event)) {
                out_->push_back(*changed);
            }
        }
    }

  private:
    std::vector<WaterChanged>* out_;
};

// A game on the full-size synthetic map with one city over the river.
struct Game {
    TempDir dir;
    MapContent map;
    std::unique_ptr<content::ContentDatabase> db = std::make_unique<content::ContentDatabase>();
    std::shared_ptr<const LoadedMap> loaded;
    std::unique_ptr<core::Simulation> sim;
    std::vector<WaterChanged> events;
    std::uint64_t sequence = 1;

    explicit Game(const char* label) : dir(label), map(dir, citysim::test::synthetic_map_dir()) {
        REQUIRE(db->load({map.mod_dir()}).ok());
        loaded = open_map(*db, MapContent::map_id());
        sim = make_simulation();
        const RiverVertex& vertex = main_vertex_near(loaded->rivers(), 45000.0);
        const TileCoord home = tile_of(WorldPos{vertex.x, vertex.z});
        // A 4 x 4 city that has the river's tile in it, kept to the detail tiles (16..30, 18..42).
        const std::int32_t tx = std::clamp(home.tx - 1, 16, 27);
        const std::int32_t ty = std::clamp(home.ty - 1, 18, 39);
        const auto notices = run(core::Command{FoundCity{tx, ty, "Test"}});
        REQUIRE(std::any_of(notices.begin(), notices.end(),
                            [](const core::UiNotice& notice) { return notice.tag == "city_founded"; }));
    }

    std::unique_ptr<core::Simulation> make_simulation() {
        auto simulation = std::make_unique<core::Simulation>(7, 2);
        install(*simulation, *db, WorldConfig{MapContent::map_id(), loaded});
        simulation->add_system(core::Phase::Publish, std::make_unique<EventRecorder>(&events));
        return simulation;
    }

    std::vector<core::UiNotice> run(core::Simulation& simulation, core::Command command) {
        simulation.enqueue({core::SequencedCommand{sequence++, simulation.tick(), std::move(command)}});
        simulation.step();
        return simulation.world().ctx().get<core::UiOutbox>().take();
    }
    std::vector<core::UiNotice> run(core::Command command) { return run(*sim, std::move(command)); }

    // Ticks until the flood has settled (and a few more), at most `limit` ticks.
    int settle(core::Simulation& simulation, int limit = 400) {
        int ticks = 0;
        while (ticks < limit && !(simulation.world().ctx().get<FloodMap>().settled() && ticks > 2)) {
            simulation.step();
            ++ticks;
        }
        return ticks;
    }
    int settle(int limit = 400) { return settle(*sim, limit); }
    FloodMap& flood() { return sim->world().ctx().get<FloodMap>(); }
};

} // namespace

TEST_CASE("SetRiverLevel floods over some ticks and WaterChanged says when it is done",
          "[world][flood][synthetic_map]") {
    Game game("flood_command");
    game.settle(8);
    REQUIRE(game.events.empty());
    const std::uint64_t before = game.sim->world().state_hash();

    CHECK(game.run(core::Command{SetRiverLevel{0, 2.0F}}).empty());
    CHECK(game.sim->world().ctx().get<WaterState>().level_offset(0) == 2.0F);
    CHECK(game.sim->world().state_hash() != before); // the level is state
    CHECK(game.events.empty());                      // not spread yet: a 36-tile fill takes several ticks
    const int ticks = game.settle();
    CHECK(ticks > 2);
    REQUIRE(game.events.size() == 1);
    CHECK(game.events[0].reach == 0);
    CHECK(game.events[0].city == 1);
    CHECK(game.events[0].flooded_area_m2 > 1.0e6);
    CHECK(game.events[0].flooded_area_m2 == game.flood().flooded_area_m2(0));

    // The flood itself is not state: the hash is the same with the water spread as just after the command.
    const std::uint64_t flooded_hash = game.sim->world().state_hash();
    Game other("flood_command_fresh");
    other.run(core::Command{SetRiverLevel{0, 2.0F}});
    CHECK(other.sim->world().state_hash() == flooded_hash);

    // Back to the channel.
    game.run(core::Command{SetRiverLevel{0, 0.0F}});
    game.settle();
    REQUIRE(game.events.size() == 2);
    CHECK(game.events[1].flooded_area_m2 == 0.0);
    CHECK(game.flood().grids().empty());
}

TEST_CASE("SetRiverLevel refuses a reach that does not exist and a level that is not a number",
          "[world][flood][synthetic_map]") {
    Game game("flood_refused");
    const auto has = [](const std::vector<core::UiNotice>& notices, const char* key) {
        return std::any_of(notices.begin(), notices.end(), [&](const core::UiNotice& notice) {
            return notice.tag == "river_level_failed" && notice.key == key;
        });
    };
    CHECK(has(game.run(core::Command{SetRiverLevel{99, 1.0F}}), "water.river_level.error.unknown_reach"));
    CHECK(has(game.run(core::Command{SetRiverLevel{0, std::numeric_limits<float>::quiet_NaN()}}),
              "water.river_level.error.offset"));
    CHECK(has(game.run(core::Command{SetRiverLevel{0, std::numeric_limits<float>::infinity()}}),
              "water.river_level.error.offset"));
    CHECK(game.sim->world().ctx().get<WaterState>().level_offset(0) == 0.0F);
}

TEST_CASE("a loaded game converges to the same flood", "[world][flood][synthetic_map]") {
    Game game("flood_save_load");
    game.run(core::Command{SetRiverLevel{0, 0.7F}});
    game.run(core::Command{SetRiverLevel{2, 0.7F}});
    game.settle();
    REQUIRE(!game.flood().grids().empty());

    save::SaveOptions save_options;
    save_options.content = game.db.get();
    const std::vector<std::byte> bytes = save::save_simulation_bytes(*game.sim, save_options);

    auto loaded = game.make_simulation();
    save::LoadOptions load_options;
    load_options.content = game.db.get();
    const save::LoadOutcome outcome = save::load_simulation_bytes(*loaded, bytes, load_options);
    CHECK(outcome.warnings.empty());
    CHECK(loaded->world().state_hash() == game.sim->world().state_hash());
    CHECK(loaded->world().ctx().get<FloodMap>().grids().empty()); // derived: nothing saved
    game.settle(*loaded);

    const FloodMap& expected = game.flood();
    const FloodMap& got = loaded->world().ctx().get<FloodMap>();
    CHECK(marks_of(got) == marks_of(expected));
    CHECK(got.flooded_area_m2(0) == expected.flooded_area_m2(0));
    CHECK(got.flooded_area_m2(2) == expected.flooded_area_m2(2));
}

TEST_CASE("flooded tiles are published as shared grids that change only with the flood",
          "[world][flood][synthetic_map]") {
    Game game("flood_publish");
    runner::RenderSnapshot none;
    publish_flood_extents(*game.sim, none);
    CHECK(none.grids.empty());

    game.run(core::Command{SetRiverLevel{0, 0.7F}});
    game.settle();
    runner::RenderSnapshot first;
    publish_flood_extents(*game.sim, first);
    REQUIRE(!first.grids.empty());
    REQUIRE(first.grids.size() == game.flood().grids().size());
    for (const runner::SnapshotGrid& grid : first.grids) {
        CHECK(grid.name.rfind("water.flood.", 0) == 0);
        CHECK(grid.width == 513);
        CHECK(grid.height == 513);
        CHECK(grid.cell_size == 4.0F);
        REQUIRE(grid.cells != nullptr);
        CHECK(grid.cells->size() == 513u * 513u);
    }

    game.sim->step();
    runner::RenderSnapshot second;
    publish_flood_extents(*game.sim, second);
    REQUIRE(second.grids.size() == first.grids.size());
    for (std::size_t i = 0; i < first.grids.size(); ++i) {
        CHECK(second.grids[i].name == first.grids[i].name);
        CHECK(second.grids[i].cells == first.grids[i].cells); // same pointer: nothing changed
    }
    CHECK(first.find_grid(first.grids[0].name) == &first.grids[0]);
}

// Hidden by default: run with `citysim_tests "[.bench]"`. One tick's share of the flood fill (the default
// budget of 250,000 samples) must take under 20 ms in a Release build (a tick at 1x is 50 ms), on a flood
// that fills the whole floodplain of the 3 x 3 active tiles.
TEST_CASE("one tick of flood fill is fast", "[.bench][synthetic_map]") {
    Valley valley;
    valley.water.set_level_offset(0, 20.0F);
    FloodMap flood;
    double worst_ms = 0.0;
    double total_ms = 0.0;
    int calls = 0;
    do {
        const auto start = std::chrono::steady_clock::now();
        flood.advance(valley.terrain, valley.water, TerrainDefines{}.flood_samples_per_tick);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        worst_ms = std::max(worst_ms, ms);
        total_ms += ms;
        ++calls;
    } while (!flood.settled());
    std::printf("flood fill: %d calls, worst %.2f ms, mean %.2f ms\n", calls, worst_ms, total_ms / calls);
#ifdef NDEBUG
    CHECK(worst_ms < 20.0);
#endif
}
