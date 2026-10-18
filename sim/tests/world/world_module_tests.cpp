#include "citysim/content/content_database.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/runner/game_session.hpp"
#include "citysim/save/save_format_error.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/world/terrain_publisher.hpp"
#include "citysim/world/world_module.hpp"
#include "support/map_content.hpp"
#include "support/temp_dir.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace citysim;
using namespace citysim::world;
using citysim::test::MapContent;
using citysim::test::TempDir;

namespace {

// The small map's 4 x 4 detail tiles are exactly one city area; its ring lies on the map too (8 x 8 tiles).
const CityArea k_city{TileCoord{2, 3}};

std::unique_ptr<content::ContentDatabase> load_content(const MapContent& map) {
    auto db = std::make_unique<content::ContentDatabase>();
    const content::LoadResult result = db->load({map.mod_dir()});
    REQUIRE(result.ok());
    return db;
}

WorldConfig config(std::shared_ptr<const LoadedMap> map = nullptr) {
    return WorldConfig{MapContent::map_id(), std::move(map)};
}

WorldTerrain& terrain_of(core::Simulation& simulation) {
    return simulation.world().ctx().get<WorldTerrain>();
}

std::vector<std::byte> save_bytes(const core::Simulation& simulation, const content::ContentDatabase& db) {
    save::SaveOptions options;
    options.content = &db;
    return save::save_simulation_bytes(simulation, options);
}

save::LoadOutcome load_bytes(core::Simulation& simulation, const std::vector<std::byte>& bytes,
                             const content::ContentDatabase& db) {
    save::LoadOptions options;
    options.content = &db;
    return save::load_simulation_bytes(simulation, bytes, options);
}

bool has_warning(const std::vector<std::string>& warnings, const std::string& needle) {
    return std::any_of(warnings.begin(), warnings.end(),
                       [&](const std::string& warning) { return warning.find(needle) != std::string::npos; });
}

} // namespace

TEST_CASE("install opens the map named by its definition", "[world][module]") {
    const TempDir dir("world_install");
    const MapContent map(dir);
    const auto db = load_content(map);
    core::Simulation simulation(3, 1);
    install(simulation, *db, config());
    const WorldTerrain& terrain = terrain_of(simulation);
    REQUIRE(terrain.has_map());
    CHECK(terrain.map_id() == MapContent::map_id());
    CHECK(terrain.package_hash() == read_manifest(map.package_dir()).package_hash);
    CHECK(terrain.generator_version() == k_terrain_generator_version);
    CHECK(terrain.map_seed() == 1); // the MapDef's seed, not the game's (3)
    CHECK(terrain.active_tiles().empty());
    CHECK(simulation.world().registry().find_singleton("world.Terrain") != nullptr);
    CHECK(simulation.after_load_hooks().size() == 1);
}

TEST_CASE("install reports a bad map id or package", "[world][module]") {
    const TempDir dir("world_install_errors");
    const MapContent map(dir);
    const auto db = load_content(map);
    {
        core::Simulation simulation(1, 1);
        CHECK_THROWS_AS(install(simulation, *db, WorldConfig{}), std::invalid_argument);
    }
    {
        core::Simulation simulation(1, 1);
        CHECK_THROWS_AS(install(simulation, *db, WorldConfig{"test:map.nowhere", nullptr}),
                        std::invalid_argument);
    }
    CHECK_THROWS_AS(open_map(*db, "test:map.nowhere"), std::invalid_argument);
    std::filesystem::remove(map.package_dir() / "rivers.json");
    {
        core::Simulation simulation(1, 1);
        CHECK_THROWS_AS(install(simulation, *db, config()), MapFormatError);
    }
}

TEST_CASE("the hash covers which tiles are active, not how they were activated", "[world][module]") {
    const TempDir dir("world_hash");
    const MapContent map(dir);
    const auto db = load_content(map);
    const auto shared = open_map(*db, MapContent::map_id());
    core::Simulation a(1, 1);
    core::Simulation b(1, 4);
    install(a, *db, config(shared));
    install(b, *db, config(shared));
    CHECK(a.world().state_hash() == b.world().state_hash());

    const std::vector<TileCoord> tiles = k_city.tiles();
    terrain_of(a).activate_tiles(a.jobs(), tiles);
    CHECK(a.world().state_hash() != b.world().state_hash());
    // The same set, in two calls and another order.
    std::vector<TileCoord> reversed(tiles.rbegin(), tiles.rend());
    terrain_of(b).activate_tiles(b.jobs(), std::span(reversed).first(5));
    terrain_of(b).activate_tiles(b.jobs(), reversed);
    CHECK(a.world().state_hash() == b.world().state_hash());
}

