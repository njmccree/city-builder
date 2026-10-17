#pragma once

#include "citysim/core/isystem.hpp"
#include "citysim/core/phase.hpp"
#include "citysim/core/system_observer.hpp"
#include "citysim/core/tick_context.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace citysim::core {

// Owns the systems and runs them one phase at a time. Runs on the tick clock.
//
// Inside a phase, systems run in registration order. That order is part of the game's behavior, so it
// is never sorted or reordered implicitly (architecture §5.2).
class Scheduler {
  public:
    explicit Scheduler(World& world) : world_(world) {}

    // Registers `system` in `phase`, calling its on_register(world) first. Throws std::logic_error for a
    // null system or a name already used by any system in any phase; nothing is changed in that case.
    ISystem& add(Phase phase, std::unique_ptr<ISystem> system);

    void run_phase(Phase phase, TickContext& context, SystemObserver* observer = nullptr);

    std::span<const std::unique_ptr<ISystem>> systems(Phase phase) const;
    std::size_t system_count() const;

  private:
    World& world_;
    std::array<std::vector<std::unique_ptr<ISystem>>, k_phase_count> phases_;
};

} // namespace citysim::core
