#pragma once

#include "citysim/world/city_tile.hpp"
#include "citysim/world/loaded_map.hpp"
#include "citysim/world/tile_coord.hpp"

#include <cstdint>
#include <memory>

namespace citysim::world {

// The version of the terrain generator (architecture §15.4). Terrain is a function of the map package (its
// package hash), the map's seed and this version, so saves store it and warn when it differs on load. Bump it
// whenever a change alters any synthesized height, and say so in the PR.
//   0  placeholder: bilinear upsampling of the 32 m DEM only (T1.1.2.1)
inline constexpr std::uint32_t k_terrain_generator_version = 0;

// Makes the 4 m city tiles from a map package (architecture §15.4). Deterministic: the same map, tile and
// seed always give the same heights, bit for bit, on any thread. Shared edges agree with the neighbor's
// because every sample is a function of its world position alone. Safe to call for different tiles in
// parallel. Not tied to a clock: called when tiles are activated.
//
// For now this is the placeholder generator (version 0): each sample is LoadedMap::dem_height() at its world
// position, i.e. bilinear in the 32 m detail DEM where the package has detail tiles and in the region
// elevation elsewhere (ring tiles outside the detail regions). T1.1.2.2 replaces it with bicubic upsampling,
// seeded micro-detail from `map_seed` (never the game seed) and river channels.
class TerrainSynth {
  public:
    // Throws std::out_of_range if the tile lies outside the map, MapFormatError if a detail tile it needs is
    // damaged.
    static std::shared_ptr<const CityTile> make_tile(const LoadedMap& map, TileCoord tile,
                                                     std::uint64_t map_seed);
};

} // namespace citysim::world
