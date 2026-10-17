#pragma once

#include "citysim/core/isystem.hpp"
#include "citysim/debug/grid_pathfinder.hpp"

#include <cstdint>
#include <vector>

namespace citysim::debug {

// Gives every idle debug agent a new random destination and the path to it. Runs in the Update phase, on the
// TICK clock. To bound the cost of a tick, at most `max_searches_per_tick` agents are planned, always the
// ones with the lowest entity index first, so the choice never depends on storage order. The destination
// comes from rng.at("debug.wander", entity, trips): the same game always plans the same trips.
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

  private:
    std::uint32_t max_searches_per_tick_;
    std::uint32_t node_budget_;
    PathScratch scratch_;
    std::vector<std::uint32_t> candidates_; // entity values, reused every tick
    std::uint32_t searches_last_tick_ = 0;
    std::uint64_t searches_total_ = 0;
};

} // namespace citysim::debug
