#include "citysim/content/content_database.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/time/calendar_module.hpp"
#include "citysim/time/calendar_state.hpp"
#include "citysim/time/calendar_system.hpp"
#include "citysim/time/time_commands.hpp"
#include "citysim/time/time_module.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <stdexcept>

using namespace citysim;
using namespace citysim::time;

namespace {

std::unique_ptr<content::ContentDatabase> load_calendar_content() {
    auto db = std::make_unique<content::ContentDatabase>();
    const auto result = db->load({std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "content" / "calendar"});
    REQUIRE(result.ok());
    return db;
}

// A simulation with the time module and calendar installed, starting in `era`.
struct Game {
    std::unique_ptr<content::ContentDatabase> content = load_calendar_content();
    core::Simulation sim;

    explicit Game(std::string_view era, std::optional<Date> start = std::nullopt) : sim(1, 1) {
        time::install(sim);
        install_calendar(sim, *content, era, start);
    }

    const CalendarState& calendar() const { return sim.world().ctx().get<CalendarState>(); }
    void step(std::uint64_t ticks) {
        for (std::uint64_t i = 0; i < ticks; ++i) {
            sim.step();
        }
    }
    void set_era(std::string_view id, std::uint64_t sequence) {
        sim.enqueue({core::SequencedCommand{sequence, 0, SetEra{std::string(id)}}});
    }
};

constexpr std::int64_t k_year = k_seconds_per_year;

} // namespace

TEST_CASE("Ancient cadence: exactly 2400 ticks make one year", "[time][calendar][era]") {
    Game game("test:era.ancient");
    REQUIRE(current_date(game.calendar()) == Date{-3000, 1, 1}); // the era's default start

    game.step(2399);
    REQUIRE(current_date(game.calendar()) == Date{-3000, 12, 31}); // one tick short of the new year
    REQUIRE(game.calendar().now == to_calendar_time(Date{-2999, 1, 1}) - 13140);

    game.step(1);
    REQUIRE(current_date(game.calendar()) == Date{-2999, 1, 1});
    REQUIRE(game.calendar().now == to_calendar_time(Date{-2999, 1, 1}));
    REQUIRE(game.calendar().remainder == 0);
}

TEST_CASE("Modern cadence: exactly 14,400 ticks make one year", "[time][calendar][era]") {
    Game game("test:era.modern");
    REQUIRE(current_date(game.calendar()) == Date{1900, 1, 1});
    game.step(14399);
    REQUIRE(current_date(game.calendar()) == Date{1900, 12, 31});
    game.step(1);
    REQUIRE(game.calendar().now == to_calendar_time(Date{1901, 1, 1}));
}

TEST_CASE("every era takes exactly real_seconds_per_year * base_tick_rate ticks per year",
          "[time][calendar][era]") {
    const auto content = load_calendar_content();
    const TimeDefines defines;
    for (const content::EraDef& era : content->registry<content::EraDef>().all()) {
        core::Simulation sim(1, 1);
        time::install(sim);
        install_calendar(sim, *content, era.id.str());
        const CalendarTime start = sim.world().ctx().get<CalendarState>().now;

        const std::uint64_t ticks_per_year =
            static_cast<std::uint64_t>(era.real_seconds_per_year) * defines.base_tick_rate;
        for (std::uint64_t i = 0; i < ticks_per_year; ++i) {
            sim.step();
        }
        const auto& state = sim.world().ctx().get<CalendarState>();
        INFO("era " << era.id.str());
        REQUIRE(state.now - start == k_year); // exactly one year, down to the second
        REQUIRE(state.remainder == 0);
    }
}

TEST_CASE("an era whose seconds per tick are fractional carries the remainder exactly",
          "[time][calendar][era]") {
    // 7 real seconds per year at 20 tps is 140 ticks per year: 225,257.142857... calendar seconds per tick.
    Game game("test:era.odd");
    const CalendarTime start = game.calendar().now;
    constexpr std::int64_t denominator = 140;

    game.step(1);
    REQUIRE(game.calendar().now - start == 225257);
    REQUIRE(game.calendar().remainder == 31536000 % denominator * 1); // 31,536,000 mod 140 = 20

    game.step(139);
    REQUIRE(game.calendar().now - start == k_year); // 140 ticks: exactly a year, remainder back to zero
    REQUIRE(game.calendar().remainder == 0);

    for (std::int64_t ticks = 1; ticks <= 3000; ticks += 7) {
        Game fresh("test:era.odd");
        fresh.step(static_cast<std::uint64_t>(ticks));
        REQUIRE(fresh.calendar().now - start == ticks * k_year / denominator); // floor(T * N / D)
    }
}

TEST_CASE("a 10-million-tick run shows no drift against closed-form math", "[time][calendar][era]") {
    // The tight loop calls the same advance function the system uses, so ten million ticks stay fast.
    CalendarState state;
    constexpr std::uint64_t denominator = 140; // the odd era
    constexpr std::int64_t total_ticks = 10'000'000;

    for (std::int64_t tick = 1; tick <= total_ticks; ++tick) {
        advance_calendar(state, denominator);
        if (tick % 999'983 == 0 || tick == total_ticks) {
            REQUIRE(state.now == tick * k_year / static_cast<std::int64_t>(denominator));
            REQUIRE(state.remainder < denominator);
        }
    }
    // A whole number of years has passed exactly: 10,000,000 / 140 is not whole, so check a whole count too.
    CalendarState years;
    for (std::int64_t tick = 0; tick < 140 * 70000; ++tick) {
        advance_calendar(years, denominator);
    }
    REQUIRE(years.now == 70000 * k_year);
    REQUIRE(years.remainder == 0);
}

