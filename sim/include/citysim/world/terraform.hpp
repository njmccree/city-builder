#pragma once

#include "citysim/world/frames.hpp"
#include "citysim/world/terraform_commands.hpp"
#include "citysim/world/terrain_defines.hpp"
#include "citysim/world/world_terrain.hpp"

#include <cstddef>

namespace citysim::world {

enum class TerraformStatus {
    Applied,       // the brush was evaluated (it may still have changed nothing: flat ground, clamps)
    BadParameters, // a radius, strength, height or operation outside its range, or a value that is not finite
    OutsideCity,   // the brush does not touch the city's area at all
};

struct TerraformResult {
    TerraformStatus status = TerraformStatus::Applied;
    std::size_t samples_changed = 0; // distinct samples (a sample on a tile edge counts once)
    double volume_m3 = 0.0;          // the sum over them of |height change| x (4 m)^2
};

// The brush engine behind the Terraform command (see terraform_commands.hpp for the rules). Evaluates the
// brush on a snapshot of the heights, so the result does not depend on the order of the samples, and applies
// it through WorldTerrain::apply_delta. Only samples inside `area` are changed; the area's tiles must be
// active (std::logic_error otherwise, from the terrain). Pure function of its arguments and the terrain, on
// the tick clock (it is called from a command handler); it uses plain float/double arithmetic only, and its
// source is compiled without fused multiply-add so every compiler gives the same heights.
TerraformResult apply_terraform(WorldTerrain& terrain, const CityArea& area, const Terraform& command,
                                const TerrainDefines& defines);

} // namespace citysim::world
