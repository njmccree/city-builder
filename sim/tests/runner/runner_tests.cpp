#include "citysim/core/state_hasher.hpp"
#include "citysim/runner/simulation_runner.hpp"
#include "citysim/time/time_commands.hpp"
#include "citysim/time/time_module.hpp"
#include "support/fn_system.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace citysim;
using citysim::runner::FakeClock;
using citysim::runner::RenderSnapshot;
using citysim::runner::RunnerConfig;
using citysim::runner::SimulationRunner;
using citysim::test::make_system;

namespace {
// Moves the fake clock and waits until the runner has done everything that became due.
void advance_to(FakeClock& clock, SimulationRunner& runner, double seconds) {
    clock.set_time(seconds);
    REQUIRE(runner.wait_until_idle());
}

// A counter in the world that DebugSetComponentValue commands set, shown in snapshots via the hook, so
// tests can see command effects without reaching into the running simulation.
struct Counter {
    int value = 0;
};

void install_counter(SimulationRunner& runner) {
    runner.simulation().world().ctx().emplace<Counter>();
    runner.simulation().dispatcher().register_handler<core::DebugSetComponentValue>(
        [](core::TickContext& context, const core::DebugSetComponentValue& cmd) {
            context.world.ctx().get<Counter>().value = cmd.value;
        });
    runner.set_publish_hook([](core::Simulation& sim, RenderSnapshot& snapshot) {
        snapshot.ui_events.push_back({"counter=" + std::to_string(sim.world().ctx().get<Counter>().value)});
    });
}

core::Command set_counter(std::int32_t value) {
    return core::DebugSetComponentValue{0, value};
}
} // namespace

TEST_CASE("runner: at 1x, one fake second gives 20 ticks", "[runner]") {
    FakeClock clock;
    SimulationRunner runner(1, clock, {}, 1);
    runner.start();
    REQUIRE(runner.wait_until_idle());
    REQUIRE(runner.latest_snapshot().tick == 0); // the first tick is one interval away

    const double interval = 1.0 / 20.0;
    for (int k = 1; k <= 20; ++k) {
        advance_to(clock, runner, k * interval);
        REQUIRE(runner.latest_snapshot().tick == static_cast<std::uint64_t>(k));
    }
    const RenderSnapshot snapshot = runner.latest_snapshot();
    REQUIRE(snapshot.tick == 20);
    REQUIRE(snapshot.speed == 1);
    REQUIRE_FALSE(snapshot.paused);
    runner.stop();
    REQUIRE(runner.simulation().tick() == 20);
}

TEST_CASE("runner: speeds 2x and 4x give 40 and 80 ticks per fake second", "[runner]") {
    for (const auto& [speed, ticks_per_second] :
         {std::pair<std::uint8_t, int>{2, 40}, std::pair<std::uint8_t, int>{3, 80}}) {
        FakeClock clock;
        SimulationRunner runner(1, clock, {}, 1);
        runner.start();
        REQUIRE(runner.wait_until_idle());

        // SetSpeed is a command, so it takes effect in the tick that consumes it: the first tick, at 1x.
        runner.submit(time::SetSpeed{speed});
        advance_to(clock, runner, 1.0 / 20.0);
        REQUIRE(runner.latest_snapshot().tick == 1);
        REQUIRE(runner.latest_snapshot().speed == speed);

        // From there, one fake second at the new speed gives exactly ticks_per_second ticks.
        const double switch_time = 1.0 / 20.0;
        const double interval = 1.0 / ticks_per_second;
        for (int k = 1; k <= ticks_per_second; ++k) {
            advance_to(clock, runner, switch_time + k * interval);
        }
        REQUIRE(runner.latest_snapshot().tick == 1 + static_cast<std::uint64_t>(ticks_per_second));
        runner.stop();
    }
}

