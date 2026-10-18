#pragma once

#include "citysim/core/simulation.hpp"

namespace citysim::world {

// Wires the flood extents into a simulation that already has the WorldTerrain and WaterState singletons
// (world::install calls this): emplaces the FloodMap (derived state, deliberately not registered with the
// ComponentRegistry: it is neither hashed nor saved, see flood_map.hpp), adds the flood system to the
// PostUpdate phase and the handler of SetRiverLevel. The system advances the FloodMap once per tick within
// the budget of TerrainDefines::flood_samples_per_tick and emits a WaterChanged event for every reach whose
// extent finished changing.
//
// Clock: tick.
void install_flood(core::Simulation& simulation);

inline constexpr const char* k_error_unknown_reach = "water.river_level.error.unknown_reach";
inline constexpr const char* k_error_offset = "water.river_level.error.offset";

} // namespace citysim::world
