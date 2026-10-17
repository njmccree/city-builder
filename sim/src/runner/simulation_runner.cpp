#include "citysim/runner/simulation_runner.hpp"

#include "citysim/core/log.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace citysim::runner {

SimulationRunner::SimulationRunner(std::uint64_t world_seed, IClock& clock, RunnerConfig config,
                                   std::size_t worker_count)
    : clock_(clock), config_(config), simulation_(world_seed, worker_count) {
    for (std::size_t speed = 1; speed < config_.ticks_per_second.size(); ++speed) {
        if (!(config_.ticks_per_second[speed] > 0.0)) {
            throw std::invalid_argument("every speed in the table needs a positive tick rate");
        }
    }
    if (config_.max_catchup_ticks == 0) {
        throw std::invalid_argument("max_catchup_ticks must be at least 1");
    }
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

void SimulationRunner::set_speed(std::uint8_t speed) {
    if (speed < 1 || speed >= config_.ticks_per_second.size()) {
        throw std::out_of_range("speed must be 1..3");
    }
    {
        const std::lock_guard lock(mutex_);
        speed_ = speed;
    }
    note_stimulus();
}

void SimulationRunner::set_paused(bool paused) {
    {
        const std::lock_guard lock(mutex_);
        paused_ = paused;
    }
    note_stimulus();
}

RenderSnapshot SimulationRunner::latest_snapshot() {
    return snapshots_.read();
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

void SimulationRunner::publish_snapshot(std::uint8_t speed, bool paused) {
    RenderSnapshot& snapshot = snapshots_.back();
    snapshot.tick = simulation_.tick();
    snapshot.speed = speed;
    snapshot.paused = paused;
    snapshot.published_at = clock_.now_seconds();
    snapshot.stats = stats_;
    snapshot.stats.entity_count = simulation_.world().entity_count();
    snapshot.ui_events.clear(); // the slot holds an older snapshot's data
    if (publish_hook_) {
        publish_hook_(simulation_, snapshot);
    }
    snapshots_.publish();
    published_tick_.store(simulation_.tick());
}

void SimulationRunner::loop_body() {
    std::vector<core::SequencedCommand> batch;
    const auto drain_into_simulation = [&] {
        batch.clear();
        queue_.drain(batch);
        simulation_.enqueue(batch);
        return !batch.empty();
    };

    // Ticks are scheduled as origin + (n + 1) * interval, a multiplication rather than a running sum, so
    // the schedule does not accumulate rounding error. The origin is reset whenever the pace changes.
    double origin = 0.0;
    std::uint64_t ticks_since_origin = 0;
    std::uint8_t origin_speed = 0;
    bool need_rebase = true;

    while (!stop_.load()) {
        // Read the stimulus counter first, then the state it covers. Anything that arrives later bumps the
        // counter again and (via the sticky interrupt or the wait predicate) brings us around once more.
        std::uint64_t seq = 0;
        std::uint8_t speed = 0;
        bool paused = false;
        {
            const std::lock_guard lock(mutex_);
            seq = stimulus_seq_;
            speed = speed_;
            paused = paused_;
        }

        if (paused) {
            // Commands still apply, without advancing the tick, so the UI can see their effect.
            if (drain_into_simulation()) {
                simulation_.apply_commands_only();
            }
            publish_snapshot(speed, true);
            need_rebase = true; // no catch-up burst for the time spent paused

            std::unique_lock lock(mutex_);
            state_ = State::SleepingPaused;
            handled_seq_ = seq;
            state_cv_.notify_all();
            wake_cv_.wait(lock, [&] { return stop_.load() || stimulus_seq_ != seq; });
            state_ = State::Running;
            continue;
        }

        if (need_rebase || speed != origin_speed) {
            origin = clock_.now_seconds();
            ticks_since_origin = 0;
            origin_speed = speed;
            need_rebase = false;
        }

        const double interval = 1.0 / config_.ticks_per_second[speed];
        const auto next_deadline = [&] {
            return origin + static_cast<double>(ticks_since_origin + 1) * interval;
        };

        if (clock_.now_seconds() < next_deadline()) {
            set_state(State::SleepingClock, seq, next_deadline());
            clock_.sleep_until(next_deadline());
            set_state(State::Running, seq);
            continue;
        }

        // Due. Run what is owed, but never more than max_catchup_ticks in a row.
        std::uint32_t ran = 0;
        while (ran < config_.max_catchup_ticks && !stop_.load() && clock_.now_seconds() >= next_deadline()) {
            drain_into_simulation();
            const double started = clock_.now_seconds();
            simulation_.step();
            const double finished = clock_.now_seconds();
            ++ticks_since_origin;
            ++ran;
            record_tick_stats(started, finished);
            publish_snapshot(speed, false);
        }
        if (clock_.now_seconds() >= next_deadline()) {
            // Still behind: drop the backlog and slow the game down rather than spiral.
            origin = clock_.now_seconds();
            ticks_since_origin = 0;
        }
    }
}

} // namespace citysim::runner
