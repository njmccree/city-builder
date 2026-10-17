#pragma once

#include "citysim/content/content_database.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/time/calendar_module.hpp"
#include "citysim/time/calendar_state.hpp"
#include "citysim/time/time_commands.hpp"
#include "citysim/time/time_module.hpp"
#include "support/fn_system.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace citysim::test {

// Loads the "calendar" fixture eras: ancient (120 s/year), modern (720), odd (7: fractional seconds per tick)
// and fast (1: about 18 days per tick).
inline std::unique_ptr<content::ContentDatabase> load_calendar_content() {
    auto db = std::make_unique<content::ContentDatabase>();
    const auto result = db->load({std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "content" / "calendar"});
    REQUIRE(result.ok());
    return db;
}

// A simulation with the time module and the calendar installed, starting in `era`.
struct CalendarGame {
    std::unique_ptr<content::ContentDatabase> content = load_calendar_content();
    core::Simulation sim;
    std::vector<core::Event> seen; // every event, captured in the Publish phase, in the order it was emitted

    explicit CalendarGame(std::string_view era, std::optional<time::Date> start = std::nullopt) : sim(1, 1) {
        time::install(sim);
        time::install_calendar(sim, *content, era, start);
        sim.add_system(core::Phase::Publish, make_system("test.capture", [this](core::TickContext& context) {
                           for (const core::Event& event : context.events.readable()) {
                               seen.push_back(event);
                           }
                       }));
    }

    const time::CalendarState& calendar() const { return sim.world().ctx().get<time::CalendarState>(); }
    void step(std::uint64_t ticks = 1) {
        for (std::uint64_t i = 0; i < ticks; ++i) {
            sim.step();
        }
    }
    void set_era(std::string_view id, std::uint64_t sequence) {
        sim.enqueue({core::SequencedCommand{sequence, 0, time::SetEra{std::string(id)}}});
    }
};

} // namespace citysim::test