TEST_CASE("the calendar advances through the simulation exactly like the closed form",
          "[time][calendar][era]") {
    Game game("test:era.odd");
    const CalendarTime start = game.calendar().now;
    game.step(100000);
    REQUIRE(game.calendar().now - start == 100000LL * k_year / 140);
}

TEST_CASE("switching eras mid-year keeps total time exact when cadences divide evenly",
          "[time][calendar][era]") {
    Game game("test:era.ancient");
    const CalendarTime start = game.calendar().now;
    game.step(1000);
    REQUIRE(game.calendar().now - start == 1000 * 13140);

    game.set_era("test:era.modern", 1); // consumed by the next tick, whose Calendar phase already uses Modern
    game.step(5000);
    REQUIRE(game.calendar().now - start == 1000 * 13140 + 5000 * 2190);
    REQUIRE(game.calendar().remainder == 0);
    REQUIRE(game.sim.world().ctx().get<CalendarState>().era ==
            *game.content->registry<content::EraDef>().find("test:era.modern"));
}

TEST_CASE("switching eras with fractional cadences stays within a second of ideal", "[time][calendar][era]") {
    // 1,234 ticks of Ancient (D = 2400), then 777 ticks of odd (D = 140). The ideal elapsed time is
    // 1234*N/2400 + 777*N/140 seconds; the clock may lag it by less than two seconds (one for the floor, up
    // to one for the fraction rounded down at the switch).
    Game game("test:era.ancient");
    const CalendarTime start = game.calendar().now;
    game.step(1234);
    game.set_era("test:era.odd", 1);
    game.step(777);

    const std::int64_t actual = game.calendar().now - start;
    constexpr std::int64_t d1 = 2400;
    constexpr std::int64_t d2 = 140;
    const std::int64_t numerator = 1234 * k_year * d2 + 777 * k_year * d1; // ideal = numerator / (d1 * d2)
    REQUIRE(actual * (d1 * d2) <= numerator);
    REQUIRE(numerator < (actual + 2) * (d1 * d2));
    REQUIRE(game.calendar().remainder < 140);
}

TEST_CASE("rescale_remainder moves the fraction between denominators", "[time][calendar][era]") {
    REQUIRE(rescale_remainder(0, 2400, 140) == 0);
    REQUIRE(rescale_remainder(1200, 2400, 14400) == 7200); // half a second stays half a second
    REQUIRE(rescale_remainder(100, 140, 2400) == 100 * 2400 / 140);
    for (std::uint64_t remainder : {1ULL, 17ULL, 99ULL, 139ULL}) {
        REQUIRE(rescale_remainder(remainder, 140, 2400) < 2400); // always a valid remainder for the new era
    }
    // Absurdly large denominators drop the fraction instead of overflowing.
    REQUIRE(rescale_remainder(1ULL << 40, 1ULL << 41, 1ULL << 40) == 0);
    REQUIRE(rescale_remainder(5, 0, 100) == 0);
}

TEST_CASE("SetEra with an unknown or malformed id is ignored", "[time][calendar][era]") {
    Game game("test:era.ancient");
    game.step(10);
    const CalendarState before = game.calendar();

    game.set_era("test:era.missing", 1);
    game.step(1);
    game.set_era("not an id", 2);
    game.step(1);
    REQUIRE(game.calendar().era == before.era);
    REQUIRE(game.calendar().now == before.now + 2 * 13140); // still ticking at the Ancient pace
}

TEST_CASE("install_calendar validates its inputs", "[time][calendar][era]") {
    const auto content = load_calendar_content();

    core::Simulation without_time(1, 1);
    REQUIRE_THROWS_AS(install_calendar(without_time, *content, "test:era.ancient"), std::logic_error);

    core::Simulation sim(1, 1);
    time::install(sim);
    REQUIRE_THROWS_AS(install_calendar(sim, *content, "test:era.nope"), std::invalid_argument);
    REQUIRE_THROWS_AS(install_calendar(sim, *content, "test:era.ancient", Date{0, 2, 30}),
                      std::invalid_argument);
    REQUIRE(sim.scheduler().system_count() == 1); // only the clock system: a failed install adds nothing

    // An explicit start date overrides the era's default.
    install_calendar(sim, *content, "test:era.ancient", Date{-2500, 7, 4});
    REQUIRE(current_date(sim.world().ctx().get<CalendarState>()) == Date{-2500, 7, 4});
}

TEST_CASE("the calendar is part of the state hash", "[time][calendar][hash]") {
    Game a("test:era.modern");
    Game b("test:era.modern");
    a.step(50);
    b.step(50);
    REQUIRE(a.sim.world().state_hash() == b.sim.world().state_hash());

    b.step(1);
    REQUIRE(a.sim.world().state_hash() != b.sim.world().state_hash());

    Game other_era("test:era.ancient", Date{1900, 1, 1}); // same date, different era
    Game same_date("test:era.modern");
    REQUIRE(other_era.sim.world().state_hash() != same_date.sim.world().state_hash());
}

TEST_CASE("the calendar does not depend on the game speed", "[time][calendar][era]") {
    // Speed changes how fast ticks arrive in real time, never how much calendar one tick covers.
    Game slow("test:era.ancient");
    Game fast("test:era.ancient");
    fast.sim.enqueue({core::SequencedCommand{1, 0, SetSpeed{3}}});
    slow.step(500);
    fast.step(500);
    REQUIRE(slow.calendar().now == fast.calendar().now);
}

TEST_CASE("a zero denominator is rejected", "[time][calendar][era]") {
    CalendarState state;
    REQUIRE_THROWS_AS(advance_calendar(state, 0), std::invalid_argument);
}
