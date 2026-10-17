#include "citysim/core/simulation.hpp"

#include <algorithm>
#include <iterator>
#include <spdlog/spdlog.h>

namespace citysim::core {

Simulation::Simulation(std::uint64_t world_seed, std::size_t worker_count)
    : world_seed_(world_seed), scheduler_(world_), jobs_(worker_count) {}

void Simulation::step() {
    // Built once per tick: tick_ keeps its value until the end, so every phase sees the same tick.
    TickContext context{world_, tick_, rng::RngContext{world_seed_, tick_}, jobs_, events_};

    for (const Phase phase : k_all_phases) {
        events_.begin_phase();
        if (phase == Phase::ApplyCommands) {
            apply_commands(context);
        }
        scheduler_.run_phase(phase, context, observer_);
    }

    events_.clear();
    ++tick_;
}

void Simulation::apply_commands_only() {
    TickContext context{world_, tick_, rng::RngContext{world_seed_, tick_}, jobs_, events_};
    events_.begin_phase();
    apply_commands(context);
    events_.clear();
}

void Simulation::enqueue(std::vector<SequencedCommand> commands) {
    pending_commands_.insert(pending_commands_.end(), std::make_move_iterator(commands.begin()),
                             std::make_move_iterator(commands.end()));
}

void Simulation::apply_commands(TickContext& context) {
    // Sequence order is the contract, not arrival order. stable_sort keeps equal sequences (which a
    // correct queue never produces) in arrival order, so the result is still deterministic.
    std::stable_sort(
        pending_commands_.begin(), pending_commands_.end(),
        [](const SequencedCommand& a, const SequencedCommand& b) { return a.sequence < b.sequence; });

    // Moved out first so a handler cannot disturb the list being walked.
    std::vector<SequencedCommand> batch;
    batch.swap(pending_commands_);
    for (const SequencedCommand& command : batch) {
        if (!dispatcher_.dispatch(context, command.cmd)) {
            spdlog::warn("no handler for command '{}' (sequence {}); ignored", describe(command.cmd),
                         command.sequence);
        }
        input_log_.record(tick_, command.sequence, command.cmd);
    }
}

} // namespace citysim::core
