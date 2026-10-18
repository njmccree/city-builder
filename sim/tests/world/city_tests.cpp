#include "citysim/content/content_database.hpp"
#include "citysim/core/commands.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/core/ui_notice.hpp"
#include "citysim/runner/game_session.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/world/city_module.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/terrain_publisher.hpp"
#include "citysim/world/world_module.hpp"
#include "support/map_content.hpp"
#include "support/synthetic_map.hpp"
#include "support/temp_dir.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace citysim;
using namespace citysim::world;
using citysim::test::MapContent;
using citysim::test::TempDir;

namespace {

// A game on the full-size synthetic map (96 km). Its detail tiles are (16..30, 18..42), the coast is about
// 82-88% of the way north (tile row 38 and up), so a 4 x 4 city fits at origins (16..27, 18..39) and is land
// up to ~row 34.
struct Game {
    TempDir dir;
    MapContent map;
    std::unique_ptr<content::ContentDatabase> db = std::make_unique<content::ContentDatabase>();
    std::shared_ptr<const LoadedMap> loaded;
    std::unique_ptr<core::Simulation> sim;
    std::uint64_t sequence = 1;

    explicit Game(const char* label, CityDefines defines = {}, std::size_t workers = 2)
        : dir(label), map(dir, citysim::test::synthetic_map_dir()) {
        REQUIRE(db->load({map.mod_dir()}).ok());
        loaded = open_map(*db, MapContent::map_id());
        sim = make_simulation(defines, workers);
    }

    std::unique_ptr<core::Simulation> make_simulation(CityDefines defines, std::size_t workers = 2) const {
        auto simulation = std::make_unique<core::Simulation>(7, workers);
        WorldConfig config{MapContent::map_id(), loaded};
        config.cities = defines;
        install(*simulation, *db, config);
        return simulation;
    }

    // Runs one tick with the command; returns the notices it produced.
    std::vector<core::UiNotice> run(core::Simulation& simulation, core::Command command) {
        simulation.enqueue({core::SequencedCommand{sequence++, simulation.tick(), std::move(command)}});
        simulation.step();
        return simulation.world().ctx().get<core::UiOutbox>().take();
    }
    std::vector<core::UiNotice> run(core::Command command) { return run(*sim, std::move(command)); }

    CityRegistry& cities() { return sim->world().ctx().get<CityRegistry>(); }
    ActiveCity& active() { return sim->world().ctx().get<ActiveCity>(); }
    WorldTerrain& terrain() { return sim->world().ctx().get<WorldTerrain>(); }
    std::uint64_t hash() const { return sim->world().state_hash(); }
};

FoundCity found(std::int32_t tx, std::int32_t ty, std::string name = {}) {
    return FoundCity{tx, ty, std::move(name)};
}

bool has_notice(const std::vector<core::UiNotice>& notices, const std::string& tag,
                const std::string& key = {}) {
    return std::any_of(notices.begin(), notices.end(), [&](const core::UiNotice& notice) {
        return notice.tag == tag && (key.empty() || notice.key == key);
    });
}

} // namespace

TEST_CASE("founding a city adds it and makes the first one active", "[world][cities][synthetic_map]") {
    Game game("cities_found");
    const auto notices = game.run(found(20, 22, "Memphis"));
    CHECK(has_notice(notices, "city_founded"));
    CHECK(has_notice(notices, "active_city_changed"));

    REQUIRE(game.cities().size() == 1);
    const CityInfo& city = game.cities().cities()[0];
    CHECK(city.id == 1);
    CHECK(city.name == "Memphis");
    CHECK(city.first_tile == TileCoord{20, 22});
    CHECK(city.mode == SimMode::Agent);
    CHECK(game.active().city_id == 1);
    // 4 x 4 tiles plus a ring of 1: 6 x 6.
    CHECK(game.terrain().active_tiles().size() == 36);
    CHECK(game.terrain().is_active(TileCoord{19, 21}));
    CHECK(game.terrain().is_active(TileCoord{24, 26}));
    CHECK(!game.terrain().is_active(TileCoord{25, 22}));

    // A second city is founded but not made active; an empty name becomes "City N".
    const auto second = game.run(found(24, 22));
    CHECK(has_notice(second, "city_founded"));
    CHECK(!has_notice(second, "active_city_changed"));
    REQUIRE(game.cities().size() == 2);
    CHECK(game.cities().cities()[1].name == "City 2");
    CHECK(game.cities().cities()[1].mode == SimMode::Aggregate);
    CHECK(game.active().city_id == 1);
}