TEST_CASE("runner: changing speed mid-run restarts the pace from now", "[runner]") {
    FakeClock clock;
    SimulationRunner runner(1, clock, {}, 1);
    runner.start();
    REQUIRE(runner.wait_until_idle());

    for (int k = 1; k <= 10; ++k) {
        advance_to(clock, runner, k * (1.0 / 20.0));
    }
    REQUIRE(runner.latest_snapshot().tick == 10);

    runner.submit(time::SetSpeed{3}); // consumed by the next tick, at t = 0.55
    REQUIRE(runner.wait_until_idle());
    advance_to(clock, runner, 0.55);
    REQUIRE(runner.latest_snapshot().tick == 11);
    REQUIRE(runner.latest_snapshot().speed == 3);
    for (int k = 1; k <= 8; ++k) { // 4x from t = 0.55
        advance_to(clock, runner, 0.55 + k * (1.0 / 80.0));
    }
    REQUIRE(runner.latest_snapshot().tick == 19);
}

TEST_CASE("runner: paused makes no progress but still applies commands", "[runner]") {
    FakeClock clock;
    SimulationRunner runner(1, clock, {}, 1);
    install_counter(runner);
    runner.start();
    REQUIRE(runner.wait_until_idle());

    for (int k = 1; k <= 10; ++k) {
        advance_to(clock, runner, k * (1.0 / 20.0));
    }
    REQUIRE(runner.latest_snapshot().tick == 10);

    // The pause is a command: it is consumed by the next tick, which still completes, then ticking stops.
    runner.submit(time::TogglePause{});
    advance_to(clock, runner, 11 * (1.0 / 20.0));
    REQUIRE(runner.latest_snapshot().tick == 11);
    REQUIRE(runner.latest_snapshot().paused);

    advance_to(clock, runner, 5.0); // a long stretch of fake time while paused
    REQUIRE(runner.latest_snapshot().tick == 11);

    runner.submit(set_counter(42));
    REQUIRE(runner.wait_until_idle());
    const RenderSnapshot while_paused = runner.latest_snapshot();
    REQUIRE(while_paused.tick == 11); // applied without advancing the tick
    REQUIRE(while_paused.paused);
    REQUIRE(while_paused.ui_events.size() == 1);
    REQUIRE(while_paused.ui_events[0].text == "counter=42");

    // Unpausing is applied immediately even while paused, and ticking resumes at the normal pace from
    // now, with no burst for the time spent paused.
    runner.submit(time::TogglePause{});
    REQUIRE(runner.wait_until_idle());
    REQUIRE_FALSE(runner.latest_snapshot().paused);
    REQUIRE(runner.latest_snapshot().tick == 11);
    advance_to(clock, runner, 5.0 + 1.0 / 20.0);
    REQUIRE(runner.latest_snapshot().tick == 12);

    // Commands submitted while running are applied by the next tick.
    runner.submit(set_counter(7));
    advance_to(clock, runner, 5.0 + 2.0 / 20.0);
    REQUIRE(runner.latest_snapshot().tick == 13);
    REQUIRE(runner.latest_snapshot().ui_events[0].text == "counter=7");
}

TEST_CASE("runner: catch-up is limited, then the backlog is dropped", "[runner]") {
    FakeClock clock;
    RunnerConfig config;
    config.max_catchup_ticks = 5;
    SimulationRunner runner(1, clock, config, 1);
    runner.start();
    REQUIRE(runner.wait_until_idle());

    // One fake second passes at once: 20 ticks are owed but only 5 run.
    advance_to(clock, runner, 1.0);
    REQUIRE(runner.latest_snapshot().tick == 5);

    // The rest of the backlog is gone: the next tick is one interval after the jump, not 15 owed ticks.
    advance_to(clock, runner, 1.0 + 0.5 * (1.0 / 20.0));
    REQUIRE(runner.latest_snapshot().tick == 5);
    advance_to(clock, runner, 1.0 + 1.0 / 20.0);
    REQUIRE(runner.latest_snapshot().tick == 6);
}

TEST_CASE("runner: a smaller catch-up limit is respected", "[runner]") {
    FakeClock clock;
    RunnerConfig config;
    config.max_catchup_ticks = 2;
    SimulationRunner runner(1, clock, config, 1);
    runner.start();
    REQUIRE(runner.wait_until_idle());
    advance_to(clock, runner, 10.0);
    REQUIRE(runner.latest_snapshot().tick == 2);
}

