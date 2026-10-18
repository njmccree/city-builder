#include "citysim/time/calendar_events.hpp"
#include "citysim/time/calendar_scheduler.hpp"
#include "support/calendar_game.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <variant>

using namespace citysim;
using namespace citysim::time;
using citysim::test::CalendarGame;

namespace {

// A compact text form of an event, so whole streams can be compared at a glance.
std::string show(const core::Event& event) {
    return std::visit(
        [](const auto& e) -> std::string {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, DayStarted>) {
                return "day " + std::to_string(e.date.year) + "-" + std::to_string(e.date.month) + "-" +
                       std::to_string(e.date.day);
            } else if constexpr (std::is_same_v<E, MonthStarted>) {
                return "month " + std::to_string(e.date.year) + "-" + std::to_string(e.date.month);
            } else if constexpr (std::is_same_v<E, YearStarted>) {
                return "year " + std::to_string(e.year);
            } else if constexpr (std::is_same_v<E, ScheduledActionDue>) {
                return "due " + std::get<DebugAction>(e.action).tag + "@" + std::to_string(e.when);
            } else {
                return "other";
            }
        },
        event);
}

std::vector<std::string> shown(const std::vector<core::Event>& events) {
    std::vector<std::string> out;
    for (const core::Event& event : events) {
        out.push_back(show(event));
    }
    return out;
}

std::size_t count(const std::vector<core::Event>& events, const std::string& prefix) {
    std::size_t n = 0;
    for (const std::string& s : shown(events)) {
        if (s.rfind(prefix, 0) == 0) {
            ++n;
        }
    }
    return n;
}

CalendarScheduler& scheduler_of(CalendarGame& game) {
    return game.sim.world().ctx().get<CalendarScheduler>();
}

} // namespace

// ---- boundary events --------------------------------------------------------------------------------------

TEST_CASE("an era that advances several days per tick emits one event per boundary, in order",
          "[time][calendar][events]") {
    // Fast era: 20 ticks per year, 1,576,800 seconds (18.25 days) per tick.
    CalendarGame game("test:era.fast"); // starts 1 Jan 2000
    game.step();
    // 18.25 days after midnight of 1 Jan: the 1st through 18th day boundaries are 2 Jan .. 19 Jan.
    std::vector<std::string> expected;
    for (int day = 2; day <= 19; ++day) {
        expected.push_back("day 2000-1-" + std::to_string(day));
    }
    REQUIRE(shown(game.seen) == expected);

    game.seen.clear();
    game.step();
    // 36.5 days in: boundaries 20 Jan .. 31 Jan, then Feb 1 (a month start) .. Feb 6.
    REQUIRE(count(game.seen, "day ") == 18); // days 19..36 after the start
    REQUIRE(count(game.seen, "month ") == 1);
    REQUIRE(shown(game.seen)[12] == "month 2000-2"); // the month event comes just before Feb 1's day event
    REQUIRE(shown(game.seen)[13] == "day 2000-2-1");
}

TEST_CASE("a full year emits exactly 365 days, 12 months and 1 year, in chronological order",
          "[time][calendar][events]") {
    CalendarGame game("test:era.fast");
    game.step(20); // exactly one year
    REQUIRE(game.calendar().now == to_calendar_time(Date{2001, 1, 1}));

    REQUIRE(count(game.seen, "day ") == 365);
    REQUIRE(count(game.seen, "month ") == 12); // Feb 1 .. Dec 1, and Jan 1 of the new year
    REQUIRE(count(game.seen, "year ") == 1);

    // Build the expected stream independently, day by day from the start date.
    std::vector<std::string> expected;
    for (int d = 1; d <= 365; ++d) {
        const Date date = add_days(Date{2000, 1, 1}, d);
        if (date.month == 1 && date.day == 1) {
            expected.push_back("year " + std::to_string(date.year));
        }
        if (date.day == 1) {
            expected.push_back("month " + std::to_string(date.year) + "-" + std::to_string(date.month));
        }
        expected.push_back("day " + std::to_string(date.year) + "-" + std::to_string(date.month) + "-" +
                           std::to_string(date.day));
    }
    REQUIRE(shown(game.seen) == expected);

    // At the new year, the year event comes before the month event, which comes before the day event.
    const auto stream = shown(game.seen);
    REQUIRE(stream[stream.size() - 3] == "year 2001");
    REQUIRE(stream[stream.size() - 2] == "month 2001-1");
    REQUIRE(stream[stream.size() - 1] == "day 2001-1-1");
}