TEST_CASE("a site that cannot be settled is refused with a reason and nothing changes",
          "[world][cities][synthetic_map]") {
    Game game("cities_refused");
    game.run(found(20, 22));
    const std::uint64_t before = game.hash();
    const std::size_t tiles_before = game.terrain().active_tiles().size();

    struct Case {
        FoundCity command;
        const char* key;
    };
    const Case cases[] = {
        {found(-1, 22), "city.found.error.out_of_bounds"}, // west of the map
        {found(44, 22), "city.found.error.out_of_bounds"}, // reaches past the last column (46)
        {found(22, 45), "city.found.error.out_of_bounds"}, // reaches past the last row
        {found(14, 22), "city.found.error.no_detail"},     // tiles 14 and 15 have no detail data
        {found(28, 22), "city.found.error.no_detail"},     // tile 31 has none
        {found(20, 40), "city.found.error.no_detail"},     // row 43 has none
        {found(21, 23), "city.found.error.overlap"},       // inside the first city
        {found(20, 22), "city.found.error.overlap"},       // exactly the first city
        {found(17, 20), "city.found.error.overlap"},       // the corner overlaps by one tile
        {found(20, 39), "city.found.error.not_land"},      // the sea, up on the coast
        {FoundCity{24, 22, std::string(65, 'x')}, "city.found.error.name"},
    };
    for (const Case& test : cases) {
        INFO("at (" << test.command.origin_tx << ", " << test.command.origin_ty << ")");
        const auto notices = game.run(test.command);
        CHECK(has_notice(notices, "city_found_failed", test.key));
        CHECK(!has_notice(notices, "city_founded"));
        CHECK(game.cities().size() == 1);
        CHECK(game.hash() == before);
        CHECK(game.terrain().active_tiles().size() == tiles_before);
    }
}

TEST_CASE("touching cities are allowed", "[world][cities][synthetic_map]") {
    Game game("cities_touching");
    game.run(found(20, 22));
    CHECK(has_notice(game.run(found(24, 22)), "city_founded")); // east, sharing an edge
    CHECK(has_notice(game.run(found(20, 26)), "city_founded")); // north
    CHECK(has_notice(game.run(found(16, 26)), "city_founded")); // north-west
    CHECK(has_notice(game.run(found(24, 26)), "city_founded")); // touching only at a corner of two areas
    CHECK(game.cities().size() == 5);
}

TEST_CASE("the number of cities is limited by a define", "[world][cities][synthetic_map]") {
    CityDefines defines;
    defines.max_player_cities = 2;
    Game game("cities_limit", defines);
    game.run(found(16, 18));
    game.run(found(20, 18));
    const auto notices = game.run(found(24, 18));
    CHECK(has_notice(notices, "city_found_failed", "city.found.error.limit"));
    CHECK(game.cities().size() == 2);

    // And the built-in default is ten.
    CHECK(CityDefines{}.max_player_cities == 10);
    Game ten("cities_ten");
    int count = 0;
    for (const TileCoord origin : {TileCoord{16, 18},
                                   {20, 18},
                                   {24, 18},
                                   {16, 22},
                                   {20, 22},
                                   {24, 22},
                                   {16, 26},
                                   {20, 26},
                                   {24, 26},
                                   {16, 30}}) {
        CHECK(has_notice(ten.run(found(origin.tx, origin.ty)), "city_founded"));
        ++count;
    }
    CHECK(count == 10);
    CHECK(has_notice(ten.run(found(20, 30)), "city_found_failed", "city.found.error.limit"));
}

