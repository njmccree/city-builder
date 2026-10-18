#pragma once

#include <cstdint>
#include <string>

namespace citysim::time {

// Plain command structs for the game clock. They are listed in the central `Command` variant in
// citysim/core/commands.hpp; the handlers are installed by time::install().

// Choose a speed: 1, 2 or 3 (1x, 2x, 4x). Anything else is ignored with a warning.
struct SetSpeed {
    std::uint8_t speed = 1;
};

struct TogglePause {};

// Switch the calendar to another era's cadence, by content id (e.g. "base:era.modern").
struct SetEra {
    std::string era_id;
};

// Change how fast agents move through simulated time and how long a day is (architecture §6): simulated
// seconds of movement per tick = movement_scale / base_tick_rate, and one day = day_length_ticks. Both live
// in the hashed TimeDefines, so this is a normal recorded command and replays exactly. A value out of range
// (a scale that is not above 0 or is more than 100, or a day shorter than 24 ticks) is ignored with a
// warning. Added for the time-scale spike (S0.8.2).
struct SetTimeScale {
    float movement_scale = 6.0F;
    std::uint32_t day_length_ticks = 28800;
};

} // namespace citysim::time
