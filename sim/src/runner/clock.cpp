#include "citysim/runner/clock.hpp"

#include <chrono>
#include <stdexcept>

namespace citysim::runner {

// ---- SteadyClock ----------------------------------------------------------------------------------

double SteadyClock::now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void SteadyClock::sleep_until(double deadline) {
    const auto target =
        std::chrono::steady_clock::time_point(std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(deadline)));
    std::unique_lock lock(mutex_);
    cv_.wait_until(lock, target, [this] { return interrupted_; });
    interrupted_ = false;
}

void SteadyClock::interrupt() {
    {
        const std::lock_guard lock(mutex_);
        interrupted_ = true;
    }
    cv_.notify_all();
}

// ---- FakeClock ------------------------------------------------------------------------------------

double FakeClock::now_seconds() {
    const std::lock_guard lock(mutex_);
    return now_;
}

void FakeClock::sleep_until(double deadline) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return now_ >= deadline || interrupted_; });
    interrupted_ = false;
}

void FakeClock::interrupt() {
    {
        const std::lock_guard lock(mutex_);
        interrupted_ = true;
    }
    cv_.notify_all();
}

void FakeClock::advance(double seconds) {
    if (seconds < 0.0) {
        throw std::invalid_argument("FakeClock cannot go backwards");
    }
    {
        const std::lock_guard lock(mutex_);
        now_ += seconds;
    }
    cv_.notify_all();
}

void FakeClock::set_time(double seconds) {
    {
        const std::lock_guard lock(mutex_);
        if (seconds < now_) {
            throw std::invalid_argument("FakeClock cannot go backwards");
        }
        now_ = seconds;
    }
    cv_.notify_all();
}

} // namespace citysim::runner
