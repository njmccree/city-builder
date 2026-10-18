#pragma once

#include "citysim/world/tile_coord.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace citysim::world {

// Coordinate frames of the world (architecture §15.3). Not tied to a clock.
//
//   World frame       meters from the south-west corner of the projected map bounds, x east and z north, in
//                     double. For region-level things: city area origins, rivers, trade routes, armies.
//   City-local frame  meters from the south-west corner of a city area, in float. Everything inside a city
//                     (agents, buildings, roads) uses it: over 8 km a float still resolves about a
//                     millimeter. Conversions go through CityArea::origin(), which is exact in double.
//   Tile grid         2,048 m tiles counted from the south-west corner (TileCoord). Tile (tx, ty) covers
//                     [tx * 2048, (tx + 1) * 2048] x [ty * 2048, (ty + 1) * 2048]; neighbors share their
//                     edge.

// The side of a tile of the world grid, in meters (package detail tiles and city tiles alike).
inline constexpr double k_tile_size_m = 2048.0;
// Spacing of the synthesized city terrain samples.
inline constexpr double k_city_sample_spacing_m = 4.0;
// Samples along one side of a city tile: 2,048 / 4 + 1, because edge samples are shared with the neighbor.
inline constexpr std::uint32_t k_city_tile_samples = 513;
// A city area is this many tiles along each side (4 x 4 tiles = 8,192 m).
inline constexpr std::int32_t k_city_area_tiles = 4;
inline constexpr double k_city_area_size_m = k_tile_size_m * k_city_area_tiles;

static_assert(static_cast<double>(k_city_tile_samples - 1) * k_city_sample_spacing_m == k_tile_size_m);

// A point in the world frame (meters, double).
struct WorldPos {
    double x = 0.0;
    double z = 0.0;

    friend constexpr bool operator==(const WorldPos&, const WorldPos&) = default;
};

// A point in a city's local frame (meters from the city area's south-west corner, float).
struct LocalPos {
    float x = 0.0F;
    float z = 0.0F;

    friend constexpr bool operator==(const LocalPos&, const LocalPos&) = default;
};

// floor(meters / tile size) as a tile index, clamped to a range no map comes near, so a wild coordinate can
// never overflow the integer. Non-finite input gives 0; callers that care check std::isfinite first.
inline std::int32_t tile_index_of(double meters) {
    if (!std::isfinite(meters)) {
        return 0;
    }
    constexpr double k_limit = 1 << 30;
    return static_cast<std::int32_t>(std::clamp(std::floor(meters / k_tile_size_m), -k_limit, k_limit));
}

// The tile whose half-open square [origin, origin + 2048) contains the point.
inline TileCoord tile_of(WorldPos pos) {
    return TileCoord{tile_index_of(pos.x), tile_index_of(pos.z)};
}

// The south-west corner of a tile.
inline constexpr WorldPos tile_origin(TileCoord tile) {
    return WorldPos{static_cast<double>(tile.tx) * k_tile_size_m,
                    static_cast<double>(tile.ty) * k_tile_size_m};
}

// A city area (architecture §15.1): exactly 4 x 4 tiles, aligned to the tile grid, named by its south-west
// tile. City areas never overlap but may touch.
struct CityArea {
    TileCoord first_tile;

    // The south-west corner of the area in the world frame: the origin of its city-local frame.
    constexpr WorldPos origin() const { return tile_origin(first_tile); }

    constexpr bool contains_tile(TileCoord tile) const {
        return tile.tx >= first_tile.tx && tile.tx < first_tile.tx + k_city_area_tiles &&
               tile.ty >= first_tile.ty && tile.ty < first_tile.ty + k_city_area_tiles;
    }

    // The area's 16 tiles plus `ring` tiles on every side, sorted by TileCoord (south row first). The
    // simulation activates the area with a ring of 1 (architecture §15.1).
    std::vector<TileCoord> tiles(std::int32_t ring = 0) const {
        std::vector<TileCoord> out;
        const std::int32_t side = k_city_area_tiles + 2 * std::max(ring, 0);
        out.reserve(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
        const std::int32_t r = std::max(ring, 0);
        for (std::int32_t ty = first_tile.ty - r; ty < first_tile.ty + k_city_area_tiles + r; ++ty) {
            for (std::int32_t tx = first_tile.tx - r; tx < first_tile.tx + k_city_area_tiles + r; ++tx) {
                out.push_back(TileCoord{tx, ty});
            }
        }
        return out;
    }

    friend constexpr bool operator==(const CityArea&, const CityArea&) = default;
};

// City-local to world: exact (a float widened to double plus a multiple of 2,048).
inline constexpr WorldPos to_world(const CityArea& city, LocalPos local) {
    const WorldPos origin = city.origin();
    return WorldPos{origin.x + static_cast<double>(local.x), origin.z + static_cast<double>(local.z)};
}

// World to city-local: rounds to float, which is exact for any point that came from to_world().
inline constexpr LocalPos to_local(const CityArea& city, WorldPos pos) {
    const WorldPos origin = city.origin();
    return LocalPos{static_cast<float>(pos.x - origin.x), static_cast<float>(pos.z - origin.z)};
}

} // namespace citysim::world
