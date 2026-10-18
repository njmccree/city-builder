#pragma once

#include <array>
#include <cstdint>

namespace citysim::time {

// Tunable time constants (architecture §6). These are the built-in defaults; content loading fills the
// struct from defines/time.json later. They are part of the hashed state, so two worlds with different
// defines never compare equal.
struct TimeDefines {
    std::uint32_t base_tick_rate = 20; // ticks per real second at 1x
    // Tick-rate multipliers by speed index. Index 0 is "paused" and never used as a speed; 1..3 are 1x/2x/4x.
    std::array<std::uint32_t, 4> speed_multipliers{0, 1, 2, 4};
    float movement_scale = 3.0F;            // simulated seconds of movement per tick = this / base_tick_rate
    std::uint32_t day_length_ticks = 14400; // one in-game day (12 real minutes at 1x)

    // Autosave (architecture §7.1, §10) runs on REAL time, measured by the runner's clock, never by the
    // simulation. These two are settings, not game state: they are not hashed and not saved, so a loaded
    // game keeps the ones the running build was configured with.
    std::uint32_t autosave_interval_real_seconds = 600; // 0 turns autosave off
    std::uint32_t autosave_slots = 3;                   // rotating files autosave_1 .. autosave_<slots>
};

} // namespace citysim::time
