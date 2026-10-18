#include "citysim/core/simulation.hpp"
#include "support/fn_system.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

using namespace citysim::core;
using citysim::test::FnSystem;
using citysim::test::make_system;

namespace {
using Log = std::vector<std::string>;

std::unique_ptr<ISystem> logging_system(std::string name, Log& log) {
    return make_system(name, [name, &log](TickContext&) { log.push_back(name); });
}
} // namespace

TEST_CASE("phases run in the fixed order regardless of registration order", "[core][sim]") {
    Simulation sim(1);
    Log log;
    // Registered in scrambled order on purpose.
    sim.add_system(Phase::Publish, logging_system("publish", log));
    sim.add_system(Phase::Update, logging_system("update", log));
    sim.add_system(Phase::ApplyCommands, logging_system("apply", log));
    sim.add_system(Phase::Calendar, logging_system("calendar", log));
    sim.add_system(Phase::PreUpdate, logging_system("pre", log));
    sim.add_system(Phase::PostUpdate, logging_system("post", log));

    sim.step();
    REQUIRE(log == Log{"apply", "pre", "update", "post", "calendar", "publish"});
}

TEST_CASE("systems in a phase run in registration order", "[core][sim]") {
    Simulation sim(1);
    Log log;
    sim.add_system(Phase::Update, logging_system("zebra", log));
    sim.add_system(Phase::Update, logging_system("apple", log)); // not sorted by name
    sim.add_system(Phase::Update, logging_system("mango", log));

    sim.step();
    sim.step();
    REQUIRE(log == Log{"zebra", "apple", "mango", "zebra", "apple", "mango"});
}

TEST_CASE("duplicate system names are rejected across phases", "[core][sim]") {
    Simulation sim(1);
    Log log;
    sim.add_system(Phase::Update, logging_system("same", log));
    REQUIRE_THROWS_AS(sim.add_system(Phase::Publish, logging_system("same", log)), std::logic_error);
    REQUIRE_THROWS_AS(sim.add_system(Phase::Update, logging_system("same", log)), std::logic_error);
    REQUIRE_THROWS_AS(sim.add_system(Phase::Update, nullptr), std::logic_error);
    REQUIRE(sim.scheduler().system_count() == 1);
}

TEST_CASE("on_register runs once, and not for rejected systems", "[core][sim]") {
    Simulation sim(1);
    int registered = 0;
    auto first = make_system("a", [](TickContext&) {});
    first->on_register_fn = [&](World&) { ++registered; };
    sim.add_system(Phase::Update, std::move(first));
    REQUIRE(registered == 1);

    auto duplicate = make_system("a", [](TickContext&) {});
    duplicate->on_register_fn = [&](World&) { ++registered; };
    REQUIRE_THROWS_AS(sim.add_system(Phase::Update, std::move(duplicate)), std::logic_error);
    REQUIRE(registered == 1);
}

TEST_CASE("tick increments once per step and is stable within a step", "[core][sim]") {
    Simulation sim(5);
    std::vector<std::uint64_t> seen;
    for (const Phase phase : k_all_phases) {
        sim.add_system(phase, make_system(std::string(phase_name(phase)),
                                          [&](TickContext& context) { seen.push_back(context.tick); }));
    }

    REQUIRE(sim.tick() == 0);
    sim.step();
    REQUIRE(sim.tick() == 1);
    sim.step();
    REQUIRE(sim.tick() == 2);

    REQUIRE(seen.size() == 12);
    for (std::size_t i = 0; i < seen.size(); ++i) {
        REQUIRE(seen[i] == i / 6); // all six phases of a step see the same tick
    }
}

TEST_CASE("TickContext carries the seed, tick and world", "[core][sim]") {
    Simulation sim(777);
    World* seen_world = nullptr;
    std::uint64_t seen_hash = 0;
    sim.add_system(Phase::Update, make_system("probe", [&](TickContext& context) {
                       seen_world = &context.world;
                       seen_hash = context.rng.at(1, 2, 3);
                       REQUIRE(&context.jobs == &sim.jobs());
                   }));
    sim.step();
    sim.step();
    REQUIRE(seen_world == &sim.world());
    REQUIRE(seen_hash == citysim::rng::hash64(777, 1, 1, 2, 3)); // second step: tick 1
}

