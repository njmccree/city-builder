#pragma once

#include "citysim/core/simulation.hpp"
#include "citysim/runner/render_snapshot.hpp"

namespace citysim::world {

// A publish hook (SimulationRunner::set_publish_hook, GameSession publish_hooks) that fills
// RenderSnapshot::terrain_tiles ("terrain.tiles") with the active city tiles, sorted by coordinate: the
// coordinate, the tile's version and the shared CityTile itself (no heights are copied). Runs on the runner
// thread after a tick and only reads the World. Does nothing if the world module is not installed.
void publish_terrain_tiles(core::Simulation& simulation, runner::RenderSnapshot& snapshot);

// Another publish hook: fills RenderSnapshot::cities ("world.cities": id, name, area, founding date, and
// which one is active) and active_city_id from the CityRegistry and ActiveCity singletons, in founding order.
// Runs on the runner thread after a tick and only reads the World. Does nothing if the cities are not
// installed.
void publish_cities(core::Simulation& simulation, runner::RenderSnapshot& snapshot);

// Another publish hook: adds one SnapshotGrid per active tile that has flooded land, named
// "water.flood.<tx>_<ty>" (513 x 513 cells of 4 m, one byte each: 1 = flooded land, 0 = dry or channel; the
// channel itself is drawn from the river data). The `cells` pointer is the FloodMap's own and changes only
// when the tile's marks do, so a renderer rebuilds a flood surface only when the pointer differs from the
// last one it saw. A tile without a grid has no flood. Runs on the runner thread after a tick and only reads
// the World. Does nothing if the flood extents are not installed.
void publish_flood_extents(core::Simulation& simulation, runner::RenderSnapshot& snapshot);

// Another publish hook: once a Terraform stroke has been applied, adds the channel "world.terraform.last"
// with one record of 5 floats: [serial, city id, volume m3, cost, total cost] (TerraformReport). The serial
// counts the strokes, so a UI sees a new one whenever it changes. Runs on the runner thread after a tick and
// only reads the World. Does nothing before the first stroke or if terraforming is not installed.
void publish_terraform_report(core::Simulation& simulation, runner::RenderSnapshot& snapshot);

} // namespace citysim::world
