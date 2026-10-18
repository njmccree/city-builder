#include "citysim/content/content_database.hpp"
#include "citysim/core/commands.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/core/ui_notice.hpp"
#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/agent_snapshot.hpp"
#include "citysim/debug/debug_commands.hpp"
#include "citysim/debug/terrain_walk_grid.hpp"
#include "citysim/debug/walking_skeleton.hpp"
#include "citysim/runner/game_session.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/time/time_module.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/flood_map.hpp"
#include "citysim/world/terraform_commands.hpp"
#include "citysim/world/water_commands.hpp"
#include "citysim/world/water_queries.hpp"
#include "citysim/world/world_module.hpp"
#include "support/map_content.hpp"
#include "support/synthetic_map.hpp"
#include "support/temp_dir.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <entt/entt.hpp>
#include <memory>
#include <vector>

using namespace citysim;
using namespace citysim::debug;
using namespace citysim::world;
using citysim::test::MapContent;
using citysim::test::TempDir;

namespace {

// A world game with the walking skeleton on the real terrain of one city that has a stretch of the main
// river in its area.
struct Game {
    TempDir dir;
    MapContent map;
    std::unique_ptr<content::ContentDatabase> db = std::make_unique<content::ContentDatabase>();
    std::shared_ptr<const LoadedMap> loaded;
    TerrainDefines defines;
    std::unique_ptr<core::Simulation> sim;
    WalkingSkeleton skeleton;
    std::uint64_t sequence = 1;
    TileCoord origin_tile;

    explicit Game(const char* label, TerrainDefines terrain_defines = {}, std::size_t workers = 2)
        : dir(label), map(dir, citysim::test::synthetic_map_dir()), defines(terrain_defines) {
        REQUIRE(db->load({map.mod_dir()}).ok());
        loaded = open_map(*db, MapContent::map_id());
        sim = make_simulation(workers, &skeleton);
        const auto& vertices = loaded->rivers().reaches().at(0).vertices;
        const RiverVertex& vertex = *std::min_element(
            vertices.begin(), vertices.end(), [](const RiverVertex& a, const RiverVertex& b) {
                return std::abs(a.z - 45000.0) < std::abs(b.z - 45000.0);
            });
        const TileCoord home = tile_of(WorldPos{vertex.x, vertex.z});
        origin_tile = TileCoord{std::clamp(home.tx - 1, 16, 27), std::clamp(home.ty - 1, 18, 39)};
        run(core::Command{FoundCity{origin_tile.tx, origin_tile.ty, "Test"}});
        sim->step(); // the walk grid is built in the PostUpdate of the first tick of the city
        REQUIRE(sim->world().ctx().get<ActiveCity>().city_id == 1);
    }

    std::unique_ptr<core::Simulation> make_simulation(std::size_t workers, WalkingSkeleton* out = nullptr) {
        auto simulation = std::make_unique<core::Simulation>(7, workers);
        time::install(*simulation);
        WorldConfig config{MapContent::map_id(), loaded};
        config.terrain = defines;
        install(*simulation, *db, config);
        WalkingSkeletonConfig skeleton_config;
        skeleton_config.source = WalkGridSource::Terrain;
        const WalkingSkeleton installed = debug::install(*simulation, skeleton_config);
        if (out != nullptr) {
            *out = installed;
        }
        return simulation;
    }

    std::vector<core::UiNotice> run(core::Simulation& simulation, core::Command command) {
        simulation.enqueue({core::SequencedCommand{sequence++, simulation.tick(), std::move(command)}});
        simulation.step();
        return simulation.world().ctx().get<core::UiOutbox>().take();
    }
    std::vector<core::UiNotice> run(core::Command command) { return run(*sim, std::move(command)); }
    void steps(int count) {
        for (int i = 0; i < count; ++i) {
            sim->step();
        }
    }

