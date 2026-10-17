#pragma once

#include "citysim/core/command_queue.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/runner/clock.hpp"
#include "citysim/runner/render_snapshot.hpp"
#include "citysim/runner/triple_buffer.hpp"
#include "citysim/save/migration_registry.hpp"
#include "citysim/time/time_defines.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace citysim::content {
class ContentDatabase;
}

namespace citysim::runner {

struct RunnerConfig {
    // The pace comes from the time defines (architecture §6): ticks per second = base_tick_rate x the
    // multiplier of the current speed (speed 1..3 = 1x, 2x, 4x by default). Content overrides them through
    // defines/time.json.
    time::TimeDefines time;
    // When the runner falls behind it runs at most this many ticks back to back, then drops the backlog
    // so the game slows down instead of spiralling.
    std::uint32_t max_catchup_ticks = 5;

    // Saving and loading (architecture §10). `content` resolves content references in saves and lets a load
    // warn about changed content; `migrations` upgrades saves from older builds. Both must outlive the
    // runner.
    const content::ContentDatabase* content = nullptr;
    const save::MigrationRegistry* migrations = nullptr;
    // Where autosave_1.csav .. autosave_<slots>.csav go (time.autosave_slots, every
    // time.autosave_interval_real_seconds of the runner's clock). Empty turns autosave off.
    std::filesystem::path autosave_directory;
};

// Runs a Simulation on its own thread at the pace of the current speed (architecture §7.1). This is the
// only place real time enters the simulation, and it does so only through the IClock it is given. The
// UI thread talks to it through submit() (in) and latest_snapshot() (out) and nothing else.
//
// SaveGame and LoadGame commands are executed here, between ticks and also while paused: a save writes the
// state as it stands (commands not yet applied are saved with it); a load builds a new Simulation, loads the
// file into it, and replaces the old one only if that worked. Either way the outcome reaches the UI as a
// SaveCompleted / LoadCompleted event in a snapshot published right away. A load replaces the whole game
// state, so for it to work the Simulation's systems and handlers must be set up through configure(), which
// the runner can repeat on the new Simulation.
//
// RequestStateHash is handled the same way: the hash of the whole state (World::state_hash) is taken between
// ticks and returned as a StateHash event.
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

    // For inspecting state after stop(), and for quick setup in tests. Never touch it while the runner thread
    // is running. Anything added through it is lost when a LoadGame replaces the Simulation; use configure().
    // The reference is invalidated by a load.
    core::Simulation& simulation() { return *simulation_; }

    // Installs the game's modules, systems and handlers: runs `setup` on the current Simulation now and again
    // on every Simulation a load creates (after the time module is installed). Call before start(); throws
    // std::logic_error afterwards.
    void configure(std::function<void(core::Simulation&)> setup);

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

    // The newest published snapshot (a copy). Call from one consumer thread only. Save and load results
    // (SaveCompleted / LoadCompleted) are never lost to a newer snapshot: they stay in each snapshot until a
    // call has returned them, and are returned exactly once.
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
    std::unique_ptr<core::Simulation> make_simulation();
    // Splits a drained batch: SaveGame / LoadGame run here, everything else goes to the simulation. Returns
    // true if a load replaced the simulation.
    void push_result_event(UiEvent event);
    bool handle_batch(std::vector<core::SequencedCommand>& batch);
    void do_save(const std::filesystem::path& path, bool autosave);
    bool do_load(const std::filesystem::path& path);
    void run_autosave();
    double autosave_interval() const;
    void publish_snapshot();
    void record_tick_stats(double started, double finished);
    void set_state(State state, std::uint64_t handled_seq, double deadline = 0.0);
    void note_stimulus();

    IClock& clock_;
    RunnerConfig config_;
    std::uint64_t world_seed_;
    std::size_t worker_count_;
    std::function<void(core::Simulation&)> setup_;
    std::unique_ptr<core::Simulation> simulation_;
    core::CommandQueue queue_;
    TripleBuffer<RenderSnapshot> snapshots_;
    PublishHook publish_hook_;
    std::thread thread_;
    bool started_ = false;

    std::atomic<bool> stop_{false};
    std::atomic<std::uint64_t> published_tick_{0};
    std::atomic<std::uint64_t> consumed_event_serial_{0}; // newest result event the consumer has been given
    std::uint64_t seen_event_serial_ = 0;                 // consumer thread only

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
    std::vector<UiEvent> pending_ui_events_; // results not yet given to the consumer, oldest first
    std::uint64_t next_event_serial_ = 1;
    std::uint32_t next_autosave_slot_ = 1;
    double autosave_due_ = 0.0;
    std::array<double, 60> tick_durations_{};
    std::array<double, 60> tick_finish_times_{};
    std::size_t stats_count_ = 0;
    std::size_t stats_next_ = 0;
    SimStats stats_;
};

} // namespace citysim::runner
