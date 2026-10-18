#pragma once

#include "citysim/core/isystem.hpp"
#include "citysim/debug/grid_pathfinder.hpp"

#include <cstdint>
#include <vector>

namespace citysim::debug {

// Gives every idle debug agent a new random destination and the path to it. Runs in the Update phase, on the
// TICK clock. It plans on whatever WalkGrid the world has (the flat TestMap, or the TerrainWalkGrid of the
// active city). When the grid's version() has changed since it last looked, it first drops the path of every
// agent whose remaining cells have become blocked (a flood, a terrain edit), so that agent is planned again
// like an idle one; and an agent that stands on a blocked cell is sent the shortest way out instead of to a
// random destination (find_escape_path). To bound the cost of a tick, at most `max_searches_per_tick` agents
// are planned, always the ones with the lowest entity index first, so the choice never depends on storage
// order. The destination comes from rng.at("debug.wander", entity, trips): the same game always plans the
// same trips.
//
// Its members are caches and counters for diagnostics; none of it is game state.
class WanderPlannerSystem final : public core::ISystem {
  public:
    WanderPlannerSystem(std::uint32_t max_searches_per_tick, std::uint32_t node_budget)
        : max_searches_per_tick_(max_searches_per_tick), node_budget_(node_budget) {}

    std::string_view name() const override { return "debug.wander_planner"; }
    void update(core::TickContext& context) override;

    // Path searches run in the latest tick, and in all ticks so far (for the benchmark).
    std::uint32_t searches_last_tick() const { return searches_last_tick_; }
    std::uint64_t searches_total() const { return searches_total_; }
    // Paths dropped because a cell on them became blocked, over all ticks (diagnostics).
    std::uint64_t paths_invalidated_total() const { return paths_invalidated_total_; }

  private:
    std::uint32_t max_searches_per_tick_;
    std::uint32_t node_budget_;
    PathScratch scratch_;
    std::vector<std::uint32_t> candidates_; // entity values, reused every tick
    std::uint64_t checked_version_ = 0; // the WalkGrid::version() the agents' paths were last checked against
    std::uint32_t searches_last_tick_ = 0;
    std::uint64_t searches_total_ = 0;
    std::uint64_t paths_invalidated_total_ = 0;
};

} // namespace citysim::debug
