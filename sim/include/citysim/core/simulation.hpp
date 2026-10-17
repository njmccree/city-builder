#pragma once

#include "citysim/core/command_dispatcher.hpp"
#include "citysim/core/commands.hpp"
#include "citysim/core/event_bus.hpp"
#include "citysim/core/input_log.hpp"
#include "citysim/core/job_system.hpp"
#include "citysim/core/scheduler.hpp"
#include "citysim/core/system_observer.hpp"
#include "citysim/core/world.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

// ============================================================================================
// THE TICK CONTRACT (architecture §4-§7)
//
// One call to Simulation::step() is one tick. It runs the phases below in this fixed order, then
// drops the tick's events and increments tick(). Inside a phase, systems run in registration order.
// Simulation knows nothing about threads or wall-clock time: the runner decides when to call step().
//
//   ApplyCommands  The only place external input (UI, console, load/save requests) changes state,
//                  and it arrives as commands. Before any system registered in this phase runs,
//                  Simulation applies the commands passed to enqueue() in sequence order, through
//                  the handlers registered on dispatcher(), and records each in the input log.
//                  Other systems must not react to input any other way.
//   PreUpdate      Read state and prepare this tick's inputs (derived caches, per-tick buffers).
//                  May read events emitted by ApplyCommands.
//   Update         The main simulation work. May change components and singletons, create and
//                  destroy entities, emit events and use randomness. May use TickContext::parallel_for.
//   PostUpdate     Reconcile after Update: clean up, resolve effects that span systems.
//   Calendar       Advance the clock and calendar, and run processes that live on calendar time
//                  (aging, harvests, construction progress, economy; §6).
//   Publish        Read-only. Copy what the UI needs into snapshots. Must not change simulation
//                  state, because snapshots are not part of the hashed state.
//
// Rules for every system:
//   * Randomness only through TickContext::rng (counter-based) or a Pcg32 seeded from it. Never
//     rand(), <random>, or wall-clock time (§4). Timing belongs in a SystemObserver.
//   * All state that affects the simulation lives in World components or ctx() singletons that are
//     registered in the ComponentRegistry, so it is hashed and saved. Do not keep such state in
//     system members. Caches that are rebuilt every tick are fine.
//   * Do not keep pointers or references to components across ticks; storage may move.
//   * Events emitted in phase P become readable in phase P+1, never in P, and are dropped at the end
//     of the tick. Do not rely on an event surviving into the next tick.
//   * Do not spawn threads. Parallel work goes through TickContext::parallel_for, with per-chunk
//     output merged in chunk order (§7.2).
// ============================================================================================

namespace citysim::core {

class Simulation {
  public:
    // `worker_count` is passed to the JobSystem: 0 picks a default from the hardware, 1 means no helper
    // threads. The simulation result never depends on it (architecture §7.2).
    explicit Simulation(std::uint64_t world_seed, std::size_t worker_count = 0);
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    // Not movable: the scheduler refers to the world this object owns.
    Simulation(Simulation&&) = delete;
    Simulation& operator=(Simulation&&) = delete;

    // Runs exactly one tick.
    void step();

    // Number of completed ticks; during step() it is the tick being simulated.
    std::uint64_t tick() const { return tick_; }
    std::uint64_t world_seed() const { return world_seed_; }

    World& world() { return world_; }
    const World& world() const { return world_; }
    Scheduler& scheduler() { return scheduler_; }
    const Scheduler& scheduler() const { return scheduler_; }
    const EventBus& events() const { return events_; }

    // Applies the pending commands now, without running a tick or advancing tick(): used by the runner
    // while the game is paused so the UI can see the effect. Only the dispatcher runs: no systems, and
    // events emitted by handlers are discarded, since nothing is listening between ticks.
    void apply_commands_only();

    // Called by the runner between ticks with commands drained from a CommandQueue. They are applied
    // in sequence order during the next step(), whatever order they arrive in. Simulation itself is
    // not thread-safe: only the thread that calls step() may call this.
    void enqueue(std::vector<SequencedCommand> commands);
    std::size_t pending_command_count() const { return pending_commands_.size(); }

    // Commands waiting for the next step(), in arrival order; saved with the game.
    const std::vector<SequencedCommand>& pending_commands() const { return pending_commands_; }

    // Sets the loop state a save recorded, after the World has been loaded: the seed, the tick counter, the
    // commands that were waiting and the input log so far. For save loading only; the Simulation must be
    // fresh (tick() == 0, nothing pending).
    void restore_loop_state(std::uint64_t world_seed, std::uint64_t tick,
                            std::vector<SequencedCommand> pending_commands,
                            std::vector<InputLogEntry> input_log);

    CommandDispatcher& dispatcher() { return dispatcher_; }
    const InputLog& input_log() const { return input_log_; }

    // Convenience for scheduler().add(...).
    ISystem& add_system(Phase phase, std::unique_ptr<ISystem> system) {
        return scheduler_.add(phase, std::move(system));
    }

    // Not owned. Pass nullptr to remove.
    void set_observer(SystemObserver* observer) { observer_ = observer; }
    JobSystem& jobs() { return jobs_; }

  private:
    void apply_commands(TickContext& context);

    std::uint64_t world_seed_;
    std::uint64_t tick_ = 0;
    World world_;
    Scheduler scheduler_;
    EventBus events_;
    CommandDispatcher dispatcher_;
    InputLog input_log_;
    std::vector<SequencedCommand> pending_commands_;
    SystemObserver* observer_ = nullptr;
    JobSystem jobs_;
};

} // namespace citysim::core
