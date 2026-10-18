#include "citysim/core/rng.hpp"
#include "citysim/core/state_hasher.hpp"
#include "support/determinism.hpp"
#include "support/fn_system.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace citysim::core;
using citysim::test::expect_same_hashes;
using citysim::test::first_divergence;
using citysim::test::make_system;
using citysim::test::run_scenario;
using citysim::test::Script;

namespace {
struct Slot {
    std::int32_t value = 0;
};

void hash_slot(StateHasher& hasher, const Slot& slot) {
    hasher.add_pod(slot.value);
}

// Ten slots that drift randomly each tick; a command overwrites one slot's value.
void setup(Simulation& sim) {
    Hooks<Slot> hooks;
    hooks.hash = &hash_slot;
    sim.world().registry().register_type<Slot>("test.Slot", hooks);

    sim.add_system(Phase::ApplyCommands, make_system("spawn", [](TickContext& context) {
                       if (context.tick == 0) {
                           for (int i = 0; i < 10; ++i) {
                               context.world.emplace<Slot>(context.world.create());
                           }
                       }
                   }));
    sim.add_system(Phase::Update, make_system("drift", [](TickContext& context) {
                       const auto stream = citysim::rng::stream("test.drift");
                       for (const auto entity : context.world.view<Slot>()) {
                           const auto h = context.rng.at(stream, entt::to_integral(entity));
                           context.world.get<Slot>(entity).value +=
                               static_cast<std::int32_t>(citysim::rng::uniform_int(h, -3, 3));
                       }
                   }));
    sim.dispatcher().register_handler<DebugSetComponentValue>(
        [](TickContext& context, const DebugSetComponentValue& cmd) {
            for (const auto entity : context.world.view<Slot>()) {
                if (entt::to_entity(entity) == cmd.entity) {
                    context.world.get<Slot>(entity).value = cmd.value;
                }
            }
        });
}

Script script_with(std::int32_t value_at_50) {
    return Script{
        {10, {DebugSetComponentValue{2, 5}}},
        {30, {DebugSetComponentValue{7, -9}, NoOp{}}},
        {50, {DebugSetComponentValue{4, value_at_50}}},
    };
}
} // namespace

TEST_CASE("the same scenario run twice matches at every tick", "[core][determinism][harness]") {
    const auto a = run_scenario(7, 100, setup, script_with(100));
    const auto b = run_scenario(7, 100, setup, script_with(100));
    REQUIRE(a.size() == 100);
    expect_same_hashes(a, b);
}

TEST_CASE("a different command at tick 50 diverges from tick 50 on", "[core][determinism][harness]") {
    const auto a = run_scenario(7, 100, setup, script_with(100));
    const auto b = run_scenario(7, 100, setup, script_with(101));

    // Identical up to and including the step before the command is applied...
    for (std::size_t i = 0; i < 50; ++i) {
        REQUIRE(a[i] == b[i]);
    }
    // ...and different from the step that applies it (tick() == 51 afterwards) onward.
    REQUIRE(first_divergence(a, b) == std::optional<std::size_t>(50));
    for (std::size_t i = 50; i < a.size(); ++i) {
        REQUIRE(a[i] != b[i]);
    }
}

TEST_CASE("first_divergence reports lengths and mismatches", "[core][determinism][harness]") {
    REQUIRE_FALSE(first_divergence({1, 2, 3}, {1, 2, 3}).has_value());
    REQUIRE(first_divergence({1, 2, 3}, {1, 9, 3}) == std::optional<std::size_t>(1));
    REQUIRE(first_divergence({1, 2}, {1, 2, 3}) == std::optional<std::size_t>(2)); // shorter run
    REQUIRE(first_divergence({}, {}).has_value() == false);
}

TEST_CASE("a different seed diverges from the very first tick", "[core][determinism][harness]") {
    const auto a = run_scenario(1, 20, setup);
    const auto b = run_scenario(2, 20, setup);
    // Tick 0 spawns identical entities before any random drift is visible only if drift ran: it does.
    REQUIRE(first_divergence(a, b).has_value());
    REQUIRE(*first_divergence(a, b) <= 1);
}