    WorldTerrain& terrain() { return sim->world().ctx().get<WorldTerrain>(); }
    const TerrainWalkGrid& grid() { return sim->world().ctx().get<TerrainWalkGrid>(); }
    CityArea area() const { return CityArea{origin_tile}; }
    std::size_t agent_count() { return sim->world().view<DebugWanderer>().size(); }
    std::uint64_t hash() const { return sim->world().state_hash(); }
};

// Is the ground at the middle of this cell water, by the world's own queries (not the grid's code)?
bool wet(Game& game, std::uint32_t cell) {
    const TerrainWalkGrid& grid = game.grid();
    const WorldPos pos = to_world(game.area(), LocalPos{grid.center_x(cell), grid.center_z(cell)});
    const WaterState& water = game.sim->world().ctx().get<WaterState>();
    if (is_water(game.terrain(), water, pos)) {
        return true;
    }
    const FloodMap& flood = game.sim->world().ctx().get<FloodMap>();
    // Flooded land: a flooded corner sample of the cell.
    for (const double dx : {-2.0, 2.0}) {
        for (const double dz : {-2.0, 2.0}) {
            if (flood.flooded_at(WorldPos{pos.x + dx, pos.z + dz})) {
                return true;
            }
        }
    }
    return false;
}

// The slope test, from heights read through the terrain service at the four corners of the cell.
bool too_steep(Game& game, std::uint32_t cell, float max_slope) {
    const TerrainWalkGrid& grid = game.grid();
    const double x = static_cast<double>(grid.x_of(cell)) * 4.0;
    const double z = static_cast<double>(grid.y_of(cell)) * 4.0;
    const auto h = [&](double dx, double dz) {
        return game.terrain().height_at(
            to_world(game.area(), LocalPos{static_cast<float>(x + dx), static_cast<float>(z + dz)}));
    };
    const float h00 = h(0, 0);
    const float h10 = h(4, 0);
    const float h01 = h(0, 4);
    const float h11 = h(4, 4);
    const float dx = ((h10 + h11) - (h00 + h01)) * 0.125F;
    const float dz = ((h01 + h11) - (h00 + h10)) * 0.125F;
    return dx * dx + dz * dz >= max_slope * max_slope;
}

std::size_t count_walkable(const WalkGrid& grid) {
    std::size_t count = 0;
    for (const std::uint64_t word : grid.words()) {
        count += static_cast<std::size_t>(std::popcount(word));
    }
    return count;
}

struct AgentCheck {
    std::size_t on_blocked = 0;
    std::size_t on_water = 0;
};

// `with_water` also asks the world's water queries about every agent's cell: slow, so loops use the grid
// alone.
AgentCheck check_agents(Game& game, bool with_water = true) {
    AgentCheck check;
    auto view = game.sim->world().view<DebugWanderer, Position>();
    const TerrainWalkGrid& grid = game.grid();
    for (const entt::entity entity : view) {
        const Position& position = view.get<Position>(entity);
        const std::uint32_t cell = grid.cell_at(position.x, position.z);
        check.on_blocked += grid.walkable(cell) ? 0U : 1U;
        if (with_water) {
            check.on_water += wet(game, cell) ? 1U : 0U;
        }
    }
    return check;
}

} // namespace

TEST_CASE("the walk grid of the active city follows the terrain and the water",
          "[debug][terrain_walk][synthetic_map]") {
    Game game("walk_grid_basic");
    const TerrainWalkGrid& grid = game.grid();
    REQUIRE(grid.city_id() == 1);
    CHECK(grid.width() == 2048);
    CHECK(grid.height() == 2048);
    CHECK(grid.cell_size() == 4.0F);
    CHECK(grid.area() == game.area());

    // Some of the area can be walked and some cannot (the river channel at least).
    const std::size_t walkable = count_walkable(grid);
    CHECK(walkable > grid.cell_count() / 2);
    CHECK(walkable < grid.cell_count());

    // Checked against the world's own queries: no cell the grid calls walkable has water at its middle, and
    // the cells in water are blocked. A coarse sweep of the whole area plus every cell around the river.
    std::size_t checked = 0;
    std::size_t blocked_by_water = 0;
    bool consistent = true;
    for (std::uint32_t y = 0; y < grid.height(); y += 7) {
        for (std::uint32_t x = 0; x < grid.width(); x += 5) {
            const std::uint32_t cell = grid.index_of(x, y);
            const bool water = wet(game, cell);
            const bool steep = too_steep(game, cell, game.defines.walk_max_slope);
            const bool expected =
                !water && !steep &&
                game.terrain().height_at(
                    to_world(game.area(), LocalPos{grid.center_x(cell), grid.center_z(cell)})) > 0.0F;
            consistent = consistent && grid.walkable(cell) == expected;
            blocked_by_water += (water && !grid.walkable(cell)) ? 1U : 0U;
            ++checked;
        }
    }
    CHECK(checked > 40000);
    CHECK(consistent);
    CHECK(blocked_by_water > 0);
    CHECK(grid.version() > 1);
}

