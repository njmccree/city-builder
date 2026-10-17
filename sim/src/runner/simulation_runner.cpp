#include "citysim/runner/simulation_runner.hpp"

#include "citysim/core/commands.hpp"
#include "citysim/core/log.hpp"
#include "citysim/save/save_format_error.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/time/calendar_state.hpp"
#include "citysim/time/game_clock.hpp"
#include "citysim/time/time_module.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <variant>

namespace citysim::runner {

SimulationRunner::SimulationRunner(std::uint64_t world_seed, IClock& clock, RunnerConfig config,
                                   std::size_t worker_count)
    : clock_(clock), config_(config), world_seed_(world_seed), worker_count_(worker_count) {
    if (config_.max_catchup_ticks == 0) {
        throw std::invalid_argument("max_catchup_ticks must be at least 1");
    }
    simulation_ = make_simulation();
}

std::unique_ptr<core::Simulation> SimulationRunner::make_simulation() {
    auto simulation = std::make_unique<core::Simulation>(world_seed_, worker_count_);
    // Validates the defines (throws std::invalid_argument) and gives the simulation its GameClock, which
    // owns speed and pause.
    time::install(*simulation, config_.time);
    if (setup_) {
        setup_(*simulation);
    }
    return simulation;
}

void SimulationRunner::configure(std::function<void(core::Simulation&)> setup) {
    if (started_) {
        throw std::logic_error("SimulationRunner::configure must be called before start()");
    }
    setup_ = std::move(setup);
    setup_(*simulation_);
}

SimulationRunner::~SimulationRunner() {
    stop();
}

void SimulationRunner::start() {
    if (started_) {
        throw std::logic_error("SimulationRunner already started");
    }
    started_ = true;
    CS_LOG_INFO(Runner, "starting simulation thread");
    thread_ = std::thread([this] { run_loop(); });
}

void SimulationRunner::stop() {
    if (!thread_.joinable()) {
        return;
    }
    CS_LOG_INFO(Runner, "stopping simulation thread");
    stop_.store(true);
    note_stimulus(); // takes the lock, so a runner about to wait cannot miss the stop
    thread_.join();
}

void SimulationRunner::note_stimulus() {
    {
        const std::lock_guard lock(mutex_);
        ++stimulus_seq_;
    }
    wake_cv_.notify_all();
    clock_.interrupt();
}

std::uint64_t SimulationRunner::submit(core::Command cmd) {
    const std::uint64_t sequence = queue_.submit(std::move(cmd), published_tick_.load());
    note_stimulus();
    return sequence;
}

RenderSnapshot SimulationRunner::latest_snapshot() {
    RenderSnapshot snapshot = snapshots_.read();
    // Result events stay in snapshots until read; hand each over once.
    std::erase_if(snapshot.ui_events, [this](const UiEvent& event) {
        return event.serial != 0 && event.serial <= seen_event_serial_;
    });
    for (const UiEvent& event : snapshot.ui_events) {
        seen_event_serial_ = std::max(seen_event_serial_, event.serial);
    }
    consumed_event_serial_.store(seen_event_serial_);
    return snapshot;
}

std::string SimulationRunner::failure() const {
    const std::lock_guard lock(mutex_);
    return failure_;
}

bool SimulationRunner::wait_until_idle(double timeout_seconds) {
    std::unique_lock lock(mutex_);
    return state_cv_.wait_for(lock, std::chrono::duration<double>(timeout_seconds), [this] {
        if (state_ == State::Stopped) {
            return true;
        }
        if (handled_seq_ < stimulus_seq_) {
            return false; // something was submitted that the runner has not dealt with yet
        }
        if (state_ == State::SleepingPaused) {
            return true;
        }
        // Asleep on the clock counts as idle only if nothing has come due in the meantime.
        return state_ == State::SleepingClock && clock_.now_seconds() < sleep_deadline_;
    });
}

void SimulationRunner::set_state(State state, std::uint64_t handled_seq, double deadline) {
    {
        const std::lock_guard lock(mutex_);
        state_ = state;
        handled_seq_ = handled_seq;
        sleep_deadline_ = deadline;
    }
    state_cv_.notify_all();
}

void SimulationRunner::run_loop() {
    try {
        loop_body();
    } catch (const std::exception& error) {
        CS_LOG_ERROR(Runner, "simulation thread failed: {}", error.what());
        const std::lock_guard lock(mutex_);
        failure_ = error.what();
    } catch (...) {
        const std::lock_guard lock(mutex_);
        failure_ = "unknown exception";
    }
    {
        const std::lock_guard lock(mutex_);
        state_ = State::Stopped;
    }
    state_cv_.notify_all();
}

void SimulationRunner::record_tick_stats(double started, double finished) {
    const double duration_ms = (finished - started) * 1000.0;
    tick_durations_[stats_next_] = duration_ms;
    tick_finish_times_[stats_next_] = finished;
    stats_next_ = (stats_next_ + 1) % tick_durations_.size();
    stats_count_ = std::min(stats_count_ + 1, tick_durations_.size());

    double sum = 0.0;
    for (std::size_t i = 0; i < stats_count_; ++i) {
        sum += tick_durations_[i];
    }
    stats_.last_tick_ms = duration_ms;
    stats_.avg_tick_ms = sum / static_cast<double>(stats_count_);

    // Oldest and newest finish times among the samples still in the ring.
    const std::size_t newest = (stats_next_ + tick_durations_.size() - 1) % tick_durations_.size();
    const std::size_t oldest = stats_count_ < tick_durations_.size() ? 0 : stats_next_;
    const double span = tick_finish_times_[newest] - tick_finish_times_[oldest];
    stats_.ticks_per_second =
        (stats_count_ >= 2 && span > 0.0) ? static_cast<double>(stats_count_ - 1) / span : 0.0;
}

void SimulationRunner::publish_snapshot() {
    const auto& game_clock = simulation_->world().ctx().get<time::GameClock>();
    RenderSnapshot& snapshot = snapshots_.back();
    snapshot.tick = simulation_->tick();
    snapshot.speed = game_clock.speed;
    snapshot.paused = game_clock.paused;
    snapshot.published_at = clock_.now_seconds();
    snapshot.stats = stats_;
    snapshot.stats.entity_count = simulation_->world().entity_count();
    snapshot.has_date = false;
    if (const auto* calendar = simulation_->world().ctx().find<time::CalendarState>()) {
        const auto& defines = simulation_->world().ctx().get<time::TimeDefines>();
        snapshot.has_date = true;
        snapshot.date = time::current_date(*calendar);
        snapshot.hour = static_cast<std::uint8_t>(time::hour(game_clock, defines));
        snapshot.minute = static_cast<std::uint8_t>(time::minute(game_clock, defines));
    }
    snapshot.ui_events.clear(); // the slot holds an older snapshot's data
    snapshot.channels.clear();
    snapshot.grids.clear();
    const std::uint64_t consumed = consumed_event_serial_.load();
    std::erase_if(pending_ui_events_, [consumed](const UiEvent& event) { return event.serial <= consumed; });
    snapshot.ui_events = pending_ui_events_; // a copy: they stay until the consumer has read them
    if (publish_hook_) {
        publish_hook_(*simulation_, snapshot);
    }
    snapshots_.publish();
    published_tick_.store(simulation_->tick());
}

void SimulationRunner::push_result_event(UiEvent event) {
    constexpr std::size_t k_max_unread_results = 64; // a consumer that never reads cannot grow this forever
    event.serial = next_event_serial_++;
    pending_ui_events_.push_back(std::move(event));
    if (pending_ui_events_.size() > k_max_unread_results) {
        pending_ui_events_.erase(pending_ui_events_.begin());
    }
}

bool SimulationRunner::handle_batch(std::vector<core::SequencedCommand>& batch) {
    bool replaced = false;
    std::vector<core::SequencedCommand> ordinary;
    // Ordinary commands go to the simulation in sequence order; a save or load happens at its place in that
    // order, so a save includes the commands submitted before it (as pending) and none submitted after.
    const auto flush = [&] {
        simulation_->enqueue(std::move(ordinary));
        ordinary.clear();
    };
    for (core::SequencedCommand& command : batch) {
        if (const auto* save = std::get_if<core::SaveGame>(&command.cmd)) {
            flush();
            do_save(save->path, false);
            publish_snapshot(); // report it now rather than at the next tick
        } else if (const auto* load = std::get_if<core::LoadGame>(&command.cmd)) {
            flush();
            replaced = do_load(load->path) || replaced;
            publish_snapshot();
        } else if (std::holds_alternative<core::RequestStateHash>(command.cmd)) {
            flush();
            UiEvent event;
            event.kind = UiEventKind::StateHash;
            event.hash = simulation_->world().state_hash();
            event.tick = simulation_->tick();
            push_result_event(std::move(event));
            publish_snapshot();
        } else {
            ordinary.push_back(std::move(command));
        }
    }
    flush();
    return replaced;
}

void SimulationRunner::do_save(const std::filesystem::path& path, bool autosave) {
    UiEvent event;
    event.kind = UiEventKind::SaveCompleted;
    event.path = path.generic_string();
    event.autosave = autosave;
    try {
        save::SaveOptions options;
        options.content = config_.content;
        options.saved_at_unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count();
        save::save_simulation(*simulation_, path, options);
        CS_LOG_INFO(Runner, "saved {} at tick {}", event.path, simulation_->tick());
    } catch (const std::exception& error) {
        event.ok = false;
        event.text = error.what();
        CS_LOG_WARN(Runner, "saving {} failed: {}", event.path, event.text);
    }
    push_result_event(std::move(event));
}

bool SimulationRunner::do_load(const std::filesystem::path& path) {
    UiEvent event;
    event.kind = UiEventKind::LoadCompleted;
    event.path = path.generic_string();
    try {
        // Into a new Simulation, so a bad file leaves the running game untouched.
        std::unique_ptr<core::Simulation> loaded = make_simulation();
        save::LoadOptions options;
        options.content = config_.content;
        options.migrations = config_.migrations;
        save::LoadOutcome outcome = save::load_simulation(*loaded, path, options);
        event.warnings = std::move(outcome.warnings);
        simulation_ = std::move(loaded);
        // The pace follows the loaded game's defines (the autosave settings stay as configured).
        config_.time = simulation_->world().ctx().get<time::TimeDefines>();
        stats_count_ = 0;
        stats_next_ = 0;
        CS_LOG_INFO(Runner, "loaded {} (tick {})", event.path, simulation_->tick());
        push_result_event(std::move(event));
        return true;
    } catch (const std::exception& error) {
        event.ok = false;
        event.text = error.what();
        CS_LOG_WARN(Runner, "loading {} failed: {}", event.path, event.text);
        push_result_event(std::move(event));
        return false;
    }
}

double SimulationRunner::autosave_interval() const {
    if (config_.autosave_directory.empty()) {
        return 0.0;
    }
    return static_cast<double>(config_.time.autosave_interval_real_seconds);
}

void SimulationRunner::run_autosave() {
    const std::uint32_t slots = config_.time.autosave_slots;
    std::error_code ec;
    std::filesystem::create_directories(config_.autosave_directory, ec);
    const auto path =
        config_.autosave_directory / ("autosave_" + std::to_string(next_autosave_slot_) + ".csav");
    next_autosave_slot_ = next_autosave_slot_ % slots + 1;
    do_save(path, true);
    publish_snapshot(); // so the UI hears about it even while ticks are far apart
}

void SimulationRunner::loop_body() {
    std::vector<core::SequencedCommand> batch;
    // Drains the queue into the simulation, running saves and loads on the way. True if a load replaced the
    // simulation, which makes the pacing below start over.
    const auto drain_into_simulation = [&] {
        batch.clear();
        queue_.drain(batch);
        return handle_batch(batch);
    };
    // Speed and pause belong to the simulation (GameClock), so they replay like any other input.
    const auto read_clock = [&]() -> const time::GameClock& {
        return simulation_->world().ctx().get<time::GameClock>();
    };
    const auto tick_rate = [&](std::uint8_t speed) {
        return static_cast<double>(config_.time.base_tick_rate) *
               static_cast<double>(config_.time.speed_multipliers[speed]);
    };

    // Ticks are scheduled as origin + (n + 1) * interval, a multiplication rather than a running sum, so
    // the schedule does not accumulate rounding error. The origin is reset whenever the pace changes.
    double origin = 0.0;
    std::uint64_t ticks_since_origin = 0;
    std::uint8_t origin_speed = 0;
    bool need_rebase = true;
    bool rearm_autosave = true; // at the start, after a pause and after a load

    publish_snapshot(); // so the front end has the starting state (tick 0, the date) before the first tick

    while (!stop_.load()) {
        // Read the stimulus counter first, then the state it covers. Anything that arrives later bumps the
        // counter again and (via the sticky interrupt or the wait predicate) brings us around once more.
        std::uint64_t seq = 0;
        {
            const std::lock_guard lock(mutex_);
            seq = stimulus_seq_;
        }
        if (drain_into_simulation()) {
            need_rebase = true;
            rearm_autosave = true;
        }

        if (read_clock().paused) {
            // Commands still apply, without advancing the tick, so the UI can see their effect. One of them
            // may be the TogglePause that ends the pause.
            if (simulation_->pending_command_count() > 0) {
                simulation_->apply_commands_only();
            }
            publish_snapshot(); // the UI sees the commands' effects, including a pause that just ended
            if (!read_clock().paused) {
                continue; // unpaused: go around again and start ticking
            }
            need_rebase = true; // no catch-up burst for the time spent paused
            rearm_autosave = true;

            std::unique_lock lock(mutex_);
            state_ = State::SleepingPaused;
            handled_seq_ = seq;
            state_cv_.notify_all();
            wake_cv_.wait(lock, [&] { return stop_.load() || stimulus_seq_ != seq; });
            state_ = State::Running;
            continue;
        }

        // Autosave runs on the runner's clock, not on ticks, so it still happens at any game speed.
        if (autosave_interval() > 0.0) {
            if (rearm_autosave) {
                autosave_due_ = clock_.now_seconds() + autosave_interval();
                rearm_autosave = false;
            } else if (clock_.now_seconds() >= autosave_due_) {
                run_autosave();
                autosave_due_ = clock_.now_seconds() + autosave_interval();
            }
        }

        const std::uint8_t speed = read_clock().speed;
        if (need_rebase || speed != origin_speed) {
            origin = clock_.now_seconds();
            ticks_since_origin = 0;
            origin_speed = speed;
            need_rebase = false;
        }

        const double interval = 1.0 / tick_rate(speed);
        const auto next_deadline = [&] {
            return origin + static_cast<double>(ticks_since_origin + 1) * interval;
        };

        if (clock_.now_seconds() < next_deadline()) {
            double wake_at = next_deadline();
            if (autosave_interval() > 0.0) {
                wake_at = std::min(wake_at, autosave_due_);
            }
            set_state(State::SleepingClock, seq, wake_at);
            clock_.sleep_until(wake_at);
            set_state(State::Running, seq);
            continue;
        }

        // Due. Run what is owed, but never more than max_catchup_ticks in a row.
        std::uint32_t ran = 0;
        bool pace_changed = false;
        while (ran < config_.max_catchup_ticks && !stop_.load() && clock_.now_seconds() >= next_deadline()) {
            if (drain_into_simulation()) {
                need_rebase = true; // a load replaced the game: start the pacing over
                rearm_autosave = true;
                pace_changed = true;
                break;
            }
            const double started = clock_.now_seconds();
            simulation_->step();
            const double finished = clock_.now_seconds();
            ++ticks_since_origin;
            ++ran;
            record_tick_stats(started, finished);
            publish_snapshot();
            if (read_clock().paused || read_clock().speed != speed) {
                pace_changed = true; // a command in that tick changed the pace: start over from the top
                break;
            }
        }
        if (!pace_changed && clock_.now_seconds() >= next_deadline()) {
            // Still behind: drop the backlog and slow the game down rather than spiral.
            origin = clock_.now_seconds();
            ticks_since_origin = 0;
        }
    }
}

} // namespace citysim::runner
