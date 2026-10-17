#pragma once

#include <cstdint>

namespace citysim::time {

// Plain command structs for the game clock. They are listed in the central `Command` variant in
// citysim/core/commands.hpp; the handlers are installed by time::install().

// Choose a speed: 1, 2 or 3 (1x, 2x, 4x). Anything else is ignored with a warning.
struct SetSpeed {
    std::uint8_t speed = 1;
};

struct TogglePause {};

} // namespace citysim::time