TEST_CASE("agents spawn and walk on the terrain, never on water", "[debug][terrain_walk][synthetic_map]") {
    Game game("walk_agents");
    game.run(core::Command{SpawnDebugAgents{300}});
    REQUIRE(game.agent_count() == 300);
    AgentCheck check = check_agents(game);
    CHECK(check.on_blocked == 0);
    CHECK(check.on_water == 0);
    for (int round = 0; round < 8; ++round) {
        game.steps(50);
        check = check_agents(game, round == 7);
        REQUIRE(check.on_blocked == 0);
        REQUIRE(check.on_water == 0);
    }
    // They really moved.
    auto view = game.sim->world().view<DebugWanderer, PathFollower>();
    std::size_t moving = 0;
    for (const entt::entity entity : view) {
        moving += view.get<PathFollower>(entity).finished() ? 0U : 1U;
    }
    CHECK(moving > 100);
}

TEST_CASE("agents are published standing on the terrain", "[debug][terrain_walk][synthetic_map]") {
    Game game("walk_agent_height");
    game.run(core::Command{SpawnDebugAgents{50}});
    game.steps(20);
    runner::RenderSnapshot snapshot;
    publish_agent_snapshot(*game.sim, snapshot);
    const runner::SnapshotChannel* channel = snapshot.find_channel(k_agent_channel);
    REQUIRE(channel != nullptr);
    REQUIRE(channel->record_count() == 50);
    bool all_on_ground = true;
    bool some_above_sea = false;
    for (std::size_t i = 0; i < channel->record_count(); ++i) {
        const float x = channel->data[i * 4];
        const float y = channel->data[i * 4 + 1];
        const float z = channel->data[i * 4 + 2];
        const float ground = game.terrain().height_at_local(game.area(), LocalPos{x, z});
        all_on_ground = all_on_ground && y == ground;
        some_above_sea = some_above_sea || y > 1.0F;
    }
    CHECK(all_on_ground);
    CHECK(some_above_sea); // not the flat y = 0 of the test map
}

TEST_CASE("agents in newly flooded cells wade out, and paths through them are replanned",
          "[debug][terrain_walk][synthetic_map]") {
    Game game("walk_flood");
    game.run(core::Command{SpawnDebugAgents{600}});
    game.steps(30);
    REQUIRE(check_agents(game).on_blocked == 0);
    const std::uint64_t version_before = game.grid().version();
    const std::size_t walkable_before = count_walkable(game.grid());

    // A small flood: the river 0.5 m above its base level.
    game.run(core::Command{SetRiverLevel{0, 0.5F}});
    for (int i = 0; i < 200 && !game.sim->world().ctx().get<FloodMap>().settled(); ++i) {
        game.sim->step();
    }
    game.steps(2);
    REQUIRE(game.sim->world().ctx().get<FloodMap>().flooded_area_m2(0) > 0.0);
    CHECK(game.grid().version() > version_before);
    CHECK(count_walkable(game.grid()) < walkable_before); // the flooded land is blocked now

    // Some agents were caught in it (this is what the test is about), and they all get out.
    const AgentCheck caught = check_agents(game);
    CHECK(caught.on_water == caught.on_blocked); // the grid and the water agree on who is in trouble
    INFO("agents in the flood: " << caught.on_blocked);
    CHECK(caught.on_blocked > 0);
    for (int i = 0; i < 1500 && check_agents(game, false).on_blocked > 0; ++i) {
        game.sim->step();
    }
    const AgentCheck after = check_agents(game);
    CHECK(after.on_blocked == 0);
    CHECK(after.on_water == 0);
    CHECK(game.skeleton.planner->paths_invalidated_total() > 0);

    // The water goes back and the land is open again.
    game.run(core::Command{SetRiverLevel{0, 0.0F}});
    for (int i = 0; i < 200 && !game.sim->world().ctx().get<FloodMap>().settled(); ++i) {
        game.sim->step();
    }
    game.steps(2);
    CHECK(count_walkable(game.grid()) == walkable_before);
}