TEST_CASE("events are readable from the next phase onward and dropped at tick end", "[core][sim]") {
    Simulation sim(1);
    std::vector<std::string> trace;
    const auto count_events = [](TickContext& context) { return context.events.readable().size(); };

    sim.add_system(Phase::ApplyCommands, make_system("apply", [&](TickContext& context) {
                       trace.push_back("apply sees " + std::to_string(count_events(context)));
                       context.events.emit(DebugEvent{"from-apply"});
                   }));
    sim.add_system(Phase::Update, make_system("update-a", [&](TickContext& context) {
                       trace.push_back("update-a sees " + std::to_string(count_events(context)));
                       context.events.emit(DebugEvent{"from-update"});
                       // An event emitted in this phase is not readable in this phase.
                       trace.push_back("update-a after emit sees " + std::to_string(count_events(context)));
                   }));
    sim.add_system(Phase::Update, make_system("update-b", [&](TickContext& context) {
                       // Registered after update-a, same phase: still must not see update-a's event.
                       trace.push_back("update-b sees " + std::to_string(count_events(context)));
                   }));
    sim.add_system(Phase::PostUpdate, make_system("post", [&](TickContext& context) {
                       const auto events = context.events.readable();
                       trace.push_back("post sees " + std::to_string(events.size()));
                       REQUIRE(std::get<DebugEvent>(events[0]).text == "from-apply"); // emission order
                       REQUIRE(std::get<DebugEvent>(events[1]).text == "from-update");
                   }));

    sim.step();
    REQUIRE(trace == std::vector<std::string>{"apply sees 0", "update-a sees 1", "update-a after emit sees 1",
                                              "update-b sees 1", "post sees 2"});

    // Dropped at the end of the tick: the next tick starts empty.
    trace.clear();
    sim.step();
    REQUIRE(trace.front() == "apply sees 0");
    REQUIRE(sim.events().readable().empty());
}

TEST_CASE("an observer is called around every system in order", "[core][sim]") {
    struct Recorder : SystemObserver {
        Log log;
        void before_system(Phase phase, std::string_view name) override {
            log.push_back("before " + std::string(phase_name(phase)) + ":" + std::string(name));
        }
        void after_system(Phase phase, std::string_view name) override {
            log.push_back("after " + std::string(phase_name(phase)) + ":" + std::string(name));
        }
    } recorder;

    Simulation sim(1);
    sim.set_observer(&recorder);
    Log inner;
    sim.add_system(Phase::Update, make_system("one", [&](TickContext&) { inner.push_back("one"); }));
    sim.add_system(Phase::PostUpdate, make_system("two", [&](TickContext&) { inner.push_back("two"); }));
    sim.step();

    REQUIRE(recorder.log ==
            Log{"before Update:one", "after Update:one", "before PostUpdate:two", "after PostUpdate:two"});

    sim.set_observer(nullptr);
    recorder.log.clear();
    sim.step();
    REQUIRE(recorder.log.empty());
}

TEST_CASE("parallel_for with a single worker runs chunks serially in order", "[core][sim]") {
    Simulation sim(1, 1); // one worker: no helper threads, so chunk order is the call order
    std::vector<std::array<std::size_t, 3>> chunks;
    sim.add_system(
        Phase::Update, make_system("chunker", [&](TickContext& context) {
            context.parallel_for(10, 4, [&](std::size_t begin, std::size_t end, std::size_t index) {
                chunks.push_back({begin, end, index});
            });
            context.parallel_for(0, 4, [&](std::size_t, std::size_t, std::size_t) { FAIL("empty range"); });
        }));
    sim.step();
    REQUIRE(chunks == std::vector<std::array<std::size_t, 3>>{{0, 4, 0}, {4, 8, 1}, {8, 10, 2}});
}
