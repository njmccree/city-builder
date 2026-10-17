#pragma once

#include "citysim/core/commands.hpp"
#include "citysim/core/simulation.hpp"

#include <cstdint>
#include <vector>

namespace citysim::headless {

// A stand-in game for exercising the kernel until real systems exist: a population of entities that
// wander using the counter-based RNG, and a handler that lets commands move them. It is scaffolding
// for the runner and its tests and is replaced once the base game has real systems and content.
inline constexpr std::uint32_t k_demo_population = 50;

// Registers the demo component, systems and command handlers on `sim`.
void setup_demo(citysim::core::Simulation& sim);

// The "player input" the demo run issues at `tick`, derived only from the seed and the tick. In a live
// run it goes through a CommandQueue like input from a UI thread would; replay ignores it and feeds the
// recorded log instead.
std::vector<citysim::core::Command> demo_input(std::uint64_t seed, std::uint64_t tick);

} // namespace citysim::headless
