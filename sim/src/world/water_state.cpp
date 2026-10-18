#include "citysim/world/water_state.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace citysim::world {

void WaterState::set_level_offset(std::uint32_t reach, float offset_m) {
    if (reach >= level_offsets_m_.size()) {
        throw std::out_of_range("river reach " + std::to_string(reach) + " does not exist");
    }
    if (!std::isfinite(offset_m)) {
        throw std::invalid_argument("a river level offset must be a finite number");
    }
    level_offsets_m_[reach] = offset_m == 0.0F ? 0.0F : offset_m;
}

void WaterState::restore(std::vector<float> offsets) {
    for (float& offset : offsets) {
        if (!std::isfinite(offset)) {
            throw std::invalid_argument("a saved river level offset is not finite");
        }
        if (offset == 0.0F) {
            offset = 0.0F;
        }
    }
    level_offsets_m_ = std::move(offsets);
}

} // namespace citysim::world
