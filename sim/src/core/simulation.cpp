#include "citysim/core/simulation.hpp"

namespace citysim::core {

Simulation::Simulation(std::uint64_t world_seed) : world_seed_(world_seed), scheduler_(world_) {}

void Simulation::step() {
    // Built once per tick: tick_ keeps its value until the end, so every phase sees the same tick.
    TickContext context{world_, tick_, rng::RngContext{world_seed_, tick_}, jobs_, events_};

    for (const Phase phase : k_all_phases) {
        events_.begin_phase();
        scheduler_.run_phase(phase, context, observer_);
    }

    events_.clear();
    ++tick_;
}

} // namespace citysim::core
