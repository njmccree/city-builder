#include "citysim/time/calendar_system.hpp"

#include "citysim/core/tick_context.hpp"
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
    advance_calendar(state, calendar_denominator(era, defines.base_tick_rate));
}

} // namespace citysim::time