TEST_CASE("runner: latest_snapshot is the newest, with time, stats and the hook's data", "[runner]") {
    FakeClock clock;
    SimulationRunner runner(1, clock, {}, 1);
    install_counter(runner);
    runner.simulation().add_system(core::Phase::ApplyCommands,
                                   make_system("spawn", [](core::TickContext& context) {
                                       if (context.tick == 0) {
                                           for (int i = 0; i < 5; ++i) {
                                               context.world.create();
                                           }
                                       }
                                   }));
    runner.start();
    REQUIRE(runner.wait_until_idle());

    const double interval = 1.0 / 20.0;
    for (int k = 1; k <= 8; ++k) {
        advance_to(clock, runner, k * interval);
    }
    const RenderSnapshot snapshot = runner.latest_snapshot();
    REQUIRE(snapshot.tick == 8);
    REQUIRE(snapshot.published_at == 8 * interval);
    REQUIRE(snapshot.stats.entity_count == 5);
    REQUIRE(snapshot.ui_events.size() == 1); // reset every tick, not accumulated
    REQUIRE_THAT(snapshot.stats.ticks_per_second, Catch::Matchers::WithinRel(20.0, 1e-6));
    REQUIRE(snapshot.stats.last_tick_ms == 0.0); // a fake clock does not move during a tick
    REQUIRE(snapshot.stats.avg_tick_ms == 0.0);
}

TEST_CASE("runner: stop is clean in every state", "[runner]") {
    SECTION("while sleeping on the clock") {
        FakeClock clock;
        SimulationRunner runner(1, clock, {}, 1);
        runner.start();
        REQUIRE(runner.wait_until_idle());
        runner.stop();
        runner.stop(); // idempotent
    }
    SECTION("while paused") {
        FakeClock clock;
        SimulationRunner runner(1, clock, {}, 1);
        runner.start();
        REQUIRE(runner.wait_until_idle()); // let the schedule start at t = 0 before time moves
        runner.submit(time::TogglePause{});
        advance_to(clock, runner, 1.0 / 20.0); // the tick that consumes the pause
        REQUIRE(runner.latest_snapshot().paused);
        runner.stop();
    }
    SECTION("never started") {
        FakeClock clock;
        SimulationRunner runner(1, clock, {}, 1);
        runner.stop();
    }
    SECTION("by the destructor") {
        FakeClock clock;
        {
            SimulationRunner runner(1, clock, {}, 1);
            runner.start();
            REQUIRE(runner.wait_until_idle());
        } // no explicit stop(): must not hang or terminate
        SUCCEED();
    }
    SECTION("immediately after start") {
        FakeClock clock;
        SimulationRunner runner(1, clock, {}, 1);
        runner.start();
        runner.stop();
    }
}

TEST_CASE("runner: start twice and bad arguments are rejected", "[runner]") {
    FakeClock clock;
    SimulationRunner runner(1, clock, {}, 1);
    runner.start();
    REQUIRE_THROWS_AS(runner.start(), std::logic_error);
    runner.stop();

    RunnerConfig bad;
    bad.time.speed_multipliers[2] = 0;
    REQUIRE_THROWS_AS(SimulationRunner(1, clock, bad, 1), std::invalid_argument);
    RunnerConfig no_rate;
    no_rate.time.base_tick_rate = 0;
    REQUIRE_THROWS_AS(SimulationRunner(1, clock, no_rate, 1), std::invalid_argument);
    RunnerConfig no_catchup;
    no_catchup.max_catchup_ticks = 0;
    REQUIRE_THROWS_AS(SimulationRunner(1, clock, no_catchup, 1), std::invalid_argument);
}

TEST_CASE("runner: a throwing system stops the thread and reports the failure", "[runner]") {
    FakeClock clock;
    SimulationRunner runner(1, clock, {}, 1);
    runner.simulation().add_system(core::Phase::Update, make_system("boom", [](core::TickContext& context) {
                                       if (context.tick == 2) {
                                           throw std::runtime_error("kaboom");
                                       }
                                   }));
    runner.start();
    REQUIRE(runner.wait_until_idle());
    for (int k = 1; k <= 5; ++k) {
        clock.set_time(k * (1.0 / 20.0));
        if (!runner.failure().empty()) {
            break;
        }
        runner.wait_until_idle();
    }
    REQUIRE(runner.wait_until_idle()); // a stopped runner counts as idle
    REQUIRE(runner.failure() == "kaboom");
    runner.stop();
}

