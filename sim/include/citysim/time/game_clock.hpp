#pragma once

#include "citysim/time/time_defines.hpp"

#include <cstdint>

namespace citysim::time {

// The player-controlled clock and the daily cycle (architecture §6). Singleton state in the World context,
// registered with the ComponentRegistry so it is hashed and, later, saved.
//
// Runs on the TICK clock: ClockSystem advances it once per tick. The calendar is a separate, faster clock.
struct GameClock {
    std::uint8_t speed = 1; // index into TimeDefines::speed_multipliers, 1..3
    bool paused = false;
    std::uint64_t day_tick = 0;  // ticks since the current day began, 0 <= day_tick < day_length_ticks
    std::uint64_t day_index = 0; // whole days elapsed since the game began
};

// Fraction of the day elapsed, in [0, 1).
double time_of_day_fraction(const GameClock& clock, const TimeDefines& defines);

// Hour 0..23 and minute 0..59 of the in-game day. Exact integer math, so it never drifts or rounds oddly.
std::uint32_t hour(const GameClock& clock, const TimeDefines& defines);
std::uint32_t minute(const GameClock& clock, const TimeDefines& defines);

// Ticks per real second at the current speed (0 while paused is the caller's business: this ignores
// `paused`).
std::uint32_t ticks_per_second(const GameClock& clock, const TimeDefines& defines);

// Simulated seconds of agent movement covered by one tick: movement_scale / base_tick_rate.
float movement_seconds_per_tick(const TimeDefines& defines);

} // namespace citysim::time
