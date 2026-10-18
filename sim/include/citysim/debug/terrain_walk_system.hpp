#pragma once

#include "citysim/core/isystem.hpp"
#include "citysim/core/job_system.hpp"
#include "citysim/core/world.hpp"

namespace citysim::debug {

// Keeps the TerrainWalkGrid right: once per tick it asks the grid to follow the active city, the terrain
// edits and the floods (TerrainWalkGrid::sync). Runs in the PostUpdate phase, on the TICK clock, after the
// flood system (world::install registers that one first), so the flood extents of the tick are already in.
// When the active city changes from one city to another, the debug agents are removed: their positions are in
// the previous city's frame. (A first city, or the same city after a load, keeps them.) What the system does
// each tick, also called once after a load (debug::install adds that hook) so that the grid exists before the
// first tick of a loaded game, as it does in the game it was saved from.
void follow_active_city(core::World& world, core::JobSystem& jobs);

class TerrainWalkSystem final : public core::ISystem {
  public:
    std::string_view name() const override { return "debug.terrain_walk"; }
    void update(core::TickContext& context) override;
};

} // namespace citysim::debug
