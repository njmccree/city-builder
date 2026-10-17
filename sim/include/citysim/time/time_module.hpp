#pragma once

#include "citysim/core/simulation.hpp"
#include "citysim/time/time_defines.hpp"

namespace citysim::time {

// Wires the time module into a simulation: registers GameClock and TimeDefines for hashing, emplaces them
// as singletons, adds the ClockSystem (PreUpdate) and handlers for SetSpeed and TogglePause.
// Call once, before the simulation runs. Throws std::invalid_argument for unusable defines (zero tick rate or
// day length, or a zero multiplier for speeds 1..3).
void install(core::Simulation& simulation, const TimeDefines& defines = {});

// Throws std::invalid_argument; also used by the runner to validate its configuration.
void validate(const TimeDefines& defines);

} // namespace citysim::time
