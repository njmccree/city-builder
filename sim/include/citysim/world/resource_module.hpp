#pragma once

#include "citysim/content/content_database.hpp"
#include "citysim/core/simulation.hpp"

namespace citysim::world {

// Wires the resource fields (architecture §15.8) into a simulation that already has the WorldTerrain
// singleton (world::install calls this): builds the field list from the content (fertility, vegetation and
// wetness, plus the package layer of every ResourceDef), registers the ResourceFields as "world.Resources"
// (only the deltas are hashed and saved) and adds the system that prepares the bases of the active tiles in
// parallel whenever the set of active tiles changes (PreUpdate, tick clock). A load whose save has changes to
// a resource this content does not have drops them with a warning (reported by world::install's after-load
// hook).
//
// `content` must outlive the simulation.
void install_resources(core::Simulation& simulation, const content::ContentDatabase& content);

} // namespace citysim::world
