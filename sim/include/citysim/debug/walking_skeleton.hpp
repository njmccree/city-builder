#pragma once

#include "citysim/core/simulation.hpp"
#include "citysim/debug/wander_planner_system.hpp"

#include <cstdint>

namespace citysim::debug {

// Where the agents walk.
enum class WalkGridSource {
    TestMap, // the flat, hashed and saved test map made from the world seed
    Terrain, // the real terrain of the active city (TerrainWalkGrid); needs the world module
};

struct WalkingSkeletonConfig {
    WalkGridSource source = WalkGridSource::TestMap;
    // TestMap only:
    std::uint32_t map_width = 256;  // cells
    std::uint32_t map_height = 256; // cells
    float cell_size = 4.0F;         // meters
    // At most this many agents get a new path per tick (always the lowest entity indices first).
    std::uint32_t max_searches_per_tick = 200;
    // A single path search gives up after this many expansions.
    std::uint32_t path_node_budget = 100000;
};

struct WalkingSkeleton {
    const WanderPlannerSystem* planner = nullptr; // for diagnostics; owned by the simulation's scheduler
};

// Installs the walking skeleton into a simulation (architecture §2): the walk grid (the TestMap made from the
// simulation's world seed, or, with WalkGridSource::Terrain, the derived TerrainWalkGrid plus the system that
// keeps it right, which needs world::install() to have run first), the debug agent components, the planner
// and movement systems (Update phase, in that order) and the SpawnDebugAgents / ClearDebugAgents commands.
// Everything is registered for hashing and saving. The module is throwaway gameplay with production-quality
// engineering: it exists to prove the architecture end to end.
//
// Requires time::install() to have run first (movement speed comes from the time defines). Call it on every
// simulation that is going to load a save made by one that called it.
WalkingSkeleton install(core::Simulation& simulation, const WalkingSkeletonConfig& config = {});

} // namespace citysim::debug
