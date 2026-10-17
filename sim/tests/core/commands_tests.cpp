#include "citysim/core/command_queue.hpp"
#include "citysim/core/simulation.hpp"
#include "support/fn_system.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

using namespace citysim::core;
using citysim::test::make_system;

namespace {
Command set_value(std::uint32_t entity, std::int32_t value) {
    return DebugSetComponentValue{entity, value};
}

// Records every DebugSetComponentValue the simulation applies, in application order.
struct Applied {
    std::vector<std::pair<std::uint32_t, std::int32_t>> values;
};

void record_applied(Simulation& sim, Applied& applied) {
    sim.dispatcher().register_handler<DebugSetComponentValue>(
        [&applied](TickContext&, const DebugSetComponentValue& cmd) {
            applied.values.emplace_back(cmd.entity, cmd.value);
        });
}
} // namespace

TEST_CASE("CommandQueue assigns increasing sequence numbers", "[core][commands]") {
    CommandQueue queue;
    REQUIRE(queue.submit(NoOp{}) == 1);
    REQUIRE(queue.submit(set_value(1, 2), 9) == 2);
    REQUIRE(queue.size() == 2);

    std::vector<SequencedCommand> out;
    queue.drain(out);
    REQUIRE(queue.size() == 0);
    REQUIRE(out.size() == 2);
    REQUIRE(out[0].sequence == 1);
    REQUIRE(out[1].sequence == 2);
    REQUIRE(out[1].issue_tick == 9);
    REQUIRE(std::get<DebugSetComponentValue>(out[1].cmd).value == 2);

    // Numbering continues after a drain, and drain appends rather than replaces.
    REQUIRE(queue.submit(NoOp{}) == 3);
    queue.drain(out);
    REQUIRE(out.size() == 3);
    REQUIRE(out[2].sequence == 3);
}

TEST_CASE("simulation applies commands in sequence order, whatever order they arrive in",
          "[core][commands]") {
    Simulation sim(1);
    Applied applied;
    record_applied(sim, applied);

    sim.enqueue({SequencedCommand{3, 0, set_value(0, 30)}, SequencedCommand{1, 0, set_value(0, 10)}});
    sim.enqueue({SequencedCommand{2, 0, set_value(0, 20)}});
    REQUIRE(sim.pending_command_count() == 3);
    REQUIRE(applied.values.empty()); // nothing happens until a tick runs

    sim.step();
    REQUIRE(sim.pending_command_count() == 0);
    REQUIRE(applied.values.size() == 3);
    REQUIRE(applied.values[0].second == 10);
    REQUIRE(applied.values[1].second == 20);
    REQUIRE(applied.values[2].second == 30);

    sim.step(); // applied exactly once
    REQUIRE(applied.values.size() == 3);
}

TEST_CASE("commands are applied before the systems of the ApplyCommands phase", "[core][commands]") {
    Simulation sim(1);
    int last_seen = 0;
    sim.dispatcher().register_handler<DebugSetComponentValue>(
        [&](TickContext&, const DebugSetComponentValue& cmd) { last_seen = cmd.value; });
    int seen_by_system = -1;
    sim.add_system(Phase::ApplyCommands,
                   make_system("observer", [&](TickContext&) { seen_by_system = last_seen; }));

    sim.enqueue({SequencedCommand{1, 0, set_value(0, 42)}});
    sim.step();
    REQUIRE(seen_by_system == 42);
}

TEST_CASE("handlers can change the world through the TickContext", "[core][commands]") {
    struct Counter {
        int value = 0;
    };
    Simulation sim(1);
    sim.world().ctx().emplace<Counter>();
    sim.dispatcher().register_handler<DebugSetComponentValue>(
        [](TickContext& context, const DebugSetComponentValue& cmd) {
            context.world.ctx().get<Counter>().value = cmd.value;
        });
    sim.enqueue({SequencedCommand{1, 0, set_value(0, 7)}});
    sim.step();
    REQUIRE(sim.world().ctx().get<Counter>().value == 7);
}

TEST_CASE("unhandled commands are ignored without disturbing the rest", "[core][commands]") {
    Simulation sim(1);
    Applied applied;
    record_applied(sim, applied);

    // NoOp has no handler.
    sim.enqueue({SequencedCommand{1, 0, NoOp{}}, SequencedCommand{2, 0, set_value(5, 50)},
                 SequencedCommand{3, 0, NoOp{}}});
    REQUIRE_NOTHROW(sim.step());
    REQUIRE(applied.values.size() == 1);
    REQUIRE(applied.values[0].first == 5);
}

