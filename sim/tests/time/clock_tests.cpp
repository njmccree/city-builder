#include "citysim/core/simulation.hpp"
#include "citysim/time/game_clock.hpp"
#include "citysim/time/time_commands.hpp"
#include "citysim/time/time_module.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <stdexcept>

using namespace citysim;
using time::GameClock;
using time::TimeDefines;

namespace {
const GameClock& clock_of(const core::Simulation& sim) {
    return sim.world().ctx().get<GameClock>();
}

core::SequencedCommand command(std::uint64_t sequence, core::Command cmd) {
    return core::SequencedCommand{sequence, 0, std::move(cmd)};
}
} // namespace

TEST_CASE("time defaults match the architecture time model", "[time][clock]") {
    const TimeDefines defines;
    REQUIRE(defines.base_tick_rate == 20);
    REQUIRE(defines.speed_multipliers == std::array<std::uint32_t, 4>{0, 1, 2, 4});
    REQUIRE(defines.movement_scale == 3.0F);
    REQUIRE(defines.day_length_ticks == 14400);
    REQUIRE(time::movement_seconds_per_tick(defines) == 0.15F);
}

TEST_CASE("the day wraps exactly at day_length_ticks", "[time][clock]") {
    TimeDefines defines;
    defines.day_length_ticks = 100;
    core::Simulation sim(1, 1);
    time::install(sim, defines);

    for (int i = 0; i < 99; ++i) {
        sim.step();
    }
    REQUIRE(clock_of(sim).day_tick == 99);
    REQUIRE(clock_of(sim).day_index == 0);

    sim.step();
    REQUIRE(clock_of(sim).day_tick == 0);
    REQUIRE(clock_of(sim).day_index == 1);

    for (int i = 0; i < 250; ++i) {
        sim.step();
    }
    REQUIRE(clock_of(sim).day_index == 3);
    REQUIRE(clock_of(sim).day_tick == 50);
}

TEST_CASE("hour, minute and time of day are exact integer math", "[time][clock]") {
    TimeDefines defines;
    defines.day_length_ticks = 1440; // one tick per in-game minute
    GameClock clock;

    const auto at = [&](std::uint64_t tick) {
        clock.day_tick = tick;
        return std::pair{time::hour(clock, defines), time::minute(clock, defines)};
    };
    REQUIRE(at(0) == std::pair<std::uint32_t, std::uint32_t>{0, 0});
    REQUIRE(at(59) == std::pair<std::uint32_t, std::uint32_t>{0, 59});
    REQUIRE(at(60) == std::pair<std::uint32_t, std::uint32_t>{1, 0});
    REQUIRE(at(754) == std::pair<std::uint32_t, std::uint32_t>{12, 34});
    REQUIRE(at(1439) == std::pair<std::uint32_t, std::uint32_t>{23, 59});

    clock.day_tick = 720;
    REQUIRE(time::time_of_day_fraction(clock, defines) == 0.5);

    // The default day: 14,400 ticks, so 600 ticks per hour and 10 per minute.
    const TimeDefines normal;
    clock.day_tick = 600 * 7 + 10 * 25;
    REQUIRE(time::hour(clock, normal) == 7);
    REQUIRE(time::minute(clock, normal) == 25);
}

TEST_CASE("ticks per second follow the speed", "[time][clock]") {
    const TimeDefines defines;
    GameClock clock;
    clock.speed = 1;
    REQUIRE(time::ticks_per_second(clock, defines) == 20);
    clock.speed = 2;
    REQUIRE(time::ticks_per_second(clock, defines) == 40);
    clock.speed = 3;
    REQUIRE(time::ticks_per_second(clock, defines) == 80);
}

TEST_CASE("SetSpeed and TogglePause commands change the clock", "[time][clock]") {
    core::Simulation sim(1, 1);
    time::install(sim);
    REQUIRE(clock_of(sim).speed == 1);
    REQUIRE_FALSE(clock_of(sim).paused);

    sim.enqueue({command(1, time::SetSpeed{3})});
    sim.step();
    REQUIRE(clock_of(sim).speed == 3);

    sim.enqueue({command(2, time::TogglePause{})});
    sim.step();
    REQUIRE(clock_of(sim).paused);
    sim.enqueue({command(3, time::TogglePause{})});
    sim.step();
    REQUIRE_FALSE(clock_of(sim).paused);

    // Out-of-range speeds are ignored, not clamped.
    sim.enqueue(
        {command(4, time::SetSpeed{0}), command(5, time::SetSpeed{4}), command(6, time::SetSpeed{200})});
    sim.step();
    REQUIRE(clock_of(sim).speed == 3);

    sim.enqueue({command(7, time::SetSpeed{2})});
    sim.step();
    REQUIRE(clock_of(sim).speed == 2);
}

