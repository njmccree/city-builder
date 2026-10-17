#pragma once

#include "citysim/core/phase.hpp"

#include <string_view>

namespace citysim::core {

// Optional hook called around every system update (used later for profiling). The simulation core never
// reads a wall clock itself: an implementation that wants timings reads the clock in these callbacks.
class SystemObserver {
  public:
    virtual ~SystemObserver() = default;
    virtual void before_system(Phase phase, std::string_view system_name) = 0;
    virtual void after_system(Phase phase, std::string_view system_name) = 0;
};

} // namespace citysim::core
