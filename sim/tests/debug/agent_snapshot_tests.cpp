#include "citysim/core/commands.hpp"
#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/agent_snapshot.hpp"
#include "citysim/debug/test_map.hpp"
#include "citysim/debug/walking_skeleton.hpp"
#include "citysim/runner/simulation_runner.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace citysim;
using citysim::runner::FakeClock;
using citysim::runner::RenderSnapshot;
using citysim::runner::SimulationRunner;

TEST_CASE("the agent channel has one record per agent, in ascending id order", "[debug][snapshot]") {
    FakeClock clock;
    SimulationRunner runner(9, clock, {}, 1);
    debug::WalkingSkeletonConfig config;
    config.map_width = 48;
    config.map_height = 48;
    runner.configure([&](core::Simulation& sim) { debug::install(sim, config); });
    runner.set_publish_hook(&debug::publish_agent_snapshot);
    runner.start();
    REQUIRE(runner.wait_until_idle());

    // Before any agent exists the channel is there and empty.
    const RenderSnapshot empty = runner.latest_snapshot();
    const runner::SnapshotChannel* none = empty.find_channel(debug::k_agent_channel);
    REQUIRE(none != nullptr);
    CHECK(none->record_count() == 0);

    runner.submit(debug::SpawnDebugAgents{120});
    for (int tick = 1; tick <= 40; ++tick) {
        clock.set_time(tick * (1.0 / 20.0));
        REQUIRE(runner.wait_until_idle());
    }
    const RenderSnapshot snapshot = runner.latest_snapshot();
    REQUIRE(snapshot.tick == 40);
    const runner::SnapshotChannel* channel = snapshot.find_channel(debug::k_agent_channel);
    REQUIRE(channel != nullptr);
    CHECK(channel->stride == 4);
    REQUIRE(channel->record_count() == 120);
    REQUIRE(channel->ids.size() == 120);
    CHECK(std::is_sorted(channel->ids.begin(), channel->ids.end()));
    CHECK(std::adjacent_find(channel->ids.begin(), channel->ids.end()) == channel->ids.end()); // unique

    // The numbers are the agents' own: stop the runner and compare with the world.
    runner.stop();
    auto& world = runner.simulation().world();
    std::size_t matched = 0;
    for (std::size_t i = 0; i < channel->ids.size(); ++i) {
        for (const auto entity : world.view<debug::Position>()) {
            if (entt::to_entity(entity) != channel->ids[i]) {
                continue;
            }
            const auto& position = world.get<debug::Position>(entity);
            const float* record = &channel->data[i * 4];
            CHECK(record[0] == position.x);
            CHECK(record[1] == 0.0F);
            CHECK(record[2] == position.z);
            CHECK(record[3] == world.get<debug::Heading>(entity).yaw);
            ++matched;
        }
    }
    CHECK(matched == 120);
}

TEST_CASE("the agent channel is rebuilt for each snapshot", "[debug][snapshot]") {
    FakeClock clock;
    SimulationRunner runner(9, clock, {}, 1);
    runner.configure([](core::Simulation& sim) { debug::install(sim); });
    runner.set_publish_hook(&debug::publish_agent_snapshot);
    runner.start();
    runner.submit(debug::SpawnDebugAgents{10});
    for (int tick = 1; tick <= 6; ++tick) {
        clock.set_time(tick * (1.0 / 20.0));
        REQUIRE(runner.wait_until_idle());
    }
    const RenderSnapshot snapshot = runner.latest_snapshot();
    CHECK(snapshot.channels.size() == 1);
    CHECK(snapshot.find_channel(debug::k_agent_channel)->record_count() == 10);

    runner.submit(debug::ClearDebugAgents{});
    clock.set_time(7 * (1.0 / 20.0));
    REQUIRE(runner.wait_until_idle());
    CHECK(runner.latest_snapshot().find_channel(debug::k_agent_channel)->record_count() == 0);
    runner.stop();
}

TEST_CASE("the hook does nothing without the walking skeleton", "[debug][snapshot]") {
    FakeClock clock;
    SimulationRunner runner(9, clock, {}, 1);
    runner.set_publish_hook(&debug::publish_agent_snapshot);
    runner.start();
    clock.set_time(1.0 / 20.0);
    REQUIRE(runner.wait_until_idle());
    const RenderSnapshot snapshot = runner.latest_snapshot();
    const runner::SnapshotChannel* channel = snapshot.find_channel(debug::k_agent_channel);
    REQUIRE(channel != nullptr);
    CHECK(channel->record_count() == 0);
    runner.stop();
}

TEST_CASE("the test map grid is published once and shared between snapshots", "[debug][snapshot]") {
    FakeClock clock;
    SimulationRunner runner(9, clock, {}, 1);
    debug::WalkingSkeletonConfig config;
    config.map_width = 40;
    config.map_height = 24;
    config.cell_size = 2.0F;
    runner.configure([&](core::Simulation& sim) { debug::install(sim, config); });
    runner.set_publish_hook(debug::TestMapPublisher{});
    runner.start();
    REQUIRE(runner.wait_until_idle());
    const RenderSnapshot first = runner.latest_snapshot();
    const runner::SnapshotGrid* grid = first.find_grid(debug::k_test_map_grid);
    REQUIRE(grid != nullptr);
    CHECK(grid->width == 40);
    CHECK(grid->height == 24);
    CHECK(grid->cell_size == 2.0F);
    REQUIRE(grid->cells != nullptr);
    REQUIRE(grid->cells->size() == 40U * 24U);

    // It is the TestMap's own data: stop and compare cell by cell.
    clock.set_time(1.0 / 20.0);
    REQUIRE(runner.wait_until_idle());
    const RenderSnapshot second = runner.latest_snapshot();
    const runner::SnapshotGrid* again = second.find_grid(debug::k_test_map_grid);
    REQUIRE(again != nullptr);
    CHECK(again->cells.get() == grid->cells.get()); // unchanged map: the same shared grid, not a copy
    runner.stop();
    const auto& map = runner.simulation().world().ctx().get<debug::TestMap>();
    std::size_t blocked = 0;
    for (std::uint32_t index = 0; index < map.cell_count(); ++index) {
        CHECK((*grid->cells)[index] == (map.walkable(index) ? 1 : 0));
        if (!map.walkable(index)) {
            ++blocked;
        }
    }
    CHECK(blocked > 0);
}
