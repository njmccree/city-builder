#pragma once

#include <cstdint>

namespace citysim::world {

// Tunable numbers of founding and activating cities (architecture §15.1, GDD §7.7). The built-in defaults are
// these; content loading overrides them from defines/world.json. Part of the hashed and saved state, like the
// time defines.
struct CityDefines {
    // The demesne limit: cities the player directly controls (GDD §7.7).
    std::uint32_t max_player_cities = 10;
    // A city area must be at least this fraction land (region cells whose center lies inside it).
    float min_land_fraction = 0.5F;
    // Tiles around the active city's 4 x 4 that are synthesized too (architecture §15.1).
    std::uint32_t active_ring_tiles = 1;
};

} // namespace citysim::world
