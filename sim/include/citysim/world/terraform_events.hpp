#pragma once

#include <cstdint>

namespace citysim::world {

// A Terraform command changed the ground. Listed in the central `Event` variant in citysim/core/events.hpp.
// Emitted in the ApplyCommands phase, so readable from PreUpdate on. Not emitted when the brush changed
// nothing.
struct TerraformApplied {
    std::uint32_t city = 0;
    // Earth moved, in cubic meters: the sum over the changed samples of |height change| x (4 m)^2.
    double volume_m3 = 0.0;
    // volume_m3 x TerrainDefines::terraform_cost_per_m3. NOTE: the treasury that charges it arrives in E4;
    // until then nothing is paid and the cost is only reported.
    double cost = 0.0;
};

} // namespace citysim::world