TEST_CASE("at the Ancient cadence a day takes about 6.6 ticks, and no tick crosses two boundaries",
          "[time][calendar][events]") {
    CalendarGame game("test:era.ancient"); // 13,140 seconds per tick
    std::size_t max_per_tick = 0;
    for (int tick = 0; tick < 2400; ++tick) {
        game.seen.clear();
        game.step();
        max_per_tick = std::max(max_per_tick, count(game.seen, "day "));
    }
    REQUIRE(max_per_tick == 1);
    REQUIRE(game.calendar().now == to_calendar_time(Date{-2999, 1, 1}));
}

TEST_CASE("the game's first day does not emit events, only boundaries crossed later",
          "[time][calendar][events]") {
    CalendarGame game("test:era.ancient");
    game.step(5); // 65,700 seconds: still the first day (86,400 seconds)
    REQUIRE(game.seen.empty());
    game.step(2); // 92,... crosses the first midnight
    REQUIRE(shown(game.seen) == std::vector<std::string>{"day -3000-1-2"});
}

TEST_CASE("crossing into and out of year 0 emits the right year events", "[time][calendar][events]") {
    CalendarGame game("test:era.fast", Date{-1, 12, 25}); // 18.25 days a tick
    game.step();
    const auto stream = shown(game.seen);
    // 25 Dec to 12 Jan of year 0: Dec 26..31 of year -1, then the new year 0.
    REQUIRE(count(game.seen, "year ") == 1);
    REQUIRE(count(game.seen, "year 0") == 1);
    const auto year_at = std::find(stream.begin(), stream.end(), "year 0") - stream.begin();
    REQUIRE(stream[static_cast<std::size_t>(year_at) + 1] == "month 0-1");
    REQUIRE(stream[static_cast<std::size_t>(year_at) + 2] == "day 0-1-1");
    REQUIRE(stream[static_cast<std::size_t>(year_at) - 1] == "day -1-12-31");
}

TEST_CASE("the same game emits the same events and hashes the same, twice",
          "[time][calendar][events][determinism]") {
    CalendarGame a("test:era.fast");
    CalendarGame b("test:era.fast");
    for (int i = 0; i < 100; ++i) {
        a.step();
        b.step();
        REQUIRE(a.sim.world().state_hash() == b.sim.world().state_hash());
    }
    REQUIRE(shown(a.seen) == shown(b.seen));
    REQUIRE(a.seen.size() > 1000);
}

// ---- the scheduler
// ------------------------------------------------------------------------------------------

TEST_CASE("CalendarScheduler keeps entries sorted by time then insertion order",
          "[time][calendar][scheduler]") {
    CalendarScheduler scheduler;
    scheduler.schedule(300, DebugAction{"c"});
    scheduler.schedule(100, DebugAction{"a1"});
    scheduler.schedule(200, DebugAction{"b"});
    scheduler.schedule(100, DebugAction{"a2"}); // same time as a1, inserted later
    scheduler.schedule(100, DebugAction{"a3"});

    std::vector<std::string> order;
    for (const auto& entry : scheduler.entries()) {
        order.push_back(std::get<DebugAction>(entry.action).tag);
    }
    REQUIRE(order == std::vector<std::string>{"a1", "a2", "a3", "b", "c"});
    REQUIRE(scheduler.next_sequence() == 5);
}

