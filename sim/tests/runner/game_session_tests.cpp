#include "citysim/content/era_def.hpp"
#include "citysim/core/commands.hpp"
#include "citysim/debug/agent_snapshot.hpp"
#include "citysim/debug/debug_commands.hpp"
#include "citysim/runner/game_session.hpp"
#include "citysim/time/time_commands.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

using namespace citysim;
using citysim::runner::FakeClock;
using citysim::runner::NewGameRequest;

namespace fs = std::filesystem;

namespace {

NewGameRequest base_request(FakeClock& clock) {
    NewGameRequest request;
    request.seed = 11;
    const fs::path content = CITYSIM_GAME_CONTENT_DIR;
    request.content_roots = {content, content / "mods"};
    request.enabled_mods = {"base"};
    request.era_id = "base:era.ancient";
    request.clock = &clock;
    request.worker_count = 1;
    return request;
}

bool has_error(const runner::NewGameOutcome& outcome, const std::string& needle) {
    return std::any_of(outcome.errors.begin(), outcome.errors.end(), [&](const content::ContentIssue& issue) {
        return issue.message.find(needle) != std::string::npos;
    });
}

} // namespace

TEST_CASE("a new game loads the base content and starts ticking", "[runner][session]") {
    FakeClock clock;
    runner::NewGameOutcome outcome = runner::start_new_game(base_request(clock));
    REQUIRE(outcome.ok());
    CHECK(outcome.errors.empty());
    auto& session = *outcome.session;
    REQUIRE(session.runner().wait_until_idle());

    // The first snapshot already has the calendar: 1 January of the era's default start year.
    runner::RenderSnapshot snapshot = session.latest_snapshot();
    CHECK(snapshot.tick == 0);
    REQUIRE(snapshot.has_date);
    CHECK(snapshot.date == time::Date{-3000, 1, 1});
    CHECK(snapshot.hour == 0);
    CHECK(snapshot.minute == 0);

    clock.set_time(1.0 / 20.0);
    REQUIRE(session.runner().wait_until_idle());
    CHECK(session.latest_snapshot().tick == 1);
}

TEST_CASE("only the enabled mods are loaded", "[runner][session]") {
    FakeClock clock;
    {
        runner::NewGameOutcome outcome = runner::start_new_game(base_request(clock));
        REQUIRE(outcome.ok());
        CHECK(outcome.session->content().mods().size() == 1);
    }
    {
        NewGameRequest request = base_request(clock);
        request.enabled_mods = {"base", "example_mod"};
        runner::NewGameOutcome outcome = runner::start_new_game(request);
        REQUIRE(outcome.ok());
        const auto& eras = outcome.session->content().registry<content::EraDef>();
        CHECK(eras.get(*eras.find("base:era.ancient")).desc_key == "example_mod.era.ancient.desc");
    }
}

TEST_CASE("problems starting a game come back as errors, not exceptions", "[runner][session]") {
    FakeClock clock;

    SECTION("unknown era") {
        NewGameRequest request = base_request(clock);
        request.era_id = "base:era.nonexistent";
        const runner::NewGameOutcome outcome = runner::start_new_game(request);
        CHECK_FALSE(outcome.ok());
        CHECK(has_error(outcome, "unknown era"));
    }
    SECTION("no base game") {
        NewGameRequest request = base_request(clock);
        request.content_roots = {fs::path(CITYSIM_TEST_FIXTURES_DIR) / "content" / "calendar"};
        request.enabled_mods = {};
        const runner::NewGameOutcome outcome = runner::start_new_game(request);
        CHECK_FALSE(outcome.ok());
        CHECK(has_error(outcome, "base game"));
    }
    SECTION("a mod that is not there") {
        NewGameRequest request = base_request(clock);
        request.enabled_mods = {"base", "no_such_mod"};
        const runner::NewGameOutcome outcome = runner::start_new_game(request);
        CHECK_FALSE(outcome.ok());
        CHECK(has_error(outcome, "no_such_mod"));
    }
    SECTION("a folder that does not exist") {
        NewGameRequest request = base_request(clock);
        request.content_roots = {fs::path(CITYSIM_GAME_CONTENT_DIR) / "nowhere"};
        const runner::NewGameOutcome outcome = runner::start_new_game(request);
        CHECK_FALSE(outcome.ok());
        CHECK_FALSE(outcome.errors.empty());
    }
}

TEST_CASE("a saved game loads back through the session", "[runner][session]") {
    const fs::path dir = fs::temp_directory_path() / "citysim_session_save";
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    const std::string file = (dir / "game.csav").generic_string();

    FakeClock clock;
    runner::NewGameOutcome outcome = runner::start_new_game(base_request(clock));
    REQUIRE(outcome.ok());
    auto& runner = outcome.session->runner();
    REQUIRE(runner.wait_until_idle());

    for (int tick = 1; tick <= 5; ++tick) {
        clock.set_time(tick * (1.0 / 20.0));
        REQUIRE(runner.wait_until_idle());
    }
    runner.submit(core::SaveGame{file});
    REQUIRE(runner.wait_until_idle());
    const runner::RenderSnapshot saved = outcome.session->latest_snapshot();

    for (int tick = 6; tick <= 12; ++tick) {
        clock.set_time(tick * (1.0 / 20.0));
        REQUIRE(runner.wait_until_idle());
    }
    CHECK(outcome.session->latest_snapshot().tick == 12);

    runner.submit(core::LoadGame{file});
    REQUIRE(runner.wait_until_idle());
    const runner::RenderSnapshot loaded = outcome.session->latest_snapshot();
    CHECK(loaded.tick == 5);
    REQUIRE(loaded.has_date);
    CHECK(loaded.date == saved.date);
    CHECK(loaded.minute == saved.minute);
    CHECK(runner.failure().empty());
    fs::remove_all(dir, ignored);
}

TEST_CASE("a game with a test map has agents, their snapshot channel and the map grid", "[runner][session]") {
    FakeClock clock;
    NewGameRequest request = base_request(clock);
    request.test_map_size = 48;
    runner::NewGameOutcome outcome = runner::start_new_game(request);
    REQUIRE(outcome.ok());
    auto& session = *outcome.session;
    REQUIRE(session.runner().wait_until_idle());

    session.runner().submit(debug::SpawnDebugAgents{25});
    clock.set_time(1.0 / 20.0);
    REQUIRE(session.runner().wait_until_idle());
    runner::RenderSnapshot snapshot = session.latest_snapshot();
    const runner::SnapshotChannel* agents = snapshot.find_channel(debug::k_agent_channel);
    REQUIRE(agents != nullptr);
    CHECK(agents->record_count() == 25);
    const runner::SnapshotGrid* grid = snapshot.find_grid(debug::k_test_map_grid);
    REQUIRE(grid != nullptr);
    CHECK(grid->width == 48);
    CHECK(grid->height == 48);
}

TEST_CASE("a game without a test map publishes no agents or grid", "[runner][session]") {
    FakeClock clock;
    runner::NewGameOutcome outcome = runner::start_new_game(base_request(clock));
    REQUIRE(outcome.ok());
    REQUIRE(outcome.session->runner().wait_until_idle());
    const runner::RenderSnapshot snapshot = outcome.session->latest_snapshot();
    CHECK(snapshot.find_channel(debug::k_agent_channel) == nullptr);
    CHECK(snapshot.find_grid(debug::k_test_map_grid) == nullptr);
}
