#include "citysim/save/binary_reader.hpp"
#include "citysim/save/binary_writer.hpp"
#include "citysim/save/world_io.hpp"
#include "citysim/time/calendar_scheduler.hpp"
#include "citysim/time/game_clock.hpp"
#include "support/calendar_game.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <stdexcept>
#include <vector>

using namespace citysim;
using citysim::test::CalendarGame;

namespace {

std::vector<std::byte> save_game(const CalendarGame& game) {
    save::BinaryWriter writer(nullptr, game.content.get());
    save::save_world(game.sim.world(), writer);
    const auto bytes = writer.bytes();
    return {bytes.begin(), bytes.end()};
}

void load_game(CalendarGame& game, const std::vector<std::byte>& bytes) {
    save::BinaryReader reader(bytes, nullptr, game.content.get());
    save::load_world(game.sim.world(), reader);
    REQUIRE(reader.at_end());
}

} // namespace

TEST_CASE("the clock, calendar and scheduled actions survive a save", "[save][time]") {
    CalendarGame original("test:era.ancient");
    original.step(75);
    auto& scheduler = original.sim.world().ctx().get<time::CalendarScheduler>();
    const time::CalendarTime now = original.calendar().now;
    scheduler.schedule(now + 5000, time::DebugAction{"later"});
    scheduler.schedule(now + 100, time::DebugAction{"sooner"});
    scheduler.schedule(now + 5000, time::DebugAction{"later too"});
    original.sim.world().ctx().get<time::GameClock>().speed = 3;
    original.sim.world().ctx().get<time::GameClock>().paused = true;

    const auto bytes = save_game(original);

    CalendarGame loaded("test:era.ancient");
    load_game(loaded, bytes);
    CHECK(loaded.sim.world().state_hash() == original.sim.world().state_hash());
    CHECK(loaded.calendar().now == original.calendar().now);
    CHECK(loaded.calendar().remainder == original.calendar().remainder);
    CHECK(loaded.sim.world().ctx().get<time::CalendarScheduler>().size() == 3);
    CHECK(loaded.sim.world().ctx().get<time::GameClock>().speed == 3);
    CHECK(save_game(loaded) == bytes);
}

TEST_CASE("a loaded game continues exactly like the original", "[save][time]") {
    // Run 300 ticks in one go, versus 120 ticks, save, load into a fresh game, and 180 more.
    CalendarGame straight("test:era.ancient");
    CalendarGame first_part("test:era.ancient");
    for (CalendarGame* game : {&straight, &first_part}) {
        auto& scheduler = game->sim.world().ctx().get<time::CalendarScheduler>();
        scheduler.schedule(game->calendar().now + 40, time::DebugAction{"early"});
        scheduler.schedule(game->calendar().now + 100000, time::DebugAction{"far"});
    }
    straight.step(300);
    first_part.step(120);

    CalendarGame resumed("test:era.ancient");
    load_game(resumed, save_game(first_part));
    resumed.step(180);

    CHECK(resumed.sim.world().state_hash() == straight.sim.world().state_hash());
    CHECK(resumed.calendar().now == straight.calendar().now);
    CHECK(resumed.calendar().remainder == straight.calendar().remainder);
}

TEST_CASE("the era is saved as an id, so a different era loads correctly", "[save][time]") {
    CalendarGame original("test:era.modern");
    original.step(10);
    const auto bytes = save_game(original);

    CalendarGame loaded("test:era.ancient"); // starts in another era
    load_game(loaded, bytes);
    const auto& registry = loaded.content->registry<content::EraDef>();
    CHECK(registry.id(loaded.calendar().era).str() == "test:era.modern");
    CHECK(loaded.sim.world().state_hash() == original.sim.world().state_hash());
}

TEST_CASE("saving time state without the content database is a logic error", "[save][time]") {
    CalendarGame game("test:era.ancient");
    save::BinaryWriter writer; // no content
    CHECK_THROWS_AS(save::save_world(game.sim.world(), writer), std::logic_error);
}

TEST_CASE("a save naming an era the content lacks is a format error", "[save][time]") {
    CalendarGame original("test:era.modern");
    const auto bytes = save_game(original);

    // Content with a different set of eras: only "ancient" exists in a one-mod fixture.
    CalendarGame other("test:era.ancient");
    // Content with a different set of eras (alpha and beta only).
    content::ContentDatabase small;
    const auto result = small.load({std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "content" / "happy"});
    REQUIRE(result.ok());
    save::BinaryReader reader(bytes, nullptr, &small);
    CHECK_THROWS_AS(save::load_world(other.sim.world(), reader), save::SaveFormatError);
}