TEST_CASE("SetTimeScale changes movement and day length, and ignores bad values", "[time][clock]") {
    core::Simulation sim(1, 1);
    time::install(sim);
    const auto defines = [&sim]() -> const time::TimeDefines& {
        return sim.world().ctx().get<TimeDefines>();
    };
    REQUIRE(defines().movement_scale == 3.0F);
    REQUIRE(defines().day_length_ticks == 14400);

    sim.enqueue({command(1, time::SetTimeScale{6.0F, 28800})});
    sim.step();
    CHECK(defines().movement_scale == 6.0F);
    CHECK(defines().day_length_ticks == 28800);
    CHECK(time::movement_seconds_per_tick(defines()) == 6.0F / 20.0F);

    // Not above 0, too big, or a day too short to have 24 ticks: ignored, nothing changes.
    sim.enqueue({command(2, time::SetTimeScale{0.0F, 28800}), command(3, time::SetTimeScale{-1.0F, 28800}),
                 command(4, time::SetTimeScale{500.0F, 28800}), command(5, time::SetTimeScale{6.0F, 10}),
                 command(6, time::SetTimeScale{std::nanf(""), 28800})});
    sim.step();
    CHECK(defines().movement_scale == 6.0F);
    CHECK(defines().day_length_ticks == 28800);

    // Shortening the day never leaves the clock past its end.
    for (int i = 0; i < 500; ++i) {
        sim.step();
    }
    REQUIRE(clock_of(sim).day_tick > 100);
    sim.enqueue({command(7, time::SetTimeScale{6.0F, 100})});
    sim.step();
    CHECK(clock_of(sim).day_tick < 100);
}

TEST_CASE("the time scale is part of the state hash", "[time][clock][hash]") {
    const auto hash_after = [](float scale, std::uint32_t day_ticks) {
        core::Simulation sim(1, 1);
        time::install(sim);
        sim.enqueue({command(1, time::SetTimeScale{scale, day_ticks})});
        sim.step();
        return sim.world().state_hash();
    };
    CHECK(hash_after(3.0F, 14400) == hash_after(3.0F, 14400));
    CHECK(hash_after(3.0F, 14400) != hash_after(6.0F, 14400));
    CHECK(hash_after(3.0F, 14400) != hash_after(3.0F, 7200));
}

TEST_CASE("clock state is part of the state hash", "[time][clock][hash]") {
    const auto hash_after = [](std::vector<core::Command> commands) {
        core::Simulation sim(1, 1);
        time::install(sim);
        std::uint64_t sequence = 1;
        for (auto& cmd : commands) {
            sim.enqueue({command(sequence++, std::move(cmd))});
        }
        sim.step();
        return sim.world().state_hash();
    };
    const auto base = hash_after({});
    REQUIRE(hash_after({}) == base);
    REQUIRE(hash_after({time::SetSpeed{2}}) != base);
    REQUIRE(hash_after({time::TogglePause{}}) != base);
    REQUIRE(hash_after({time::SetSpeed{2}}) != hash_after({time::SetSpeed{3}}));

    // The defines are hashed too, so differently tuned worlds never compare equal.
    TimeDefines other;
    other.day_length_ticks = 1000;
    core::Simulation a(1, 1);
    core::Simulation b(1, 1);
    time::install(a);
    time::install(b, other);
    REQUIRE(a.world().state_hash() != b.world().state_hash());
}

TEST_CASE("install rejects unusable defines", "[time][clock]") {
    core::Simulation sim(1, 1);
    TimeDefines zero_rate;
    zero_rate.base_tick_rate = 0;
    REQUIRE_THROWS_AS(time::install(sim, zero_rate), std::invalid_argument);

    TimeDefines zero_day;
    zero_day.day_length_ticks = 0;
    REQUIRE_THROWS_AS(time::install(sim, zero_day), std::invalid_argument);

    TimeDefines zero_speed;
    zero_speed.speed_multipliers[3] = 0;
    REQUIRE_THROWS_AS(time::install(sim, zero_speed), std::invalid_argument);

    REQUIRE(sim.scheduler().system_count() == 0); // a rejected install leaves nothing behind
}
