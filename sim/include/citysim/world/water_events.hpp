#pragma once

#include <cstdint>

namespace citysim::world {

// A river reach's flood extent has finished changing (architecture §15.6): the flood fill that follows a new
// water level reached its end. Listed in the central `Event` variant in citysim/core/events.hpp. Emitted by
// the flood system in the PostUpdate phase, so readable from the Calendar phase on.
struct WaterChanged {
    std::uint32_t city = 0;  // the active city when it finished, 0 if none
    std::uint32_t reach = 0; // the reach index (RiverNetwork order)
    // The flooded land of this reach in square meters, channel water not counted; 0 when the water went back
    // to its channel.
    double flooded_area_m2 = 0.0;
};

} // namespace citysim::world
