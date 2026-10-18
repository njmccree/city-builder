#include "citysim/core/rng.hpp"
#include "citysim/time/calendar.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <stdexcept>

using namespace citysim::time;

namespace {
Date date(std::int32_t year, int month, int day) {
    return Date{year, static_cast<std::uint8_t>(month), static_cast<std::uint8_t>(day)};
}
} // namespace

TEST_CASE("the epoch is 1 January of year 0", "[time][calendar]") {
    REQUIRE(to_calendar_time(date(0, 1, 1)) == 0);
    REQUIRE(to_date(0) == date(0, 1, 1));
    REQUIRE(to_calendar_time(date(0, 1, 2)) == k_seconds_per_day);
    REQUIRE(to_calendar_time(date(1, 1, 1)) == k_seconds_per_year);
    REQUIRE(to_calendar_time(date(-1, 1, 1)) == -k_seconds_per_year);
}

TEST_CASE("month lengths and day of year", "[time][calendar]") {
    REQUIRE(days_in_month(1) == 31);
    REQUIRE(days_in_month(2) == 28); // no leap years
    REQUIRE(days_in_month(12) == 31);
    REQUIRE_THROWS_AS(days_in_month(0), std::out_of_range);
    REQUIRE_THROWS_AS(days_in_month(13), std::out_of_range);

    REQUIRE(day_of_year(date(5, 1, 1)) == 1);
    REQUIRE(day_of_year(date(5, 2, 1)) == 32);
    REQUIRE(day_of_year(date(5, 3, 1)) == 60);
    REQUIRE(day_of_year(date(5, 12, 31)) == 365);

    int total = 0;
    for (int month = 1; month <= 12; ++month) {
        total += days_in_month(static_cast<std::uint8_t>(month));
    }
    REQUIRE(total == 365);
}

TEST_CASE("invalid dates are rejected", "[time][calendar]") {
    REQUIRE_FALSE(is_valid(date(0, 0, 1)));
    REQUIRE_FALSE(is_valid(date(0, 13, 1)));
    REQUIRE_FALSE(is_valid(date(0, 1, 0)));
    REQUIRE_FALSE(is_valid(date(0, 2, 29))); // never a leap year
    REQUIRE_FALSE(is_valid(date(0, 4, 31)));
    REQUIRE(is_valid(date(0, 2, 28)));
    REQUIRE(is_valid(date(-2999, 12, 31)));
    REQUIRE_THROWS_AS(to_calendar_time(date(0, 2, 29)), std::invalid_argument);
    REQUIRE_THROWS_AS(day_of_year(date(0, 13, 1)), std::invalid_argument);
    REQUIRE_THROWS_AS(format_date(date(0, 1, 40)), std::invalid_argument);
}

TEST_CASE("the year boundary around year 0: -1 to 0 to 1", "[time][calendar]") {
    // Dec 31 of year -1 is the last day before the epoch.
    REQUIRE(to_calendar_time(date(-1, 12, 31)) == -k_seconds_per_day);
    REQUIRE(to_date(-1) == date(-1, 12, 31)); // the last second before the epoch
    REQUIRE(to_date(-k_seconds_per_day) == date(-1, 12, 31));
    REQUIRE(to_date(-k_seconds_per_day - 1) == date(-1, 12, 30));
    REQUIRE(to_date(0) == date(0, 1, 1));

    REQUIRE(add_days(date(-1, 12, 31), 1) == date(0, 1, 1));
    REQUIRE(add_days(date(0, 12, 31), 1) == date(1, 1, 1));
    REQUIRE(add_days(date(0, 1, 1), -1) == date(-1, 12, 31));
    REQUIRE(add_days(date(1, 1, 1), -1) == date(0, 12, 31));
}

TEST_CASE("month and year boundaries", "[time][calendar]") {
    REQUIRE(add_days(date(7, 1, 31), 1) == date(7, 2, 1));
    REQUIRE(add_days(date(7, 2, 28), 1) == date(7, 3, 1)); // February has 28 days
    REQUIRE(add_days(date(7, 12, 31), 1) == date(8, 1, 1));
    REQUIRE(add_days(date(7, 3, 1), -1) == date(7, 2, 28));
    REQUIRE(add_days(date(7, 1, 1), 365) == date(8, 1, 1));
    REQUIRE(add_days(date(7, 6, 15), 0) == date(7, 6, 15));
    REQUIRE(add_days(date(-3000, 1, 1), 365 * 3000) == date(0, 1, 1));
}

TEST_CASE("time of day is separate from the date", "[time][calendar]") {
    const CalendarTime noon = to_calendar_time(date(10, 5, 5)) + 12 * 3600;
    REQUIRE(to_date(noon) == date(10, 5, 5));
    REQUIRE(seconds_into_day(noon) == 12 * 3600);
    REQUIRE(seconds_into_day(0) == 0);
    REQUIRE(seconds_into_day(-1) == k_seconds_per_day - 1); // before the epoch counts back from midnight
    REQUIRE(seconds_into_day(-k_seconds_per_day) == 0);
    REQUIRE(day_number(-1) == -1);
    REQUIRE(day_number(-k_seconds_per_day - 1) == -2);
    REQUIRE(day_number(k_seconds_per_day) == 1);
}

