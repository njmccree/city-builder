#include "citysim/core/commands.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/test_map.hpp"
#include "citysim/debug/walking_skeleton.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/time/time_module.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <vector>

using namespace citysim;

namespace {

debug::WalkingSkeletonConfig test_config() {
    debug::WalkingSkeletonConfig config;
    config.map_width = 64;
    config.map_height = 64;
    config.max_searches_per_tick = 60;
    config.path_node_budget = 20000;
    return config;
}

struct Game {
    core::Simulation sim;
    debug::WalkingSkeleton skeleton;

    Game(std::uint64_t seed, std::size_t workers) : sim(seed, workers) {
        time::install(sim);
        skeleton = debug::install(sim, test_config());
    }

    void spawn(std::uint32_t count, std::uint64_t sequence = 1) {
        sim.enqueue({core::SequencedCommand{sequence, sim.tick(), debug::SpawnDebugAgents{count}}});
    }
    std::size_t agents() { return sim.world().view<debug::DebugWanderer>().size(); }
};

} // namespace

TEST_CASE("agents spawn on walkable cells, plan paths and walk them", "[debug][skeleton]") {
    Game game(5, 1);
    game.spawn(200);
    game.sim.step();
    CHECK(game.agents() == 200);

    const auto& map = game.sim.world().ctx().get<debug::TestMap>();
    std::vector<debug::Position> start;
    for (const auto entity : game.sim.world().view<debug::Position>()) {
        const auto& position = game.sim.world().get<debug::Position>(entity);
        CHECK(map.walkable(map.cell_at(position.x, position.z)));
        start.push_back(position);
    }

    for (int tick = 0; tick < 300; ++tick) {
        game.sim.step();
    }
    std::size_t moved = 0;
    std::size_t with_path = 0;
    std::size_t index = 0;
    for (const auto entity : game.sim.world().view<debug::Position>()) {
        const auto& position = game.sim.world().get<debug::Position>(entity);
        // Agents only ever stand on walkable cells: paths run through cell centres and never cut corners.
        CHECK(map.walkable(map.cell_at(position.x, position.z)));
        const auto& follower = game.sim.world().get<debug::PathFollower>(entity);
        with_path += follower.cells.empty() ? 0U : 1U;
        moved += (std::fabs(position.x - start[index].x) + std::fabs(position.z - start[index].z) > 1.0F)
                     ? 1U
                     : 0U;
        ++index;
    }
    CHECK(moved > 150);
    CHECK(with_path > 150);
    CHECK(game.skeleton.planner->searches_total() >= 200);
}

TEST_CASE("the planner never searches for more agents than its limit per tick", "[debug][skeleton]") {
    Game game(5, 1);
    game.spawn(500);
    game.sim.step(); // spawned during this tick's ApplyCommands, planned in its Update
    CHECK(game.skeleton.planner->searches_last_tick() == 60);
    game.sim.step();
    CHECK(game.skeleton.planner->searches_last_tick() <= 60);
}

TEST_CASE("the same game gives the same hash with 1 and 8 workers", "[debug][skeleton][determinism]") {
    Game one(11, 1);
    Game eight(11, 8);
    one.spawn(2000);
    eight.spawn(2000);
    for (int tick = 1; tick <= 500; ++tick) {
        one.sim.step();
        eight.sim.step();
        if (tick % 25 == 0 || tick < 5) {
            INFO("tick " << tick);
            REQUIRE(one.sim.world().state_hash() == eight.sim.world().state_hash());
        }
    }
    // And the hash really does depend on what the agents do.
    Game other_seed(12, 1);
    other_seed.spawn(2000);
    for (int tick = 1; tick <= 500; ++tick) {
        other_seed.sim.step();
    }
    CHECK(other_seed.sim.world().state_hash() != one.sim.world().state_hash());
}

TEST_CASE("a game resumed from a save continues exactly like the original", "[debug][skeleton][save]") {
    constexpr int k_total = 400;
    constexpr int k_split = 150;

    Game straight(21, 2);
    straight.spawn(1500);
    std::vector<std::uint64_t> expected;
    for (int tick = 1; tick <= k_total; ++tick) {
        if (tick == 200) {
            straight.spawn(100, 1000); // more agents mid-walk
        }
        straight.sim.step();
        expected.push_back(straight.sim.world().state_hash());
    }

    Game first(21, 2);
    first.spawn(1500);
    for (int tick = 1; tick <= k_split; ++tick) {
        first.sim.step();
    }
    const auto bytes = save::save_simulation_bytes(first.sim, save::SaveOptions{});

    Game resumed(999, 8); // a different seed and thread count: the save restores the seed
    save::load_simulation_bytes(resumed.sim, bytes, save::LoadOptions{});
    CHECK(resumed.sim.tick() == k_split);
    CHECK(resumed.sim.world().state_hash() == first.sim.world().state_hash());
    for (int tick = k_split + 1; tick <= k_total; ++tick) {
        if (tick == 200) {
            resumed.spawn(100, 1000);
        }
        resumed.sim.step();
        INFO("tick " << tick);
        REQUIRE(resumed.sim.world().state_hash() == expected[static_cast<std::size_t>(tick - 1)]);
    }
}

TEST_CASE("a pending spawn command survives a save", "[debug][skeleton][save]") {
    Game game(3, 1);
    game.spawn(40);
    const auto bytes = save::save_simulation_bytes(game.sim, save::SaveOptions{});

    Game loaded(3, 1);
    save::load_simulation_bytes(loaded.sim, bytes, save::LoadOptions{});
    CHECK(loaded.sim.pending_commands().size() == 1);
    loaded.sim.step();
    CHECK(loaded.agents() == 40);
}

TEST_CASE("clearing removes every agent, and new agents are new", "[debug][skeleton]") {
    Game game(5, 1);
    game.spawn(30);
    game.sim.step();
    REQUIRE(game.agents() == 30);
    const auto first =
        game.sim.world().get<debug::Position>(*game.sim.world().view<debug::Position>().begin());

    game.sim.enqueue({core::SequencedCommand{2, game.sim.tick(), debug::ClearDebugAgents{}}});
    game.sim.step();
    CHECK(game.agents() == 0);
    CHECK(game.sim.world().entity_count() == 0);

    game.spawn(30, 3);
    game.sim.step();
    CHECK(game.agents() == 30);
    CHECK(game.sim.world().ctx().get<debug::DebugAgentCounter>().spawned == 60);
    // The counter makes the second batch draw new positions rather than repeat the first batch.
    bool all_same = true;
    for (const auto entity : game.sim.world().view<debug::Position>()) {
        const auto& position = game.sim.world().get<debug::Position>(entity);
        all_same = all_same && position.x == first.x && position.z == first.z;
    }
    CHECK_FALSE(all_same);
}

TEST_CASE("installing without the time module is an error", "[debug][skeleton]") {
    core::Simulation sim(1, 1);
    CHECK_THROWS_AS(debug::install(sim), std::logic_error);
}
