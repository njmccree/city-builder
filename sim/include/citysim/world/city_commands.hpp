#pragma once

#include <cstdint>
#include <string>

namespace citysim::world {

// Plain command structs for cities. They are listed in the central `Command` variant in
// citysim/core/commands.hpp; the handlers are installed by world::install().

// Found a city on the 8 x 8 km area whose south-west tile is (origin_tx, origin_ty). Checked in this order,
// and on the first problem nothing changes and a "city_found_failed" notice carries the localization key:
//   city.found.error.limit        the demesne limit of cities is reached (CityDefines::max_player_cities)
//   city.found.error.name         the name is longer than 64 bytes
//   city.found.error.out_of_bounds  one of the 16 tiles is not on the map
//   city.found.error.no_detail    one of the 16 tiles has no detail terrain (outside the detail regions)
//   city.found.error.overlap      the area shares ground with a city (touching is fine)
//   city.found.error.not_land     less than CityDefines::min_land_fraction of the area is land
// An empty name becomes "City N" (N = the new id). The first city founded also becomes the active city.
struct FoundCity {
    std::int32_t origin_tx = 0;
    std::int32_t origin_ty = 0;
    std::string name;
};

// Make a city the one being viewed: its 16 tiles plus the ring become active terrain, the previous city's
// tiles that the new set does not need are dropped, and the sim modes follow (the active city is Agent, every
// other is Aggregate). An unknown id gives a "city.active.error.unknown" notice. The same city is a no-op.
struct SetActiveCity {
    std::uint32_t city_id = 0;
};

} // namespace citysim::world
