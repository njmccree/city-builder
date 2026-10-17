#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <string>
#include <vector>

namespace citysim::time {

// Calendar arithmetic (architecture §6): 365-day years with no leap years and 12 fixed months, astronomical
// year numbering (year 0 is 1 BCE, year -2999 is 3000 BCE), all in exact integer math. Pure functions with no
// state, so they belong to no clock.

// Seconds since the epoch, 1 January of year 0, 00:00.
using CalendarTime = std::int64_t;

inline constexpr std::int64_t k_seconds_per_day = 86400;
inline constexpr std::int64_t k_days_per_year = 365;
inline constexpr std::int64_t k_seconds_per_year = k_seconds_per_day * k_days_per_year;

inline constexpr std::array<std::uint8_t, 12> k_days_in_month{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

struct Date {
    std::int32_t year = 0;
    std::uint8_t month = 1; // 1..12
    std::uint8_t day = 1;   // 1..days_in_month(month)

    // Orders by year, then month, then day.
    auto operator<=>(const Date&) const = default;
};

bool is_valid(const Date& date);
std::uint8_t days_in_month(std::uint8_t month); // month 1..12, else std::out_of_range

// 1..365.
std::uint16_t day_of_year(const Date& date);

// The first moment of `date`. Throws std::invalid_argument for an invalid date.
CalendarTime to_calendar_time(const Date& date);

// The date `time` falls on, ignoring the time of day. Correct for negative times (floor division).
// Throws std::out_of_range if the year does not fit in 32 bits.
Date to_date(CalendarTime time);

// Seconds since midnight of the day `time` falls on, 0..86399, also for negative times.
std::int64_t seconds_into_day(CalendarTime time);

// Whole days since the epoch of the day `time` falls on (negative before year 0).
std::int64_t day_number(CalendarTime time);

Date add_days(const Date& date, std::int64_t days);

// One argument of a formatted date. When `is_key` is true the value is itself a localization key (a month
// name) that the renderer must look up, not text.
struct FormattedDateArg {
    std::string name;
    std::string value;
    bool is_key = false;
};

// A date as data, not text (the Localizer arrives with S0.4.3): the template key and its arguments.
//   "calendar.date.bce" or "calendar.date.ce", with args day, month (a key "calendar.month.<n>") and year.
// The year is the displayed one: year 0 shows as 1 BCE and -2999 as 3000 BCE; year 1 is 1 CE.
struct FormattedDate {
    std::string key;
    std::vector<FormattedDateArg> args;
};

FormattedDate format_date(const Date& date);

} // namespace citysim::time