TEST_CASE("the land needed is a define", "[world][cities][synthetic_map]") {
    // Rows 39..42 are mostly sea: the strict default refuses a site that a looser define accepts.
    Game strict("cities_land_strict");
    CityDefines loose;
    loose.min_land_fraction = 0.0F;
    Game relaxed("cities_land_loose", loose);
    CHECK(has_notice(strict.run(found(20, 39)), "city_found_failed", "city.found.error.not_land"));
    CHECK(has_notice(relaxed.run(found(20, 39)), "city_founded"));
    // The reason is available without founding anything, for a UI that colors the site while it is chosen.
    CHECK(found_city_problem(strict.terrain(), strict.cities(), CityDefines{}, TileCoord{20, 39}, 0) ==
          std::string_view("city.found.error.not_land"));
    CHECK(found_city_problem(strict.terrain(), strict.cities(), CityDefines{}, TileCoord{20, 22}, 0).empty());
}

TEST_CASE("setting the active city swaps the active tiles", "[world][cities][synthetic_map]") {
    Game game("cities_activate");
    game.run(found(16, 18)); // 1, active
    game.run(found(24, 26)); // 2, far away
    game.run(found(20, 18)); // 3, touches city 1: their rings share tiles

    const auto before = game.terrain().tile(TileCoord{19, 18}); // in city 1's ring and city 3's area
    REQUIRE(before != nullptr);
    CHECK(game.terrain().active_tiles().size() == 36);

    const auto notices = game.run(SetActiveCity{3});
    CHECK(has_notice(notices, "active_city_changed"));
    CHECK(game.active().city_id == 3);
    CHECK(game.cities().find(1)->mode == SimMode::Aggregate);
    CHECK(game.cities().find(3)->mode == SimMode::Agent);
    CHECK(game.cities().find(2)->mode == SimMode::Aggregate);
    // City 3's 6 x 6 tiles are active, city 1's other tiles are gone, the shared ones kept their tiles.
    const std::vector<TileCoord> expected = CityArea{TileCoord{20, 18}}.tiles(1);
    CHECK(game.terrain().active_tiles() == expected);
    CHECK(!game.terrain().is_active(TileCoord{16, 18}));
    CHECK(game.terrain().tile(TileCoord{19, 18}) == before);

    // Far away: nothing is shared.
    game.run(SetActiveCity{2});
    CHECK(game.terrain().active_tiles() == CityArea{TileCoord{24, 26}}.tiles(1));
    CHECK(game.cities().find(3)->mode == SimMode::Aggregate);

    // Same city again, and a city that does not exist: nothing changes.
    const std::uint64_t hash = game.hash();
    CHECK(game.run(SetActiveCity{2}).empty());
    CHECK(has_notice(game.run(SetActiveCity{99}), "active_city_failed", "city.active.error.unknown"));
    CHECK(has_notice(game.run(SetActiveCity{0}), "active_city_failed", "city.active.error.unknown"));
    CHECK(game.hash() == hash);
}

TEST_CASE("edits of the previous city survive an active-city swap", "[world][cities][synthetic_map]") {
    Game game("cities_edits");
    game.run(found(16, 18));
    game.run(found(24, 26));
    game.terrain().apply_delta(TileCoord{17, 19}, SampleRect{10, 10, 20, 20},
                               [](std::uint32_t, std::uint32_t, float height) { return height + 3.0F; });
    const float edited = game.terrain().tile(TileCoord{17, 19})->sample(15, 15);
    game.run(SetActiveCity{2});
    CHECK(!game.terrain().is_active(TileCoord{17, 19}));
    game.run(SetActiveCity{1});
    CHECK(game.terrain().tile(TileCoord{17, 19})->sample(15, 15) == edited);
}

