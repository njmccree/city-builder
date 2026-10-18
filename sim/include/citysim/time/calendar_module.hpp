#pragma once

#include "citysim/content/content_database.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/time/calendar.hpp"

#include <optional>
#include <string_view>

namespace citysim::time {

// Wires the calendar into a simulation: the CalendarState singleton, the CalendarSystem (Calendar phase) and
// the SetEra command. The game starts in `era_id` on `start`, which defaults to 1 January of that era's
// default_start_year.
//
// Requires time::install() to have run first (the cadence uses TimeDefines::base_tick_rate). `content` must
// be loaded and must outlive the simulation. Throws std::invalid_argument for an unknown era or an invalid
// start date, std::logic_error if the time module is missing.
//
// SetEra{era_id} takes effect from the tick that consumes it: that tick's Calendar phase already uses the new
// era's cadence. The fractional second carried between ticks moves over, rescaled (see rescale_remainder).
// Unknown era ids are ignored with a warning.
void install_calendar(core::Simulation& simulation, const content::ContentDatabase& content,
                      std::string_view era_id, std::optional<Date> start = std::nullopt);

} // namespace citysim::time
