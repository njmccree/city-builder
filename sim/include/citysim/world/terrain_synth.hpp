#pragma once

#include "citysim/world/city_tile.hpp"
#include "citysim/world/loaded_map.hpp"
#include "citysim/world/terrain_defines.hpp"
#include "citysim/world/tile_coord.hpp"

#include <cstdint>
#include <memory>

namespace citysim::world {

// The version of the terrain generator (architecture §15.4). Terrain is a function of the map package (its
// package hash), the map's seed and this version, so saves store it and warn when it differs on load. Bump it
// whenever a change alters any synthesized height, and say so in the PR.
//   0  placeholder: bilinear upsampling of the 32 m DEM only (T1.1.2.1)
//   1  Catmull-Rom bicubic upsampling plus seeded micro-detail noise (T1.1.2.2)
//   2  river channels carved along rivers.json (T1.3.1.1)
inline constexpr std::uint32_t k_terrain_generator_version = 2;

// Makes the 4 m city tiles from a map package (architecture §15.4). Deterministic: the same map, tile and
// seed always give the same heights, bit for bit, on any thread. Shared edges agree with the neighbor's
// because every sample is a function of its world position alone. Safe to call for different tiles in
// parallel. Not tied to a clock: called when tiles are activated.
//
// How a tile is made (version 1):
//   1. The 32 m DEM nodes around the tile come from the package's detail tiles (the region elevation where a
//      detail tile is missing), read from the tile and its eight neighbors.
//   2. Bicubic (Catmull-Rom) upsampling to 4 m samples. Every sample is a function of its world position and
//      the nodes around it, so the shared edge samples of neighboring tiles are equal bit for bit.
//   3. Micro-detail: value noise at lattice spacings of 128, 64, 32, 16 and 8 m, each lattice point hashed
//   from
//      rng::hash64(map_seed, stream("terrain.detail"), tx, ty, ...) of the tile that owns the point (a point
//      on a tile border belongs to the east/north neighbor), so adjacent tiles read the same lattice. The
//      amplitude is 0.3 m on flat fertile ground (the region "fertility" layer and the base slope) and up to
//      3 m on steep or barren ground, and fades to zero at sea level and below.
//   4. River channels (version 2): every sample within a channel's half width of a river centerline is
//   lowered to
//      the bed, level - depth at the middle rising as a parabola to level - depth *
//      channel_edge_depth_fraction at the edge, and beyond the edge a bank of bank_width_m climbs back to the
//      surrounding ground in an S-curve (TerrainDefines). The bed never rises above the ground, and where
//      channels meet the lowest wins, so the result is a function of world position and edges still agree.
//      Water levels do not change terrain: the base level of the river is used, the level offsets are
//      WaterState.
// Terrain edits (T1.1.2.3) are added on top by WorldTerrain.
class TerrainSynth {
  public:
    // Throws std::out_of_range if the tile lies outside the map, MapFormatError if a detail tile it needs is
    // damaged.
    static std::shared_ptr<const CityTile> make_tile(const LoadedMap& map, TileCoord tile,
                                                     std::uint64_t map_seed,
                                                     const TerrainDefines& defines = {});
};

} // namespace citysim::world