TEST_CASE("neighbors are the cities that touch or are within one tile", "[world][cities][synthetic_map]") {
    Game game("cities_neighbors");
    game.run(found(16, 18)); // 1
    game.run(found(20, 18)); // 2: touches 1 on the east
    game.run(found(25, 18)); // 3: one tile of gap from 2, five from 1
    game.run(found(16, 24)); // 4: two tiles of gap north of 1: not a neighbor of 1
    game.run(found(20, 23)); // 5: touches 1's corner side, one tile north of 2, diagonal to 3, touches 4
    REQUIRE(game.cities().size() == 5);
    const CityRegistry& cities = game.cities();
    using Ids = std::vector<std::uint32_t>;
    CHECK(cities.neighbors(1) == Ids{2, 5});
    CHECK(cities.neighbors(2) == Ids{1, 3, 5});
    CHECK(cities.neighbors(3) == Ids{2, 5});
    CHECK(cities.neighbors(4) == Ids{5});
    CHECK(cities.neighbors(5) == Ids{1, 2, 3, 4});
    CHECK(cities.neighbors(99).empty());
    // Symmetric.
    for (const CityInfo& a : cities.cities()) {
        for (const std::uint32_t b : cities.neighbors(a.id)) {
            const auto back = cities.neighbors(b);
            CHECK(std::find(back.begin(), back.end(), a.id) != back.end());
        }
    }
}

TEST_CASE("the cities are published for the front end", "[world][cities][synthetic_map]") {
    Game game("cities_publish");
    game.run(found(16, 18, "Thebes"));
    game.run(found(24, 26, "Giza"));
    game.run(SetActiveCity{2});
    runner::RenderSnapshot snapshot;
    publish_cities(*game.sim, snapshot);
    REQUIRE(snapshot.cities.size() == 2);
    CHECK(snapshot.active_city_id == 2);
    CHECK(snapshot.cities[0].id == 1);
    CHECK(snapshot.cities[0].name == "Thebes");
    CHECK(snapshot.cities[0].first_tile == TileCoord{16, 18});
    CHECK(!snapshot.cities[0].active);
    CHECK(snapshot.cities[1].active);
    // No world module: nothing published.
    core::Simulation bare(1, 1);
    runner::RenderSnapshot empty;
    publish_cities(bare, empty);
    CHECK(empty.cities.empty());
    CHECK(empty.active_city_id == 0);
}

TEST_CASE("cities save and load, and the games go on the same", "[world][cities][synthetic_map]") {
    Game game("cities_save_load");
    game.run(found(16, 18, "Thebes"));
    game.run(found(24, 26, "Giza"));
    game.run(SetActiveCity{2});
    game.terrain().apply_delta(TileCoord{25, 27}, SampleRect{0, 0, 5, 5},
                               [](std::uint32_t, std::uint32_t, float height) { return height - 2.0F; });

    save::SaveOptions save_options;
    save_options.content = game.db.get();
    const std::vector<std::byte> bytes = save::save_simulation_bytes(*game.sim, save_options);

    auto loaded = game.make_simulation(CityDefines{});
    save::LoadOptions load_options;
    load_options.content = game.db.get();
    const save::LoadOutcome outcome = save::load_simulation_bytes(*loaded, bytes, load_options);
    CHECK(outcome.warnings.empty());
    CHECK(loaded->world().state_hash() == game.hash());
    CHECK(loaded->world().ctx().get<CityRegistry>() == game.cities());
    CHECK(loaded->world().ctx().get<ActiveCity>() == game.active());
    CHECK(loaded->world().ctx().get<WorldTerrain>().active_tiles() == game.terrain().active_tiles());
    CHECK(loaded->world().ctx().get<WorldTerrain>().deltas() == game.terrain().deltas());

    // The same commands afterwards give the same states.
    for (const core::Command& command : {core::Command{found(20, 22, "Edfu")},
                                         core::Command{SetActiveCity{3}}, core::Command{SetActiveCity{1}}}) {
        game.run(*game.sim, command);
        game.run(*loaded, command);
        CHECK(loaded->world().state_hash() == game.hash());
    }
    CHECK(game.cities().size() == 3);
}

