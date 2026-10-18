#pragma once

#include "citysim/core/simulation.hpp"

namespace citysim::world {

// Wires terraforming into a simulation that already has the WorldTerrain, CityRegistry and ActiveCity
// singletons (world::install calls this): emplaces the TerraformReport and adds the handler of the Terraform
// command. The handler refuses, with a "terraform_failed" notice carrying one of the keys below and no
// change, a city that does not exist, a city that is not the active one, parameters out of range, and a brush
// that does not touch the city's area. A stroke that changes the ground emits a TerraformApplied event, a
// "terraform_applied" notice (subject = the city) and updates the TerraformReport. Clock: tick.
void install_terraform(core::Simulation& simulation);

inline constexpr const char* k_error_terraform_unknown_city = "terraform.error.unknown_city";
inline constexpr const char* k_error_terraform_not_active = "terraform.error.not_active";
inline constexpr const char* k_error_terraform_parameters = "terraform.error.parameters";
inline constexpr const char* k_error_terraform_outside_city = "terraform.error.outside_city";

} // namespace citysim::world
