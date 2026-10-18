#pragma once

#include "citysim/content/localizer.hpp"
#include "citysim/time/calendar.hpp"

#include <string>

namespace citysim::content {

// Renders a FormattedDate through the Localizer, e.g. "15 March 3000 BCE" in English. The month argument is a
// key and is looked up in the current language first.
std::string render_date(const Localizer& localizer, const time::FormattedDate& date);

// format_date(date) rendered through the Localizer.
std::string format_date_text(const Localizer& localizer, const time::Date& date);

} // namespace citysim::content
