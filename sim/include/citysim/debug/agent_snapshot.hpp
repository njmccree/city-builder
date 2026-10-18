#pragma once

#include "citysim/core/simulation.hpp"
#include "citysim/runner/render_snapshot.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace citysim::debug {

// Name of the snapshot channel with the debug agents' transforms.
inline constexpr const char* k_agent_channel = "agents.xform";

// A publish hook (SimulationRunner::set_publish_hook, GameSession publish_hooks) that adds the channel
// "agents.xform" to the snapshot: one record per debug agent, four floats each (x, y, z, yaw), with y = 0 on
// the flat test map and the terrain height at the agent's position on real terrain (positions are then
// city-local, architecture §15.3; WorldTerrain::height_at_local), and `ids` holding each agent's entity
// index. Agents are in ascending entity-index order, so a renderer matches two snapshots with a merge and
// agents that were spawned or cleared in between are simply skipped. Positions are meters on the ground plane
// (x, z); yaw is radians (see Heading). Runs on the runner thread after a tick and only reads the World. Does
// nothing if the walking skeleton is not installed.
void publish_agent_snapshot(core::Simulation& simulation, runner::RenderSnapshot& snapshot);

// Name of the snapshot grid with the test map's walkable cells (1 = walkable, 0 = blocked).
inline constexpr const char* k_test_map_grid = "testmap.grid";

// Publishes the TestMap as the grid "testmap.grid". It keeps the last grid it built and hands out the same
// shared pointer until the map's cells change (a new game or a load), so the front end can tell "same map"
// from "new map" by comparing pointers. Runs on the runner thread only, so it needs no locking; one per game.
class TestMapPublisher {
  public:
    void operator()(core::Simulation& simulation, runner::RenderSnapshot& snapshot);

  private:
    std::uint64_t fingerprint_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    float cell_size_ = 0.0F;
    std::shared_ptr<const std::vector<std::uint8_t>> cells_;
};

} // namespace citysim::debug