TEST_CASE("terrain edits that make ground too steep block it", "[debug][terrain_walk][synthetic_map]") {
    TerrainDefines strong;
    strong.terraform_step_m = 50.0F;
    Game game("walk_edit", strong);
    const std::uint64_t version = game.grid().version();
    // A cone 50 m high over 40 m: steeper than 35 degrees nearly everywhere on it.
    game.run(core::Command{Terraform{1, TerraformOp::Raise, LocalPos{600.0F, 600.0F}, 40.0F, 1.0F, 0.0F}});
    game.steps(1);
    CHECK(game.grid().version() > version);
    // The cells of the cone at about 20 m out are blocked.
    CHECK(!game.grid().walkable(static_cast<std::int64_t>(600 / 4 + 5), static_cast<std::int64_t>(600 / 4)));
    // Levelled out again, the ground can be walked on.
    game.run(core::Command{Terraform{1, TerraformOp::Level, LocalPos{600.0F, 600.0F}, 60.0F, 1.0F,
                                     game.terrain().height_at_local(game.area(), LocalPos{900.0F, 900.0F})}});
    game.steps(1);
    CHECK(game.grid().walkable(static_cast<std::int64_t>(600 / 4), static_cast<std::int64_t>(600 / 4)));
}

TEST_CASE("an incrementally updated grid equals one built from scratch",
          "[debug][terrain_walk][synthetic_map]") {
    Game game("walk_incremental");
    game.run(core::Command{SetRiverLevel{0, 0.7F}});
    game.run(
        core::Command{Terraform{1, TerraformOp::Smooth, LocalPos{3000.0F, 3000.0F}, 150.0F, 1.0F, 0.0F}});
    game.run(core::Command{Terraform{1, TerraformOp::Raise, LocalPos{2000.0F, 5000.0F}, 80.0F, 1.0F, 0.0F}});
    for (int i = 0; i < 200 && !game.sim->world().ctx().get<FloodMap>().settled(); ++i) {
        game.sim->step();
    }
    game.steps(3);

    // A save, a load and a tick: the loaded game builds its grid from nothing.
    save::SaveOptions save_options;
    save_options.content = game.db.get();
    const std::vector<std::byte> bytes = save::save_simulation_bytes(*game.sim, save_options);
    auto loaded = game.make_simulation(2);
    save::LoadOptions load_options;
    load_options.content = game.db.get();
    const save::LoadOutcome outcome = save::load_simulation_bytes(*loaded, bytes, load_options);
    CHECK(outcome.warnings.empty());
    // The loaded game fills its floods again (they are derived state); wait until it has the same extent.
    const double flooded = game.sim->world().ctx().get<FloodMap>().flooded_area_m2(0);
    REQUIRE(flooded > 0.0);
    for (int i = 0; i < 400 && loaded->world().ctx().get<FloodMap>().flooded_area_m2(0) != flooded; ++i) {
        loaded->step();
    }
    loaded->step();
    loaded->step();
    {
        const TerrainWalkGrid& fresh = loaded->world().ctx().get<TerrainWalkGrid>();
        std::size_t differing = 0;
        std::uint32_t first = 0;
        for (std::uint32_t cell = 0; cell < fresh.cell_count(); ++cell) {
            if (fresh.walkable(cell) != game.grid().walkable(cell)) {
                first = differing == 0 ? cell : first;
                ++differing;
            }
        }
        INFO("differing cells: " << differing << ", first at (" << fresh.x_of(first) << ", "
                                 << fresh.y_of(first) << "): incremental " << game.grid().walkable(first)
                                 << ", fresh " << fresh.walkable(first));
        CHECK(differing == 0);
    }
}

TEST_CASE("a game with agents on the terrain resumes from a save exactly",
          "[debug][terrain_walk][save][synthetic_map]") {
    Game game("walk_save_load");
    game.run(core::Command{SpawnDebugAgents{200}});
    game.steps(40);
    save::SaveOptions save_options;
    save_options.content = game.db.get();
    const std::vector<std::byte> bytes = save::save_simulation_bytes(*game.sim, save_options);

    auto loaded = game.make_simulation(2);
    save::LoadOptions load_options;
    load_options.content = game.db.get();
    const save::LoadOutcome outcome = save::load_simulation_bytes(*loaded, bytes, load_options);
    CHECK(outcome.warnings.empty());
    CHECK(loaded->world().state_hash() == game.hash());

    // The same things happen to both from here: a river rises, agents walk, a brush shapes the ground.
    for (const core::Command& command :
         {core::Command{SetRiverLevel{0, 0.5F}}, core::Command{SpawnDebugAgents{5}},
          core::Command{Terraform{1, TerraformOp::Raise, LocalPos{4000.0F, 4000.0F}, 60.0F, 1.0F, 0.0F}}}) {
        game.run(*game.sim, command);
        game.run(*loaded, command);
        for (int i = 0; i < 25; ++i) {
            game.sim->step();
            loaded->step();
        }
        CHECK(loaded->world().state_hash() == game.hash());
    }
}