TEST_CASE("the same commands give the same state for any number of workers",
          "[world][cities][synthetic_map]") {
    Game one("cities_workers_1", CityDefines{}, 1);
    Game eight("cities_workers_8", CityDefines{}, 8);
    for (const core::Command& command :
         {core::Command{found(16, 18)}, core::Command{found(20, 18, "Two")}, core::Command{found(20, 18)},
          core::Command{SetActiveCity{2}}, core::Command{found(24, 30)}, core::Command{SetActiveCity{3}}}) {
        one.run(command);
        eight.run(command);
        CHECK(one.hash() == eight.hash());
    }
    CHECK(one.cities() == eight.cities());
}

TEST_CASE("a saved game with a wrong city is refused", "[world][cities][synthetic_map]") {
    Game game("cities_bad_save");
    game.run(found(16, 18));
    // Two cities on the same ground cannot be made through commands; a registry built by hand shows the
    // loader would refuse one anyway.
    CityRegistry registry;
    registry.restore({CityInfo{1, "A", TileCoord{16, 18}, {}, SimMode::Agent},
                      CityInfo{2, "B", TileCoord{17, 19}, {}, SimMode::Aggregate}},
                     3);
    CHECK(registry.overlaps(CityArea{TileCoord{18, 20}}));
    CHECK(registry.neighbors(1) == std::vector<std::uint32_t>{2});
}

TEST_CASE("cities in a game session: notices arrive once and the snapshot lists the cities",
          "[world][cities][session]") {
    TempDir dir("cities_session");
    const MapContent map(dir); // a 16 km map: one city fits (tiles 2..5, 3..6), at origin (2, 3)
    runner::FakeClock clock;
    runner::NewGameRequest request;
    request.seed = 3;
    const std::filesystem::path content = CITYSIM_GAME_CONTENT_DIR;
    request.content_roots = {content, map.root()};
    request.enabled_mods = {"base", "test"};
    request.era_id = "base:era.ancient";
    request.map_id = MapContent::map_id();
    request.clock = &clock;
    request.worker_count = 1;
    runner::NewGameOutcome outcome = runner::start_new_game(request);
    REQUIRE(outcome.ok());
    auto& session = *outcome.session;
    REQUIRE(session.runner().wait_until_idle());

    session.runner().submit(found(0, 0)); // nothing there: no detail data
    session.runner().submit(found(2, 3, "Only"));
    clock.set_time(1.0 / 20.0); // one tick applies both commands
    REQUIRE(
        session.runner().wait_until_idle(120.0)); // founding makes 36 terrain tiles: slow under a sanitizer
    runner::RenderSnapshot snapshot = session.latest_snapshot();
    std::vector<std::string> tags;
    for (const runner::UiEvent& event : snapshot.ui_events) {
        if (event.kind == runner::UiEventKind::Notice) {
            tags.push_back(event.tag + ":" + event.text);
        }
    }
    CHECK(tags == std::vector<std::string>{"city_found_failed:city.found.error.no_detail",
                                           "city_founded:", "active_city_changed:"});
    REQUIRE(snapshot.cities.size() == 1);
    CHECK(snapshot.cities[0].name == "Only");
    CHECK(snapshot.active_city_id == 1);
    CHECK(snapshot.terrain_tiles.size() == 36);

    // Each notice is handed over once.
    const runner::RenderSnapshot again = session.latest_snapshot();
    CHECK(std::none_of(again.ui_events.begin(), again.ui_events.end(), [](const runner::UiEvent& event) {
        return event.kind == runner::UiEventKind::Notice;
    }));
}