TEST_CASE("dates compare chronologically", "[time][calendar]") {
    REQUIRE(date(-5, 12, 31) < date(-4, 1, 1));
    REQUIRE(date(0, 1, 31) < date(0, 2, 1));
    REQUIRE(date(0, 3, 4) < date(0, 3, 5));
    REQUIRE(date(2, 2, 2) == date(2, 2, 2));
    REQUIRE(date(-3000, 6, 6) < date(1600, 1, 1));
}

TEST_CASE("to_date and to_calendar_time round-trip over many random dates", "[time][calendar]") {
    const std::uint64_t stream = citysim::rng::stream("test.calendar.roundtrip");
    for (std::uint64_t i = 0; i < 20000; ++i) {
        const std::uint64_t h = citysim::rng::hash64(1234, stream, 0, i, 0);
        Date d;
        d.year = static_cast<std::int32_t>(citysim::rng::uniform_int(h, -5000, 5000)); // both sides of year 0
        d.month = static_cast<std::uint8_t>(citysim::rng::uniform_int(h >> 5, 1, 12));
        d.day = static_cast<std::uint8_t>(citysim::rng::uniform_int(h >> 11, 1, days_in_month(d.month)));
        REQUIRE(is_valid(d));

        const CalendarTime t = to_calendar_time(d);
        REQUIRE(to_date(t) == d);
        // Any moment inside the day maps to the same date.
        const std::int64_t offset = citysim::rng::uniform_int(h >> 17, 0, k_seconds_per_day - 1);
        REQUIRE(to_date(t + offset) == d);
        REQUIRE(seconds_into_day(t + offset) == offset);
    }
}

TEST_CASE("consecutive days walk the calendar with no gaps or repeats", "[time][calendar]") {
    // Three years either side of year 0, one day at a time.
    Date expected = date(-3, 1, 1);
    CalendarTime t = to_calendar_time(expected);
    for (int day = 0; day < 6 * 365; ++day) {
        REQUIRE(to_date(t) == expected);
        const Date next = add_days(expected, 1);
        REQUIRE(next > expected);
        expected = next;
        t += k_seconds_per_day;
    }
    REQUIRE(expected == date(3, 1, 1));
}

TEST_CASE("years far outside the usual range still work, and overflow is an error", "[time][calendar]") {
    REQUIRE(to_date(to_calendar_time(date(1000000, 6, 15))) == date(1000000, 6, 15));
    REQUIRE(to_date(to_calendar_time(date(-1000000, 6, 15))) == date(-1000000, 6, 15));
    REQUIRE_THROWS_AS(to_date(std::numeric_limits<CalendarTime>::max()), std::out_of_range);
    REQUIRE_THROWS_AS(to_date(std::numeric_limits<CalendarTime>::min()), std::out_of_range);
}

TEST_CASE("dates format as keys and arguments, with BCE numbering", "[time][calendar]") {
    const auto arg = [](const FormattedDate& f, const std::string& name) {
        const auto found = std::find_if(f.args.begin(), f.args.end(),
                                        [&](const FormattedDateArg& a) { return a.name == name; });
        INFO("looking for argument " << name);
        REQUIRE(found != f.args.end());
        return *found;
    };

    // Year 0 is 1 BCE; year -2999 is 3000 BCE.
    const FormattedDate bce_one = format_date(date(0, 3, 9));
    REQUIRE(bce_one.key == "calendar.date.bce");
    REQUIRE(arg(bce_one, "year").value == "1");
    REQUIRE(arg(bce_one, "day").value == "9");
    REQUIRE(arg(bce_one, "month").value == "calendar.month.3");
    REQUIRE(arg(bce_one, "month").is_key);
    REQUIRE_FALSE(arg(bce_one, "day").is_key);
    REQUIRE_FALSE(arg(bce_one, "year").is_key);

    const FormattedDate ancient = format_date(date(-2999, 12, 31));
    REQUIRE(ancient.key == "calendar.date.bce");
    REQUIRE(arg(ancient, "year").value == "3000");
    REQUIRE(arg(ancient, "month").value == "calendar.month.12");

    // Year 1 is 1 CE, and the display jumps from 1 BCE straight to 1 CE (there is no year zero on screen).
    const FormattedDate ce_one = format_date(date(1, 1, 1));
    REQUIRE(ce_one.key == "calendar.date.ce");
    REQUIRE(arg(ce_one, "year").value == "1");
    REQUIRE(format_date(date(1900, 6, 1)).key == "calendar.date.ce");
    REQUIRE(arg(format_date(date(1900, 6, 1)), "year").value == "1900");
    REQUIRE(arg(format_date(date(std::numeric_limits<std::int32_t>::min(), 1, 1)), "year").value ==
            "2147483649");
}
