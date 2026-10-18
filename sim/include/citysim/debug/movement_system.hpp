#pragma once

#include "citysim/core/isystem.hpp"

#include <cstdint>
#include <vector>

namespace citysim::debug {

// Moves every debug agent along its path. Runs in the Update phase, on the TICK clock, after the planner.
// Each agent covers speed x movement_seconds_per_tick (architecture §6) per tick, walking cell centre to cell
// centre. The work is a parallel_for over agents in fixed chunks of 1024; agents do not influence each other,
// so the result is the same with any number of threads (architecture §7.2).
class MovementSystem final : public core::ISystem {
  public:
    std::string_view name() const override { return "debug.movement"; }
    void update(core::TickContext& context) override;

  private:
    std::vector<std::uint32_t> agents_; // entity values, rebuilt every tick
};

} // namespace citysim::debug
