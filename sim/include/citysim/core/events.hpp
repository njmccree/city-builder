#pragma once

#include "citysim/time/calendar_events.hpp"

#include <string>
#include <variant>

namespace citysim::core {

// Central list of simulation events (architecture §5.5). Later tasks add their event structs here and
// append them to `Event`, the same way commands are listed in commands.hpp.
struct DebugEvent {
    std::string text;
};

using Event = std::variant<DebugEvent, time::DayStarted, time::MonthStarted, time::YearStarted,
                           time::ScheduledActionDue>;

} // namespace citysim::core
