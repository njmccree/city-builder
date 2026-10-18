#pragma once

#include <cstdint>

namespace citysim::world {

// What the last Terraform command did, for the UI to show a running cost (published as the channel
// "world.terraform.last", see publish_terraform_report). A World ctx() singleton that is deliberately not
// registered with the ComponentRegistry: it is neither hashed nor saved (it only mirrors the command results,
// like the UiOutbox), so a load starts it again from zero. Changes only in the Terraform handler, on the tick
// clock.
struct TerraformReport {
    std::uint64_t serial = 0; // counts the applied strokes; 0 until the first
    std::uint32_t city = 0;   // the city of the last stroke
    double volume_m3 = 0.0;   // the last stroke
    double cost = 0.0;        // the last stroke
    // The cost of every stroke since the game was loaded or started. NOTE: nothing is charged yet; the
    // treasury arrives in E4.
    double total_cost = 0.0;
};

} // namespace citysim::world
