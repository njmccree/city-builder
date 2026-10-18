#pragma once

#include "citysim/world/frames.hpp"

#include <cstdint>

namespace citysim::world {

// Plain command structs for terraforming. They are listed in the central `Command` variant in
// citysim/core/commands.hpp; the handler is installed by world::install().

enum class TerraformOp : std::uint8_t {
    Raise = 0,   // lift the ground by up to strength * terraform_step_m under the brush
    Lower = 1,   // the same downwards
    Flatten = 2, // move the ground towards target_height_m by strength, softly: repeated strokes get closer
    Smooth = 3,  // move the ground towards the average of its four neighbors by strength
    Level = 4, // cut and fill to target_height_m: a flat disc half the brush wide at strength 1, soft skirts
};
inline constexpr std::uint8_t k_terraform_op_count = 5;

// Shape the ground of a city with a round brush (GDD §7.5; architecture §15.4). Applied at once,
// deterministic (the brush is evaluated on a snapshot of the old heights, in a fixed sample order). Rules:
//   - `city_id` must be the active city; samples are changed only inside its 8 x 8 km area (the south and
//   west
//     edge samples are inside, the north and east ones belong to the neighbor), so a brush that straddles the
//     border is clipped, and one entirely outside changes nothing.
//   - every sample stays within [k_terraform_min_offset_m, k_terraform_max_offset_m] of its synthesized base
//     height, and the rivers stay fixed: samples inside a river channel are never changed and the ground
//     beside a channel is never cut below the channel's bed.
//   - the work is paid for upfront by volume (TerraformApplied.cost); the treasury that charges it arrives in
//   E4.
// A refusal gives a "terraform_failed" notice with a localization key and changes nothing; see
// terraform_module.hpp.
struct Terraform {
    std::uint32_t city_id = 0;
    TerraformOp op = TerraformOp::Raise;
    LocalPos center;              // in the city's local frame
    float radius_m = 20.0F;       // 2..200
    float strength = 0.5F;        // 0..1
    float target_height_m = 0.0F; // Flatten and Level: meters above sea level
};

inline constexpr float k_terraform_min_radius_m = 2.0F;
inline constexpr float k_terraform_max_radius_m = 200.0F;
inline constexpr float k_terraform_min_offset_m = -50.0F;
inline constexpr float k_terraform_max_offset_m = 200.0F;
// The target heights a command may name (the ground cannot be cut or built beyond the offset limits anyway).
inline constexpr float k_terraform_min_target_m = -500.0F;
inline constexpr float k_terraform_max_target_m = 5000.0F;

} // namespace citysim::world
