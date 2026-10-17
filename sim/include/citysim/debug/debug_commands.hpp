#pragma once

#include <cstdint>

namespace citysim::debug {

// Plain command structs for the walking skeleton. They are listed in the central `Command` variant in
// citysim/core/commands.hpp; the handlers are installed by debug::install().

// Adds `count` agents at random walkable cells.
struct SpawnDebugAgents {
    std::uint32_t count = 0;
};

// Removes every debug agent.
struct ClearDebugAgents {};

} // namespace citysim::debug