TEST_CASE("runner: the result matches stepping the same simulation directly", "[runner][determinism]") {
    const auto make_sim_systems = [](core::Simulation& sim) {
        core::Hooks<Counter> hooks;
        hooks.hash = [](core::StateHasher& hasher, const Counter& c) { hasher.add_pod(c.value); };
        sim.world().registry().register_type<Counter>("test.Counter", hooks);
        sim.add_system(core::Phase::ApplyCommands, make_system("spawn", [](core::TickContext& context) {
                           if (context.tick == 0) {
                               context.world.emplace<Counter>(context.world.create());
                           }
                       }));
        sim.add_system(core::Phase::Update, make_system("bump", [](core::TickContext& context) {
                           for (const auto entity : context.world.view<Counter>()) {
                               context.world.get<Counter>(entity).value +=
                                   static_cast<int>(citysim::rng::uniform_int(context.rng.at(1, 2), 0, 9));
                           }
                       }));
    };

    core::Simulation direct(77, 1);
    time::install(direct); // the runner installs it too, so both worlds hold the same state
    make_sim_systems(direct);
    for (int i = 0; i < 40; ++i) {
        direct.step();
    }

    FakeClock clock;
    SimulationRunner runner(77, clock, {}, 1);
    make_sim_systems(runner.simulation());
    runner.start();
    REQUIRE(runner.wait_until_idle());
    for (int k = 1; k <= 40; ++k) {
        advance_to(clock, runner, k * (1.0 / 20.0));
    }
    runner.stop();

    REQUIRE(runner.simulation().tick() == 40);
    REQUIRE(runner.simulation().world().state_hash() == direct.world().state_hash());
}

TEST_CASE("runner: many commands from several threads while ticking are all applied once",
          "[runner][threads]") {
    FakeClock clock;
    SimulationRunner runner(1, clock, {}, 2);
    std::atomic<int> applied{0};
    runner.simulation().dispatcher().register_handler<core::DebugSetComponentValue>(
        [&](core::TickContext&, const core::DebugSetComponentValue&) { applied.fetch_add(1); });
    runner.start();
    REQUIRE(runner.wait_until_idle());

    std::vector<std::thread> producers;
    for (int t = 0; t < 4; ++t) {
        producers.emplace_back([&] {
            for (int i = 0; i < 500; ++i) {
                runner.submit(set_counter(i));
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }
    for (int k = 1; k <= 3; ++k) { // a few ticks to flush the queue
        advance_to(clock, runner, k * (1.0 / 20.0));
    }
    runner.stop();
    REQUIRE(applied.load() == 2000);
    REQUIRE(runner.simulation().input_log().entries().size() == 2000);
}

// The only tests that use real time; the bounds are deliberately loose so a busy machine cannot fail them.
TEST_CASE("SteadyClock: monotonic, sleeps until the deadline, and can be interrupted", "[runner][realtime]") {
    runner::SteadyClock clock;
    const double start = clock.now_seconds();
    REQUIRE(clock.now_seconds() >= start);

    clock.sleep_until(start + 0.02);
    REQUIRE(clock.now_seconds() >= start + 0.02);

    // An interrupt that arrives before the sleep still wakes it (sticky), once.
    clock.interrupt();
    const double before = clock.now_seconds();
    clock.sleep_until(before + 30.0);
    REQUIRE(clock.now_seconds() < before + 5.0);

    // And one that arrives during the sleep.
    std::thread waker([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        clock.interrupt();
    });
    const double sleep_start = clock.now_seconds();
    clock.sleep_until(sleep_start + 30.0);
    waker.join();
    REQUIRE(clock.now_seconds() < sleep_start + 5.0);
}

TEST_CASE("runner: with the real clock it ticks, and stops promptly", "[runner][realtime]") {
    runner::SteadyClock clock;
    SimulationRunner runner(1, clock, {}, 1);
    runner.submit(time::SetSpeed{3}); // 80 ticks per second once the first tick applies it
    runner.start();

    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (runner.latest_snapshot().tick < 5 && std::chrono::steady_clock::now() < give_up) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(runner.latest_snapshot().tick >= 5);

    const auto stop_started = std::chrono::steady_clock::now();
    runner.stop();
    REQUIRE(std::chrono::steady_clock::now() - stop_started < std::chrono::seconds(5));
    REQUIRE(runner.failure().empty());
}
