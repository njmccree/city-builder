#pragma once

#include "citysim/core/command_queue.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/runner/clock.hpp"
#include "citysim/runner/render_snapshot.hpp"
#include "citysim/runner/triple_buffer.hpp"
#include "citysim/time/time_defines.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace citysim::runner {

struct RunnerConfig {
    // The pace comes from the time defines (architecture §6): ticks per second = base_tick_rate x the
    // multiplier of the current speed (speed 1..3 = 1x, 2x, 4x by default). Content overrides them through
    // defines/time.json.
    time::TimeDefines time;
    // When the runner falls behind it runs at most this many ticks back to back, then drops the backlog
    // so the game slows down instead of spiralling.
    std::uint32_t max_catchup_ticks = 5;
};

// Runs a Simulation on its own thread at the pace of the current speed (architecture §7.1). This is the
// only place real time enters the simulation, and it does so only through the IClock it is given. The
// UI thread talks to it through submit() (in) and latest_snapshot() (out) and nothing else.
//
// Speed and pause live in the simulation's GameClock and are changed with SetSpeed / TogglePause commands
// like every other input, so they are recorded in the input log and replay correctly.
//
// Threading: submit() may be called from any thread. latest_snapshot() from
// ONE consumer thread. simulation() and set_publish_hook() only while the runner is not running.
class SimulationRunner {
  public:
    using PublishHook = std::function<void(core::Simulation&, RenderSnapshot&)>;

    SimulationRunner(std::uint64_t world_seed, IClock& clock, RunnerConfig config = {},
                     std::size_t worker_count = 0);
    ~SimulationRunner(); // stops the thread if it is still running
    SimulationRunner(const SimulationRunner&) = delete;
    SimulationRunner& operator=(const SimulationRunner&) = delete;

    // For setting up systems before start() and inspecting state after stop(). Never touch it while the
    // runner thread is running.
    core::Simulation& simulation() { return simulation_; }

    // Called on the runner thread after every tick and before publishing, so other modules can add data
    // to the snapshot. Set it before start().
    void set_publish_hook(PublishHook hook) { publish_hook_ = std::move(hook); }

    // Throws std::logic_error if already started. A runner that has been stopped cannot be restarted.
    void start();
    // Joins the thread. Safe to call more than once and when never started.
    void stop();

    // Queues a command for the next tick (or applies it immediately while paused, without advancing the
    // tick). Returns its sequence number. A SetSpeed or TogglePause takes effect from the tick that consumes
    // it.
    std::uint64_t submit(core::Command cmd);

    // The newest published snapshot (a copy). Call from one consumer thread only.
    RenderSnapshot latest_snapshot();

    // Set if the simulation threw on the runner thread; the thread has then stopped.
    std::string failure() const;

    // For tests and tools: blocks (in real time, up to `timeout_seconds`) until the runner thread has
    // handled everything submitted so far and is waiting with nothing due at the clock's current time.
    // With a FakeClock this makes tests deterministic: advance the clock, then wait_until_idle().
    bool wait_until_idle(double timeout_seconds = 10.0);

  private:
    enum class State { Running, SleepingClock, SleepingPaused, Stopped };

    void run_loop();
    void loop_body();
    void publish_snapshot();
    void record_tick_stats(double started, double finished);
    void set_state(State state, std::uint64_t handled_seq, double deadline = 0.0);
    void note_stimulus();

    IClock& clock_;
    RunnerConfig config_;
    core::Simulation simulation_;
    core::CommandQueue queue_;
    TripleBuffer<RenderSnapshot> snapshots_;
    PublishHook publish_hook_;
    std::thread thread_;
    bool started_ = false;

    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> published_tick_{0};

    // Everything below is guarded by mutex_.
    mutable std::mutex mutex_;
    std::condition_variable wake_cv_;  // wakes the runner while paused
    std::condition_variable state_cv_; // wakes wait_until_idle()
    std::uint64_t stimulus_seq_ = 0;   // bumped by every submit and by stop
    std::uint64_t handled_seq_ = 0;    // the stimulus the runner last fully handled before going idle
    State state_ = State::Running;
    double sleep_deadline_ = 0.0;
    std::string failure_;

    // Runner-thread only.
    std::array<double, 60> tick_durations_{};
    std::array<double, 60> tick_finish_times_{};
    std::size_t stats_count_ = 0;
    std::size_t stats_next_ = 0;
    SimStats stats_;
};

} // namespace citysim::runner