TEST_CASE("the same game gives the same state with 1 and 8 workers",
          "[debug][terrain_walk][determinism][synthetic_map]") {
    const auto play = [](const char* label, std::size_t workers) {
        Game game(label, {}, workers);
        game.run(core::Command{SpawnDebugAgents{400}});
        game.steps(60);
        game.run(core::Command{SetRiverLevel{0, 0.5F}});
        for (int i = 0; i < 100; ++i) {
            game.sim->step();
        }
        return game.hash();
    };
    CHECK(play("walk_det_1", 1) == play("walk_det_8", 8));
}

TEST_CASE("switching city drops the agents of the previous one, a first city keeps them",
          "[debug][terrain_walk][synthetic_map]") {
    Game game("walk_switch");
    game.run(core::Command{SpawnDebugAgents{20}});
    REQUIRE(game.agent_count() == 20);
    const TileCoord second{game.origin_tile.tx >= 22 ? game.origin_tile.tx - 4 : game.origin_tile.tx + 4,
                           game.origin_tile.ty};
    game.run(core::Command{FoundCity{second.tx, second.ty, "Other"}});
    game.steps(2);
    CHECK(game.agent_count() == 20); // founding does not change the active city
    game.run(core::Command{SetActiveCity{2}});
    game.steps(2);
    CHECK(game.agent_count() == 0);
    CHECK(game.grid().city_id() == 2);
    CHECK(game.grid().area() == CityArea{second});
    game.run(core::Command{SpawnDebugAgents{10}});
    CHECK(game.agent_count() == 10);
}

TEST_CASE("the terrain walk grid needs the world", "[debug][terrain_walk]") {
    core::Simulation simulation(1, 1);
    time::install(simulation);
    WalkingSkeletonConfig config;
    config.source = WalkGridSource::Terrain;
    CHECK_THROWS_AS(debug::install(simulation, config), std::logic_error);
}

TEST_CASE("a world game started with world agents publishes agents standing on the terrain",
          "[debug][terrain_walk][session]") {
    TempDir dir("walk_session");
    MapContent content(dir); // the small synthetic world: its one city fits at tile (2, 3)
    runner::FakeClock clock;
    runner::NewGameRequest request;
    request.seed = 5;
    const std::filesystem::path game_content = CITYSIM_GAME_CONTENT_DIR;
    request.content_roots = {game_content, content.root()};
    request.enabled_mods = {"base", "test"};
    request.era_id = "base:era.ancient";
    request.map_id = MapContent::map_id();
    request.world_agents = true;
    request.clock = &clock;
    request.worker_count = 2;
    runner::NewGameOutcome outcome = runner::start_new_game(request);
    REQUIRE(outcome.ok());
    auto& session = *outcome.session;
    REQUIRE(session.runner().wait_until_idle());

    const auto tick = [&](int count) {
        for (int i = 0; i < count; ++i) {
            clock.set_time(clock.now_seconds() + 1.0 / 20.0);
            REQUIRE(session.runner().wait_until_idle());
        }
    };
    session.runner().submit(FoundCity{2, 3, "Only"});
    tick(2);
    session.runner().submit(SpawnDebugAgents{40});
    tick(3);
    const runner::RenderSnapshot snapshot = session.latest_snapshot();
    const runner::SnapshotChannel* agents = snapshot.find_channel(k_agent_channel);
    REQUIRE(agents != nullptr);
    REQUIRE(agents->record_count() == 40);
    std::size_t above_ground = 0;
    for (std::size_t i = 0; i < agents->record_count(); ++i) {
        above_ground += agents->data[i * 4 + 1] > 0.5F ? 1U : 0U;
    }
    CHECK(above_ground > 0);
    CHECK(snapshot.find_grid("testmap.grid") == nullptr); // no test map in a world game
}
