#pragma once

#include <condition_variable>
#include <mutex>

namespace citysim::runner {

// Time source for SimulationRunner. Wall-clock time is allowed only in sim/src/runner and
// sim/src/profiling (architecture §4 rule 2); everything else sees simulation time only.
class IClock {
  public:
    virtual ~IClock() = default;

    // Seconds since an arbitrary, fixed origin. Never goes backwards.
    virtual double now_seconds() = 0;

    // Blocks until now_seconds() >= deadline, or until interrupt() is called.
    virtual void sleep_until(double deadline) = 0;

    // Makes the current or next sleep_until() return early, once. The "next" part matters: it closes the
    // race where a thread calls interrupt() just before the runner thread starts sleeping.
    // sleep_until() may also return early for no reason, so callers must re-check their condition.
    virtual void interrupt() = 0;
};

// The real clock (std::chrono::steady_clock).
class SteadyClock final : public IClock {
  public:
    double now_seconds() override;
    void sleep_until(double deadline) override;
    void interrupt() override;

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool interrupted_ = false;
};

// A clock that only moves when a test says so, so runner tests need no real sleeping and cannot flake
// on timing. Thread-safe.
class FakeClock final : public IClock {
  public:
    double now_seconds() override;
    void sleep_until(double deadline) override;
    void interrupt() override;

    // Move time forward and wake any sleeper whose deadline has passed. set_time() must not go backwards.
    void advance(double seconds);
    void set_time(double seconds);

  private:
    std::mutex mutex_;
    std::condition_variable cv_;
    double now_ = 0.0;
    bool interrupted_ = false;
};

} // namespace citysim::runner
