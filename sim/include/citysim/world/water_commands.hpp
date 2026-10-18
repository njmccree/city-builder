#pragma once

#include <cstdint>

namespace citysim::world {

// Plain command structs for water. They are listed in the central `Command` variant in
// citysim/core/commands.hpp; the handler is installed by world::install().

// Debug and test command: sets how far a river reach's water surface is from its base level, in meters
// (WaterState; positive is a flood). A reach index that does not exist, or an offset that is not finite,
// gives a "river_level_failed" notice with the key "water.river_level.error.unknown_reach" or ".offset" and
// changes nothing. The flood extent follows over the next ticks (FloodMap). The river regimes (T1.3.2.1) set
// the levels from the calendar; this is for the console and for tests.
struct SetRiverLevel {
    std::uint32_t reach_index = 0;
    float offset_m = 0.0F;
};

} // namespace citysim::world
