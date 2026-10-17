#include "citysim/time/calendar.hpp"

#include <limits>
#include <stdexcept>

namespace citysim::time {

namespace {

// Days from the start of the year to the start of each month.
constexpr std::array<std::uint16_t, 13> make_cumulative_days() {
    std::array<std::uint16_t, 13> cumulative{};
    for (std::size_t i = 0; i < 12; ++i) {
        cumulative[i + 1] = static_cast<std::uint16_t>(cumulative[i] + k_days_in_month[i]);
    }
    return cumulative;
}
constexpr std::array<std::uint16_t, 13> k_cumulative_days = make_cumulative_days();
static_assert(k_cumulative_days[12] == 365);

// Division rounding toward negative infinity (C++ truncates toward zero, which is wrong before year 0).
constexpr std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    const std::int64_t quotient = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? quotient - 1 : quotient;
}

} // namespace

bool is_valid(const Date& date) {
    return date.month >= 1 && date.month <= 12 && date.day >= 1 &&
           date.day <= k_days_in_month[date.month - 1];
}

std::uint8_t days_in_month(std::uint8_t month) {
    if (month < 1 || month > 12) {
        throw std::out_of_range("month must be 1..12");
    }
    return k_days_in_month[month - 1];
}

std::uint16_t day_of_year(const Date& date) {
    if (!is_valid(date)) {
        throw std::invalid_argument("invalid date");
    }
    return static_cast<std::uint16_t>(k_cumulative_days[date.month - 1] + date.day);
}

CalendarTime to_calendar_time(const Date& date) {
    if (!is_valid(date)) {
        throw std::invalid_argument("invalid date");
    }
    const std::int64_t days = static_cast<std::int64_t>(date.year) * k_days_per_year +
                              static_cast<std::int64_t>(day_of_year(date)) - 1;
    return days * k_seconds_per_day;
}

std::int64_t day_number(CalendarTime time) {
    return floor_div(time, k_seconds_per_day);
}

std::int64_t seconds_into_day(CalendarTime time) {
    return time - day_number(time) * k_seconds_per_day;
}

Date to_date(CalendarTime time) {
    const std::int64_t days = day_number(time);
    const std::int64_t year = floor_div(days, k_days_per_year);
    if (year < std::numeric_limits<std::int32_t>::min() || year > std::numeric_limits<std::int32_t>::max()) {
        throw std::out_of_range("calendar year does not fit in 32 bits");
    }
    const auto day_in_year = static_cast<std::uint16_t>(days - year * k_days_per_year); // 0..364

    std::uint8_t month = 1;
    while (day_in_year >= k_cumulative_days[month]) {
        ++month;
    }
    Date date;
    date.year = static_cast<std::int32_t>(year);
    date.month = month;
    date.day = static_cast<std::uint8_t>(day_in_year - k_cumulative_days[month - 1] + 1);
    return date;
}

Date add_days(const Date& date, std::int64_t days) {
    return to_date(to_calendar_time(date) + days * k_seconds_per_day);
}

FormattedDate format_date(const Date& date) {
    if (!is_valid(date)) {
        throw std::invalid_argument("invalid date");
    }
    const bool bce = date.year <= 0;
    const std::int64_t displayed_year = bce ? 1 - static_cast<std::int64_t>(date.year) : date.year;

    FormattedDate formatted;
    formatted.key = bce ? "calendar.date.bce" : "calendar.date.ce";
    formatted.args.push_back({"day", std::to_string(date.day), false});
    formatted.args.push_back({"month", "calendar.month." + std::to_string(date.month), true});
    formatted.args.push_back({"year", std::to_string(displayed_year), false});
    return formatted;
}

} // namespace citysim::time
