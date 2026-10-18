#pragma once

#include "citysim/world/frames.hpp"
#include "citysim/world/water_state.hpp"
#include "citysim/world/world_terrain.hpp"

#include <optional>

namespace citysim::world {

// Where there is water (architecture §15.6), read from the carved terrain, the river data and the reach level
// offsets. All are pure reads, safe to call from parallel_for chunks. Not tied to a clock.
//
// A point has water in two cases: it lies within a river channel (closer to a centerline than half the
// channel's width), where the surface is the reach's base level plus its offset; or it is sea, where the
// ground is at or below sea level. (Land flooded by a river that has risen is added with the flood extents,
// T1.3.1.2.) Heights come from WorldTerrain::height_at, so depths are exact where the city tiles are active
// (the channels are carved there) and approximate elsewhere, where the 32 m ground has no channel.

// The water surface in meters above the datum at `pos`, or nullopt on dry land. Where channels meet, the
// nearest centerline decides (ties go to the lower reach index).
std::optional<float> water_level_at(const WorldTerrain& terrain, const WaterState& water, WorldPos pos);

// How deep the water is at `pos`: surface minus ground, 0 on dry land and wherever the ground is above the
// surface (a channel edge in a dry year).
float water_depth(const WorldTerrain& terrain, const WaterState& water, WorldPos pos);

// True if water_depth is above 0.
bool is_water(const WorldTerrain& terrain, const WaterState& water, WorldPos pos);

} // namespace citysim::world
