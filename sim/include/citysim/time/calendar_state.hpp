#pragma once

#include "citysim/content/def_handle.hpp"
#include "citysim/content/era_def.hpp"
#include "citysim/time/calendar.hpp"

#include <cstdint>

namespace citysim::time {

// The calendar clock (architecture §6): singleton state in the World context, registered so it is hashed and,
// later, saved. Runs on the TICK clock: CalendarSystem advances it once per tick, in the Calendar phase.
struct CalendarState {
    CalendarTime now = 0;        // seconds since the epoch
    std::uint64_t remainder = 0; // sub-second carry, in units of 1/denominator seconds (see advance_calendar)
    content::DefHandle<content::EraDef> era; // the era whose cadence is in force
};

inline Date current_date(const CalendarState& state) {
    return to_date(state.now);
}

} // namespace citysim::time
