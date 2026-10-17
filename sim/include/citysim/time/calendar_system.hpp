#pragma once

#include "citysim/content/content_database.hpp"
#include "citysim/core/isystem.hpp"
#include "citysim/time/calendar_state.hpp"

#include <cstdint>

namespace citysim::time {

// How many ticks it takes the calendar to advance the amount `denominator` stands for: a year takes exactly
// real_seconds_per_year * base_tick_rate ticks at 1x (architecture §6).
std::uint64_t calendar_denominator(const content::EraDef& era, std::uint32_t base_tick_rate);

// Advances `state` by one tick of an era whose denominator is `denominator`:
//
//     seconds per tick = (365 * 86400) / denominator
//
// in exact integer math. Each tick adds the numerator (365 * 86400) to `remainder`; the whole seconds in it
// move the clock and what is left over carries to the next tick. After T ticks the clock has therefore
// advanced by exactly floor(T * 365 * 86400 / denominator) seconds: it can never drift, however long the game
// runs. Returns the number of seconds the clock moved. Throws std::invalid_argument for a zero denominator.
std::int64_t advance_calendar(CalendarState& state, std::uint64_t denominator);

// Switching eras keeps the clock and carries the fractional second over, rescaled from the old denominator to
// the new one (rounded down, so under a thousandth of a second is lost per switch at most). Returns the new
// remainder.
std::uint64_t rescale_remainder(std::uint64_t remainder, std::uint64_t old_denominator,
                                std::uint64_t new_denominator);

// Advances the calendar each tick, in the Calendar phase, at the pace of the current era. The content
// database must outlive the simulation.
class CalendarSystem final : public core::ISystem {
  public:
    explicit CalendarSystem(const content::ContentDatabase& content) : content_(content) {}

    std::string_view name() const override { return "time.calendar"; }
    void update(core::TickContext& context) override;

  private:
    const content::ContentDatabase& content_;
};

} // namespace citysim::time
