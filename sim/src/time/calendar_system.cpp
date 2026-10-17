#include "citysim/time/calendar_system.hpp"

#include "citysim/core/tick_context.hpp"
#include "citysim/time/calendar_events.hpp"
#include "citysim/time/calendar_scheduler.hpp"
#include "citysim/time/time_defines.hpp"

#include <limits>
#include <stdexcept>

namespace citysim::time {

std::uint64_t calendar_denominator(const content::EraDef& era, std::uint32_t base_tick_rate) {
    return static_cast<std::uint64_t>(era.real_seconds_per_year) * base_tick_rate;
}

std::int64_t advance_calendar(CalendarState& state, std::uint64_t denominator) {
    if (denominator == 0) {
        throw std::invalid_argument("calendar denominator must be positive");
    }
    state.remainder += static_cast<std::uint64_t>(k_seconds_per_year);
    const std::uint64_t seconds = state.remainder / denominator;
    state.remainder %= denominator;
    state.now += static_cast<std::int64_t>(seconds);
    return static_cast<std::int64_t>(seconds);
}

std::uint64_t rescale_remainder(std::uint64_t remainder, std::uint64_t old_denominator,
                                std::uint64_t new_denominator) {
    if (remainder == 0 || old_denominator == 0) {
        return 0;
    }
    // remainder < old_denominator, so remainder * new / old < new and the result is a valid remainder. The
    // product can only overflow for denominators above about 4 billion (an era years-per-second value no
    // game would use); then the fraction, which is under one second, is dropped rather than risking a wrong
    // value.
    if (new_denominator > std::numeric_limits<std::uint64_t>::max() / remainder) {
        return 0;
    }
    return remainder * new_denominator / old_denominator;
}

void CalendarSystem::update(core::TickContext& context) {
    auto& state = context.world.ctx().get<CalendarState>();
    const auto& defines = context.world.ctx().get<TimeDefines>();
    const content::EraDef& era = content_.registry<content::EraDef>().get(state.era);

    const CalendarTime before = state.now;
    advance_calendar(state, calendar_denominator(era, defines.base_tick_rate));

    // One event per day boundary crossed, oldest first; a fast era can cross many in one tick. At a midnight
    // that also starts a month or a year the coarser event comes first.
    for (std::int64_t day = day_number(before) + 1; day <= day_number(state.now); ++day) {
        const Date date = to_date(day * k_seconds_per_day);
        if (date.month == 1 && date.day == 1) {
            context.events.emit(core::Event{YearStarted{date.year}});
        }
        if (date.day == 1) {
            context.events.emit(core::Event{MonthStarted{date}});
        }
        context.events.emit(core::Event{DayStarted{date}});
    }

    // Then whatever has come due, in (time, insertion order).
    for (CalendarScheduler::Entry& entry : context.world.ctx().get<CalendarScheduler>().pop_due(state.now)) {
        context.events.emit(core::Event{ScheduledActionDue{entry.when, std::move(entry.action)}});
    }
}

} // namespace citysim::time
