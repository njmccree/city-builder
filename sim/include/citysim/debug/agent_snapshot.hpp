#pragma once

#include "citysim/core/simulation.hpp"
#include "citysim/runner/render_snapshot.hpp"

namespace citysim::debug {

// Name of the snapshot channel with the debug agents' transforms.
inline constexpr const char* k_agent_channel = "agents.xform";

// A publish hook (SimulationRunner::set_publish_hook, GameSession publish_hooks) that adds the channel
// "agents.xform" to the snapshot: one record per debug agent, four floats each (x, y, z, yaw), with y = 0 for
// now, and `ids` holding each agent's entity index. Agents are in ascending entity-index order, so a renderer
// matches two snapshots with a merge and agents that were spawned or cleared in between are simply skipped.
// Positions are meters on the ground plane (x, z); yaw is radians (see Heading). Runs on the runner thread
// after a tick and only reads the World. Does nothing if the walking skeleton is not installed.
void publish_agent_snapshot(core::Simulation& simulation, runner::RenderSnapshot& snapshot);

} // namespace citysim::debug
