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

} // namespace citysim::world