TEST_CASE("registering two handlers for one command type is an error", "[core][commands]") {
    Simulation sim(1);
    sim.dispatcher().register_handler<NoOp>([](TickContext&, const NoOp&) {});
    REQUIRE_THROWS_AS(sim.dispatcher().register_handler<NoOp>([](TickContext&, const NoOp&) {}),
                      std::logic_error);
    // A different type is fine.
    REQUIRE_NOTHROW(sim.dispatcher().register_handler<DebugSetComponentValue>(
        [](TickContext&, const DebugSetComponentValue&) {}));
}

TEST_CASE("the input log records tick, sequence and command for everything consumed", "[core][commands]") {
    Simulation sim(1);
    Applied applied;
    record_applied(sim, applied);

    sim.step(); // tick 0: nothing
    sim.enqueue({SequencedCommand{2, 0, NoOp{}}, SequencedCommand{1, 0, set_value(4, -3)}});
    sim.step(); // consumed during tick 1
    sim.enqueue({SequencedCommand{3, 1, set_value(4, 8)}});
    sim.step();

    const auto& entries = sim.input_log().entries();
    REQUIRE(entries.size() == 3);
    REQUIRE(entries[0].tick == 1);
    REQUIRE(entries[0].sequence == 1);
    REQUIRE(std::get<DebugSetComponentValue>(entries[0].cmd).value == -3);
    REQUIRE(entries[1].sequence == 2); // unhandled NoOp is still logged
    REQUIRE(std::holds_alternative<NoOp>(entries[1].cmd));
    REQUIRE(entries[2].tick == 2);
    REQUIRE(entries[2].sequence == 3);

    std::ostringstream dump;
    sim.input_log().dump(dump);
    REQUIRE(dump.str() == "1 1 DebugSetComponentValue entity=4 value=-3\n"
                          "1 2 NoOp\n"
                          "2 3 DebugSetComponentValue entity=4 value=8\n");
}

TEST_CASE("many producer threads: every command is applied exactly once, in sequence order",
          "[core][commands][threads]") {
    constexpr std::uint32_t k_threads = 4;
    constexpr std::int32_t k_per_thread = 1000;
    constexpr std::size_t k_total = k_threads * static_cast<std::size_t>(k_per_thread);

    CommandQueue queue;
    Simulation sim(1);
    Applied applied;
    record_applied(sim, applied);

    std::atomic<std::uint32_t> producers_done{0};
    std::vector<std::thread> producers;
    for (std::uint32_t t = 0; t < k_threads; ++t) {
        producers.emplace_back([&queue, &producers_done, t] {
            for (std::int32_t i = 0; i < k_per_thread; ++i) {
                queue.submit(set_value(t, i));
            }
            ++producers_done;
        });
    }

    // The simulation side drains and steps while producers are still running.
    std::vector<SequencedCommand> batch;
    std::vector<std::uint64_t> drained_sequences;
    while (producers_done.load() < k_threads || queue.size() > 0) {
        batch.clear();
        queue.drain(batch);
        for (const auto& c : batch) {
            drained_sequences.push_back(c.sequence);
        }
        sim.enqueue(batch);
        sim.step();
    }
    for (auto& thread : producers) {
        thread.join();
    }
    batch.clear();
    queue.drain(batch); // anything submitted between the last check and the joins
    for (const auto& c : batch) {
        drained_sequences.push_back(c.sequence);
    }
    sim.enqueue(batch);
    sim.step();

    // Exactly once: 1..N with no gaps or repeats, and drained in order.
    REQUIRE(drained_sequences.size() == k_total);
    for (std::size_t i = 0; i < drained_sequences.size(); ++i) {
        REQUIRE(drained_sequences[i] == i + 1);
    }

    REQUIRE(applied.values.size() == k_total);
    REQUIRE(sim.input_log().entries().size() == k_total);
    for (std::size_t i = 0; i < k_total; ++i) {
        REQUIRE(sim.input_log().entries()[i].sequence == i + 1); // applied in sequence order
    }

    // Each producer's own commands keep their submission order, and none is lost or duplicated.
    std::vector<std::int32_t> next(k_threads, 0);
    for (const auto& [entity, value] : applied.values) {
        REQUIRE(value == next[entity]);
        ++next[entity];
    }
    for (const auto n : next) {
        REQUIRE(n == k_per_thread);
    }
}