TEST_CASE("save and load restore the active tiles, the heights and the state hash", "[world][module]") {
    const TempDir dir("world_save_load");
    const MapContent map(dir);
    const auto db = load_content(map);
    core::Simulation original(5, 2);
    install(original, *db, config());
    terrain_of(original).activate_tiles(original.jobs(), k_city.tiles(1));
    for (int i = 0; i < 3; ++i) {
        original.step();
    }

    const std::vector<std::byte> bytes = save_bytes(original, *db);
    // No heights in the save: 36 tiles of 513 x 513 floats would be 38 MB.
    CHECK(bytes.size() < 16 * 1024);

    core::Simulation loaded(5, 3);
    install(loaded, *db, config());
    const save::LoadOutcome outcome = load_bytes(loaded, bytes, *db);
    CHECK(outcome.warnings.empty());
    const WorldTerrain& before = terrain_of(original);
    const WorldTerrain& after = terrain_of(loaded);
    CHECK(after.active_tiles() == before.active_tiles());
    CHECK(after.saved_state() == before.saved_state());
    CHECK(loaded.world().state_hash() == original.world().state_hash());
    for (const auto& [coord, tile] : before.tiles()) {
        const auto& restored = *after.tile(coord);
        CHECK(std::memcmp(tile->heights().data(), restored.heights().data(), tile->heights().size_bytes()) ==
              0);
    }

    // And the two continue identically.
    for (int i = 0; i < 5; ++i) {
        original.step();
        loaded.step();
    }
    CHECK(loaded.world().state_hash() == original.world().state_hash());
}

TEST_CASE("a load on the same map keeps the shared package", "[world][module]") {
    const TempDir dir("world_shared_map");
    const MapContent map(dir);
    const auto db = load_content(map);
    const auto shared = open_map(*db, MapContent::map_id());
    core::Simulation original(5, 1);
    install(original, *db, config(shared));
    terrain_of(original).activate_tiles(original.jobs(), k_city.tiles());
    core::Simulation loaded(5, 1);
    install(loaded, *db, config(shared));
    load_bytes(loaded, save_bytes(original, *db), *db);
    CHECK(terrain_of(loaded).map() == shared);
    CHECK(terrain_of(loaded).active_tiles().size() == 16);
}

TEST_CASE("a rebuilt package loads with a warning", "[world][module]") {
    const TempDir dir("world_package_changed");
    const MapContent map(dir);
    const auto db = load_content(map);
    core::Simulation original(5, 1);
    install(original, *db, config());
    terrain_of(original).activate_tiles(original.jobs(), k_city.tiles());
    const std::vector<std::byte> bytes = save_bytes(original, *db);
    const std::uint64_t old_hash = terrain_of(original).package_hash();

    map.regenerate_package(2);
    core::Simulation loaded(5, 1);
    install(loaded, *db, config());
    const save::LoadOutcome outcome = load_bytes(loaded, bytes, *db);
    CHECK(has_warning(outcome.warnings, "terrain package of map 'test:map.synthetic' changed"));
    CHECK(has_warning(outcome.warnings, hash_to_hex(old_hash)));
    // The tiles come back, made from the package that is there now.
    CHECK(terrain_of(loaded).active_tiles() == k_city.tiles());
    CHECK(terrain_of(loaded).package_hash() != old_hash);
}

TEST_CASE("restoring reports another generator version and tiles off the map", "[world][module]") {
    const TempDir dir("world_restore");
    const MapContent map(dir);
    const auto db = load_content(map);
    core::Simulation simulation(5, 1);
    install(simulation, *db, config());
    WorldTerrain& terrain = terrain_of(simulation);

    SavedTerrain saved = terrain.saved_state();
    saved.generator_version = k_terrain_generator_version + 7;
    saved.active_tiles = {TileCoord{2, 3}, TileCoord{3, 3}, TileCoord{40, 40}};
    const std::vector<std::string> warnings = restore_terrain(terrain, saved, *db, simulation.jobs());
    CHECK(has_warning(warnings, "terrain generator changed"));
    CHECK(has_warning(warnings, "skipped terrain tile (40, 40)"));
    CHECK(!has_warning(warnings, "package"));
    CHECK(terrain.active_tiles() == std::vector<TileCoord>{TileCoord{2, 3}, TileCoord{3, 3}});
    // The terrain describes what it is now made with, so the next save carries this build's version.
    CHECK(terrain.saved_state().generator_version == k_terrain_generator_version);

    saved.map_id = "test:map.gone";
    CHECK_THROWS_AS(restore_terrain(terrain, saved, *db, simulation.jobs()), save::SaveFormatError);
}