TEST_CASE("pop_due removes exactly the due entries, in order", "[time][calendar][scheduler]") {
    CalendarScheduler scheduler;
    scheduler.schedule(10, DebugAction{"x"});
    scheduler.schedule(20, DebugAction{"y"});
    scheduler.schedule(30, DebugAction{"z"});

    REQUIRE(scheduler.pop_due(5).empty());
    const auto due = scheduler.pop_due(20);
    REQUIRE(due.size() == 2);
    REQUIRE(std::get<DebugAction>(due[0].action).tag == "x");
    REQUIRE(std::get<DebugAction>(due[1].action).tag == "y");
    REQUIRE(scheduler.size() == 1);
    REQUIRE(scheduler.pop_due(20).empty()); // not returned twice
    REQUIRE(scheduler.pop_due(1000).size() == 1);
    REQUIRE(scheduler.size() == 0);
}

TEST_CASE("scheduled actions fire exactly once, in order, including ones for the same time",
          "[time][calendar][scheduler]") {
    CalendarGame game("test:era.fast"); // 1 Jan 2000, 18.25 days a tick
    const CalendarTime start = game.calendar().now;
    constexpr CalendarTime tick = 1576800;

    auto& scheduler = scheduler_of(game);
    scheduler.schedule(start + 3 * tick, DebugAction{"third"});
    scheduler.schedule(start + tick / 2, DebugAction{"first-a"});
    scheduler.schedule(start + tick / 2, DebugAction{"first-b"}); // same time, inserted after first-a
    scheduler.schedule(start + tick + 1, DebugAction{"second"});

    game.step(); // now = start + tick
    REQUIRE(count(game.seen, "due ") == 2);
    game.step(); // now = start + 2*tick
    game.step(); // now = start + 3*tick: "third" is due exactly now
    game.step(5);

    std::vector<std::string> due;
    for (const std::string& s : shown(game.seen)) {
        if (s.rfind("due ", 0) == 0) {
            due.push_back(s.substr(4, s.find('@') - 4));
        }
    }
    REQUIRE(due == std::vector<std::string>{"first-a", "first-b", "second", "third"});
    REQUIRE(scheduler.size() == 0); // fired once and gone
}

TEST_CASE("an action scheduled in the past fires on the next calendar tick", "[time][calendar][scheduler]") {
    CalendarGame game("test:era.ancient");
    game.step(10);
    const CalendarTime long_gone = game.calendar().now - 1000000;
    scheduler_of(game).schedule(long_gone, DebugAction{"late"});
    game.seen.clear();
    game.step();
    REQUIRE(shown(game.seen) == std::vector<std::string>{"due late@" + std::to_string(long_gone)});

    // And a time in the future (year 0 is far ahead of a game that starts in 3000 BCE) does not fire yet.
    scheduler_of(game).schedule(0, DebugAction{"future"});
    game.seen.clear();
    game.step(5);
    REQUIRE(count(game.seen, "due ") == 0);
}

TEST_CASE("boundary events come before scheduled actions within a tick", "[time][calendar][scheduler]") {
    CalendarGame game("test:era.fast");
    scheduler_of(game).schedule(game.calendar().now + 1, DebugAction{"soon"});
    game.step();
    const auto stream = shown(game.seen);
    REQUIRE(stream.size() == 19); // 18 day events, then the action
    REQUIRE(stream.back().rfind("due soon", 0) == 0);
    for (std::size_t i = 0; i + 1 < stream.size(); ++i) {
        REQUIRE(stream[i].rfind("day ", 0) == 0);
    }
}

TEST_CASE("scheduled actions are part of the state hash", "[time][calendar][scheduler][hash]") {
    CalendarGame a("test:era.ancient");
    CalendarGame b("test:era.ancient");
    REQUIRE(a.sim.world().state_hash() == b.sim.world().state_hash());

    scheduler_of(a).schedule(500, DebugAction{"x"});
    REQUIRE(a.sim.world().state_hash() != b.sim.world().state_hash());

    scheduler_of(b).schedule(500, DebugAction{"y"}); // same time, different payload
    REQUIRE(a.sim.world().state_hash() != b.sim.world().state_hash());

    CalendarGame c("test:era.ancient");
    scheduler_of(c).schedule(500, DebugAction{"x"});
    REQUIRE(a.sim.world().state_hash() == c.sim.world().state_hash());
}
