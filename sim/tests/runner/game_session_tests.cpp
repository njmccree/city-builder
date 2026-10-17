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
#include <utility>

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

namespace {

// Runs a game session tick by tick on a fake clock (20 ticks per second at 1x) and reads state hashes back.
struct AgentGame {
    FakeClock clock;
    runner::NewGameOutcome outcome;
    double base = 0.0;
    int steps = 0;

    AgentGame() {
        NewGameRequest request = base_request(clock);
        request.test_map_size = 64;
        outcome = runner::start_new_game(request);
        REQUIRE(outcome.ok());
        REQUIRE(outcome.session->runner().wait_until_idle());
    }
    runner::SimulationRunner& runner() { return outcome.session->runner(); }

    // Advances `ticks` ticks, a tick at a time so none is dropped as backlog.
    void run(int ticks) {
        for (int i = 0; i < ticks; ++i) {
            ++steps;
            clock.set_time(base + static_cast<double>(steps) * (1.0 / 20.0));
            REQUIRE(runner().wait_until_idle());
        }
    }
    // The pace starts over after a load: the next tick is one interval after it.
    void rebase() {
        base = clock.now_seconds();
        steps = 0;
    }
    std::pair<std::uint64_t, std::uint64_t> hash() { // {hash, tick}
        runner().submit(core::RequestStateHash{});
        REQUIRE(runner().wait_until_idle());
        const runner::RenderSnapshot snapshot = outcome.session->latest_snapshot();
        for (const runner::UiEvent& event : snapshot.ui_events) {
            if (event.kind == runner::UiEventKind::StateHash) {
                return {event.hash, event.tick};
            }
        }
        FAIL("no StateHash event");
        return {0, 0};
    }
};

} // namespace

TEST_CASE("a game with agents ends in the same state with or without a save and load in the middle",
          "[runner][session][save]") {
    const fs::path dir = fs::temp_directory_path() / "citysim_agents_save";
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    const std::string file = (dir / "mid_walk.csav").generic_string();

    // A: spawn, walk 60 ticks, save, walk 60 more.
    AgentGame a;
    a.runner().submit(debug::SpawnDebugAgents{2000});
    a.run(60);
    const auto [a_mid_hash, a_mid_tick] = a.hash();
    a.runner().submit(core::SaveGame{file});
    REQUIRE(a.runner().wait_until_idle());
    a.run(60);
    const auto [a_end_hash, a_end_tick] = a.hash();
    CHECK(a_mid_tick == 60);
    CHECK(a_end_tick == 120);
    CHECK(a_mid_hash != a_end_hash);

    // B: the same game walked straight through, no save.
    AgentGame b;
    b.runner().submit(debug::SpawnDebugAgents{2000});
    b.run(120);
    CHECK(b.hash() == std::pair{a_end_hash, a_end_tick});

    // C: a fresh game that loads the save and walks the remaining 60 ticks.
    AgentGame c;
    c.runner().submit(core::LoadGame{file});
    REQUIRE(c.runner().wait_until_idle());
    CHECK(c.hash() == std::pair{a_mid_hash, a_mid_tick});
    c.rebase();
    c.run(60);
    CHECK(c.hash() == std::pair{a_end_hash, a_end_tick});

    fs::remove_all(dir, ignored);
}