TEST_CASE("a save without terrain leaves the installed map with no tiles", "[world][module]") {
    const TempDir dir("world_no_terrain_save");
    const MapContent map(dir);
    const auto db = load_content(map);
    core::Simulation original(5, 1); // no world module
    core::Simulation loaded(5, 1);
    install(loaded, *db, config());
    const save::LoadOutcome outcome = load_bytes(loaded, save_bytes(original, *db), *db);
    CHECK(outcome.warnings.empty());
    CHECK(terrain_of(loaded).has_map());
    CHECK(terrain_of(loaded).active_tiles().empty());
}

TEST_CASE("the publish hook shares the active tiles with the snapshot", "[world][module]") {
    const TempDir dir("world_publish");
    const MapContent map(dir);
    const auto db = load_content(map);
    core::Simulation simulation(5, 2);
    runner::RenderSnapshot snapshot;
    publish_terrain_tiles(simulation, snapshot); // no world module: nothing
    CHECK(snapshot.terrain_tiles.empty());

    install(simulation, *db, config());
    terrain_of(simulation).activate_tiles(simulation.jobs(), k_city.tiles(1));
    publish_terrain_tiles(simulation, snapshot);
    REQUIRE(snapshot.terrain_tiles.size() == 36);
    CHECK(std::is_sorted(snapshot.terrain_tiles.begin(), snapshot.terrain_tiles.end(),
                         [](const auto& a, const auto& b) { return a.coord < b.coord; }));
    for (const runner::SnapshotTerrainTile& entry : snapshot.terrain_tiles) {
        CHECK(entry.tile == terrain_of(simulation).tile(entry.coord)); // the same object, not a copy
        CHECK(entry.version == entry.tile->version());
    }
    runner::RenderSnapshot next;
    publish_terrain_tiles(simulation, next);
    CHECK(next.terrain_tiles[7].tile == snapshot.terrain_tiles[7].tile); // unchanged tile, same pointer
}

namespace {

runner::NewGameRequest map_request(runner::FakeClock& clock, const MapContent& map) {
    runner::NewGameRequest request;
    request.seed = 4;
    const std::filesystem::path content = CITYSIM_GAME_CONTENT_DIR;
    request.content_roots = {content, map.root()};
    request.enabled_mods = {"base", "test"};
    request.era_id = "base:era.ancient";
    request.map_id = MapContent::map_id();
    request.clock = &clock;
    request.worker_count = 2;
    return request;
}

} // namespace

TEST_CASE("a game session with a map installs the world", "[world][module][runner]") {
    const TempDir dir("world_session");
    const MapContent map(dir);
    runner::FakeClock clock;
    runner::NewGameOutcome outcome = runner::start_new_game(map_request(clock, map));
    REQUIRE(outcome.ok());
    runner::GameSession& session = *outcome.session;
    REQUIRE(session.runner().wait_until_idle());
    CHECK(session.latest_snapshot().terrain_tiles.empty()); // no city yet

    // A save and a load go through the runner and the after-load hook.
    const std::filesystem::path file = dir / "game.csav";
    session.runner().submit(core::SaveGame{file.generic_string()});
    REQUIRE(session.runner().wait_until_idle());
    session.runner().submit(core::LoadGame{file.generic_string()});
    REQUIRE(session.runner().wait_until_idle());
    const runner::RenderSnapshot snapshot = session.latest_snapshot();
    const auto loaded =
        std::find_if(snapshot.ui_events.begin(), snapshot.ui_events.end(),
                     [](const auto& event) { return event.kind == runner::UiEventKind::LoadCompleted; });
    REQUIRE(loaded != snapshot.ui_events.end());
    CHECK(loaded->ok);
    CHECK(loaded->warnings.empty());

    session.runner().stop();
    const auto* terrain = session.runner().simulation().world().ctx().find<WorldTerrain>();
    REQUIRE(terrain != nullptr);
    CHECK(terrain->map_id() == MapContent::map_id());
}

TEST_CASE("a game session reports an unknown map as an error", "[world][module][runner]") {
    const TempDir dir("world_session_unknown");
    const MapContent map(dir);
    runner::FakeClock clock;
    runner::NewGameRequest request = map_request(clock, map);
    request.map_id = "test:map.nowhere";
    const runner::NewGameOutcome outcome = runner::start_new_game(request);
    CHECK(!outcome.ok());
    REQUIRE(!outcome.errors.empty());
    CHECK(outcome.errors.front().message.find("unknown map 'test:map.nowhere'") != std::string::npos);
}
