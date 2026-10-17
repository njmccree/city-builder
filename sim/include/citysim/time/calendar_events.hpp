#pragma once

#include "citysim/time/calendar.hpp"
#include "citysim/time/scheduled_action.hpp"

#include <cstdint>

namespace citysim::time {

// Events the calendar emits (architecture §5.5). They are listed in the central `Event` variant in
// citysim/core/events.hpp. Emitted in the Calendar phase, so readable from the Publish phase.

// The first moment of a day, month or year has been reached. When the calendar advances several days in one
// tick, there is one event per boundary, in chronological order. For one midnight that starts a month or a
// year, the coarser event comes first: YearStarted, then MonthStarted, then DayStarted. The day the game
// starts on does not produce events: only boundaries crossed after that.
struct DayStarted {
    Date date;
};
struct MonthStarted {
    Date date; // the first day of the month
};
struct YearStarted {
    std::int32_t year = 0;
};

// A scheduled action has come due. `when` is the time it was scheduled for, not the time it fired.
struct ScheduledActionDue {
    CalendarTime when = 0;
    ScheduledAction action;
};

} // namespace citysim::time
