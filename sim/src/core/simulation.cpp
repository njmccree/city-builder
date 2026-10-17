#include "citysim/core/simulation.hpp"

#include "citysim/core/log.hpp"
#include "citysim/profiling/profile.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
namespace citysim::core {

Simulation::Simulation(std::uint64_t world_seed, std::size_t worker_count)
    : world_seed_(world_seed), scheduler_(world_), jobs_(worker_count) {}

void Simulation::step() {
    CS_PROFILE_ZONE("Simulation::step");
    // Built once per tick: tick_ keeps its value until the end, so every phase sees the same tick.
    TickContext context{world_, tick_, rng::RngContext{world_seed_, tick_}, jobs_, events_};

    for (const Phase phase : k_all_phases) {
        CS_PROFILE_ZONE_DYNAMIC(phase_name(phase));
        events_.begin_phase();
        if (phase == Phase::ApplyCommands) {
            apply_commands(context);
        }
        scheduler_.run_phase(phase, context, observer_);
    }

    events_.clear();
    ++tick_;
    CS_PROFILE_FRAME_MARK; // one tick is one profiler frame
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

void Simulation::restore_loop_state(std::uint64_t world_seed, std::uint64_t tick,
                                    std::vector<SequencedCommand> pending_commands,
                                    std::vector<InputLogEntry> input_log) {
    if (tick_ != 0 || !pending_commands_.empty() || !input_log_.entries().empty()) {
        throw std::logic_error("restore_loop_state needs a Simulation that has not run");
    }
    world_seed_ = world_seed;
    tick_ = tick;
    pending_commands_ = std::move(pending_commands);
    input_log_.restore(std::move(input_log));
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
            CS_LOG_WARN(Sim, "no handler for command '{}' (sequence {}); ignored", describe(command.cmd),
                        command.sequence);
        }
        input_log_.record(tick_, command.sequence, command.cmd);
    }
}

} // namespace citysim::core
