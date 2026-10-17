#include "citysim/core/commands.hpp"
#include "citysim/core/state_hasher.hpp"
#include "citysim/runner/simulation_runner.hpp"
#include "citysim/save/archive.hpp"
#include "citysim/save/bind_serializer.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/time/time_commands.hpp"
#include "support/fn_system.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace citysim;
using citysim::runner::FakeClock;
using citysim::runner::RenderSnapshot;
using citysim::runner::RunnerConfig;
using citysim::runner::SimulationRunner;
using citysim::runner::UiEvent;
using citysim::runner::UiEventKind;
using citysim::test::make_system;

namespace fs = std::filesystem;

namespace {

// Game state that changes every tick and that commands can set, so a load is visible in snapshots.
struct Tally {
    std::int64_t value = 0;
};
template <class Ar> void serialize(Ar& ar, Tally& tally, std::uint32_t version) {
    save::io(ar, tally.value, version);
}
void hash_tally(core::StateHasher& hasher, const Tally& tally) {
    hasher.add_pod(tally.value);
}

void setup_game(core::Simulation& sim) {
    core::Hooks<Tally> hooks;
    hooks.hash = &hash_tally;
    save::bind_serializer(hooks);
    sim.world().registry().register_singleton<Tally>("test.Tally", hooks);
    sim.world().ctx().emplace<Tally>();
    sim.dispatcher().register_handler<core::DebugSetComponentValue>(
        [](core::TickContext& context, const core::DebugSetComponentValue& cmd) {
            context.world.ctx().get<Tally>().value = cmd.value;
        });
    sim.add_system(core::Phase::Update, make_system("test.tally", [](core::TickContext& context) {
                       ++context.world.ctx().get<Tally>().value;
                   }));
}

struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& name) : path(fs::temp_directory_path() / name) {
        std::error_code ignored;
        fs::remove_all(path, ignored);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

void advance_to(FakeClock& clock, SimulationRunner& runner, double seconds) {
    clock.set_time(seconds);
    REQUIRE(runner.wait_until_idle());
}

// Fake time in whole ticks at 1x, computed the way the runner schedules them (origin + n * interval) so a
// tick is never missed by an ulp. `rebase` after anything that restarts the runner's pacing, such as a load.
struct Timeline {
    double base = 0.0;
    int steps = 0;
    double now() const { return base + static_cast<double>(steps) * (1.0 / 20.0); }
    void rebase(double at) {
        base = at;
        steps = 0;
    }
};

// Advances a tick at a time so nothing is dropped as backlog.
void run_ticks(FakeClock& clock, SimulationRunner& runner, Timeline& time, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        ++time.steps;
        advance_to(clock, runner, time.now());
    }
}

const UiEvent* find_event(const RenderSnapshot& snapshot, UiEventKind kind) {
    const auto it = std::find_if(snapshot.ui_events.begin(), snapshot.ui_events.end(),
                                 [kind](const UiEvent& event) { return event.kind == kind; });
    return it == snapshot.ui_events.end() ? nullptr : &*it;
}

std::int64_t tally_of(SimulationRunner& runner) {
    return runner.simulation().world().ctx().get<Tally>().value; // only while the runner is idle in a test
}

} // namespace

TEST_CASE("runner: SaveGame and LoadGame round-trip through commands", "[runner][save]") {
    const TempDir dir("citysim_runner_save");
    const std::string file = (dir.path / "game.csav").generic_string();

    FakeClock clock;
    SimulationRunner runner(5, clock, {}, 1);
    runner.configure(setup_game);
    runner.start();
    REQUIRE(runner.wait_until_idle());

    Timeline now;
    run_ticks(clock, runner, now, 20);
    REQUIRE(runner.latest_snapshot().tick == 20);

    runner.submit(core::SaveGame{file});
    REQUIRE(runner.wait_until_idle());
    const RenderSnapshot after_save = runner.latest_snapshot();
    const UiEvent* saved = find_event(after_save, UiEventKind::SaveCompleted);
    REQUIRE(saved != nullptr);
    CHECK(saved->ok);
    CHECK_FALSE(saved->autosave);
    CHECK(saved->path == file);
    CHECK(fs::exists(file));
    CHECK(save::read_save_header(file).tick == 20);

    run_ticks(clock, runner, now, 10);
    REQUIRE(runner.latest_snapshot().tick == 30);

    runner.submit(core::LoadGame{file});
    REQUIRE(runner.wait_until_idle());
    const RenderSnapshot after_load = runner.latest_snapshot();
    const UiEvent* loaded = find_event(after_load, UiEventKind::LoadCompleted);
    REQUIRE(loaded != nullptr);
    CHECK(loaded->ok);
    CHECK(after_load.tick == 20); // the game went back
    runner.stop();
    CHECK(tally_of(runner) == 20);
}

TEST_CASE("runner: a loaded game keeps ticking from where the save was", "[runner][save]") {
    const TempDir dir("citysim_runner_resume");
    const std::string file = (dir.path / "game.csav").generic_string();

    FakeClock clock;
    SimulationRunner runner(5, clock, {}, 1);
    runner.configure(setup_game);
    runner.start();
    REQUIRE(runner.wait_until_idle());
    Timeline now;
    run_ticks(clock, runner, now, 12);
    runner.submit(core::SaveGame{file});
    REQUIRE(runner.wait_until_idle());
    run_ticks(clock, runner, now, 30);

    runner.submit(core::LoadGame{file});
    REQUIRE(runner.wait_until_idle());
    REQUIRE(runner.latest_snapshot().tick == 12);

    // The pace starts over at the load: the next tick is one interval after it.
    now.rebase(clock.now_seconds());
    run_ticks(clock, runner, now, 8);
    CHECK(runner.latest_snapshot().tick == 20);
    runner.stop();
    CHECK(tally_of(runner) == static_cast<std::int64_t>(runner.simulation().tick()));
}

TEST_CASE("runner: a load while paused publishes a snapshot", "[runner][save]") {
    const TempDir dir("citysim_runner_paused");
    const std::string file = (dir.path / "game.csav").generic_string();

    FakeClock clock;
    SimulationRunner runner(5, clock, {}, 1);
    runner.configure(setup_game);
    runner.start();
    REQUIRE(runner.wait_until_idle());
    Timeline now;
    run_ticks(clock, runner, now, 7);
    runner.submit(core::SaveGame{file});
    REQUIRE(runner.wait_until_idle());
    run_ticks(clock, runner, now, 5);
    REQUIRE(runner.latest_snapshot().tick == 12);

    runner.submit(time::TogglePause{}); // takes effect in the tick that consumes it
    run_ticks(clock, runner, now, 1);
    REQUIRE(runner.latest_snapshot().paused);
    REQUIRE(runner.latest_snapshot().tick == 13);

    runner.submit(core::LoadGame{file});
    REQUIRE(runner.wait_until_idle());
    const RenderSnapshot snapshot = runner.latest_snapshot();
    CHECK(snapshot.tick == 7);
    CHECK_FALSE(snapshot.paused); // pause is part of the saved state, and that game was running
    runner.stop();
}

TEST_CASE("runner: saving while paused works and does not advance the tick", "[runner][save]") {
    const TempDir dir("citysim_runner_save_paused");
    const std::string file = (dir.path / "game.csav").generic_string();

    FakeClock clock;
    SimulationRunner runner(5, clock, {}, 1);
    runner.configure(setup_game);
    runner.start();
    REQUIRE(runner.wait_until_idle());
    Timeline now;
    run_ticks(clock, runner, now, 4);
    runner.submit(time::TogglePause{}); // takes effect in the tick that consumes it
    run_ticks(clock, runner, now, 1);
    REQUIRE(runner.latest_snapshot().paused);

    runner.submit(core::SaveGame{file});
    REQUIRE(runner.wait_until_idle());
    const RenderSnapshot snapshot = runner.latest_snapshot();
    const UiEvent* saved = find_event(snapshot, UiEventKind::SaveCompleted);
    REQUIRE(saved != nullptr);
    CHECK(saved->ok);
    CHECK(snapshot.tick == 5);
    CHECK(save::read_save_header(file).tick == 5);
    runner.stop();
}

TEST_CASE("runner: a failed load or save is reported and changes nothing", "[runner][save]") {
    const TempDir dir("citysim_runner_failures");

    FakeClock clock;
    SimulationRunner runner(5, clock, {}, 1);
    runner.configure(setup_game);
    runner.start();
    REQUIRE(runner.wait_until_idle());
    Timeline now;
    run_ticks(clock, runner, now, 6);

    runner.submit(core::LoadGame{(dir.path / "missing.csav").generic_string()});
    REQUIRE(runner.wait_until_idle());
    RenderSnapshot snapshot = runner.latest_snapshot();
    const UiEvent* failed_load = find_event(snapshot, UiEventKind::LoadCompleted);
    REQUIRE(failed_load != nullptr);
    CHECK_FALSE(failed_load->ok);
    CHECK_FALSE(failed_load->text.empty());
    CHECK(snapshot.tick == 6);

    { std::ofstream(dir.path / "garbage.csav") << "this is not a save"; }
    runner.submit(core::LoadGame{(dir.path / "garbage.csav").generic_string()});
    REQUIRE(runner.wait_until_idle());
    snapshot = runner.latest_snapshot();
    failed_load = find_event(snapshot, UiEventKind::LoadCompleted);
    REQUIRE(failed_load != nullptr);
    CHECK_FALSE(failed_load->ok);

    runner.submit(core::SaveGame{(dir.path / "no_such_dir" / "deeper" / "game.csav").generic_string()});
    REQUIRE(runner.wait_until_idle());
    snapshot = runner.latest_snapshot();
    const UiEvent* failed_save = find_event(snapshot, UiEventKind::SaveCompleted);
    REQUIRE(failed_save != nullptr);
    CHECK_FALSE(failed_save->ok);

    // The runner is still fine and still ticking the original game.
    run_ticks(clock, runner, now, 4);
    CHECK(runner.latest_snapshot().tick == 10);
    CHECK(runner.failure().empty());
    runner.stop();
}

TEST_CASE("runner: result events reach the consumer once, even if ticks follow", "[runner][save]") {
    const TempDir dir("citysim_runner_events");
    const std::string file = (dir.path / "game.csav").generic_string();

    FakeClock clock;
    SimulationRunner runner(5, clock, {}, 1);
    runner.configure(setup_game);
    runner.start();
    REQUIRE(runner.wait_until_idle());
    Timeline now;
    run_ticks(clock, runner, now, 2);

    runner.submit(core::SaveGame{file});
    REQUIRE(runner.wait_until_idle());
    run_ticks(clock, runner, now, 3); // several snapshots go by before the consumer looks

    const RenderSnapshot first = runner.latest_snapshot();
    CHECK(find_event(first, UiEventKind::SaveCompleted) != nullptr);
    const RenderSnapshot second = runner.latest_snapshot();
    CHECK(find_event(second, UiEventKind::SaveCompleted) == nullptr); // already delivered
    run_ticks(clock, runner, now, 2);
    CHECK(find_event(runner.latest_snapshot(), UiEventKind::SaveCompleted) == nullptr);
    runner.stop();
}

TEST_CASE("runner: pending commands are saved and applied after a load", "[runner][save]") {
    const TempDir dir("citysim_runner_pending");
    const std::string file = (dir.path / "game.csav").generic_string();

    FakeClock clock;
    SimulationRunner runner(5, clock, {}, 1);
    runner.configure(setup_game);
    runner.start();
    REQUIRE(runner.wait_until_idle());
    Timeline now;
    run_ticks(clock, runner, now, 3);

    // Submitted before the save, so it is saved as waiting; submitted after, so it is not.
    runner.submit(core::DebugSetComponentValue{0, 1000});
    runner.submit(core::SaveGame{file});
    runner.submit(core::DebugSetComponentValue{0, -5});
    REQUIRE(runner.wait_until_idle());
    CHECK(save::read_save_header(file).tick == 3);

    runner.submit(core::LoadGame{file});
    REQUIRE(runner.wait_until_idle());
    run_ticks(clock, runner, now, 1);
    now.rebase(clock.now_seconds());
    run_ticks(clock, runner, now, 1);
    runner.stop();
    // The 1000 was applied at the first tick after the load; the Update system then added 1 per tick.
    CHECK(tally_of(runner) >= 1000);
}

TEST_CASE("runner: autosave rotates through its slots on the runner clock", "[runner][save][autosave]") {
    const TempDir dir("citysim_runner_autosave");
    RunnerConfig config;
    config.time.autosave_interval_real_seconds = 10;
    config.time.autosave_slots = 2;
    config.autosave_directory = dir.path;

    FakeClock clock;
    SimulationRunner runner(5, clock, config, 1);
    runner.configure(setup_game);
    runner.start();
    REQUIRE(runner.wait_until_idle());

    const auto slot = [&](int n) { return dir.path / ("autosave_" + std::to_string(n) + ".csav"); };

    advance_to(clock, runner, 9.9);
    CHECK_FALSE(fs::exists(slot(1)));

    advance_to(clock, runner, 10.0);
    CHECK(fs::exists(slot(1)));
    CHECK_FALSE(fs::exists(slot(2)));
    const std::uint64_t first_tick = save::read_save_header(slot(1)).tick;

    advance_to(clock, runner, 20.0);
    CHECK(fs::exists(slot(2)));
    CHECK(save::read_save_header(slot(2)).tick > first_tick);

    advance_to(clock, runner, 30.0); // wraps around to slot 1
    CHECK(save::read_save_header(slot(1)).tick > first_tick);
    CHECK_FALSE(fs::exists(slot(3)));

    // An autosave is a normal save that can be loaded.
    runner.submit(core::LoadGame{slot(2).generic_string()});
    REQUIRE(runner.wait_until_idle());
    CHECK(find_event(runner.latest_snapshot(), UiEventKind::LoadCompleted)->ok);
    runner.stop();
}

TEST_CASE("runner: autosave reports its result, does not run while paused, and can be off",
          "[runner][save][autosave]") {
    SECTION("the event says it was an autosave") {
        const TempDir dir("citysim_runner_autosave_event");
        RunnerConfig config;
        config.time.autosave_interval_real_seconds = 5;
        config.autosave_directory = dir.path;
        FakeClock clock;
        SimulationRunner runner(5, clock, config, 1);
        runner.configure(setup_game);
        runner.start();
        REQUIRE(runner.wait_until_idle());
        advance_to(clock, runner, 5.0);
        const RenderSnapshot snapshot = runner.latest_snapshot();
        const UiEvent* event = find_event(snapshot, UiEventKind::SaveCompleted);
        REQUIRE(event != nullptr);
        CHECK(event->autosave);
        CHECK(event->ok);
        runner.stop();
    }
    SECTION("a paused game is not autosaved, and the timer restarts when it resumes") {
        const TempDir dir("citysim_runner_autosave_paused");
        RunnerConfig config;
        config.time.autosave_interval_real_seconds = 5;
        config.autosave_directory = dir.path;
        FakeClock clock;
        SimulationRunner runner(5, clock, config, 1);
        runner.configure(setup_game);
        runner.start();
        REQUIRE(runner.wait_until_idle());
        runner.submit(time::TogglePause{}); // takes effect in the tick that consumes it
        advance_to(clock, runner, 1.0 / 20.0);
        REQUIRE(runner.latest_snapshot().paused);
        clock.set_time(100.0);
        REQUIRE(runner.wait_until_idle());
        CHECK(fs::is_empty(dir.path));

        runner.submit(time::TogglePause{}); // resume at t = 100
        REQUIRE(runner.wait_until_idle());
        advance_to(clock, runner, 104.0);
        CHECK(fs::is_empty(dir.path)); // the interval counts from the resume
        advance_to(clock, runner, 105.0);
        CHECK_FALSE(fs::is_empty(dir.path));
        runner.stop();
    }
    SECTION("an interval of 0 turns it off") {
        const TempDir dir("citysim_runner_autosave_off");
        RunnerConfig config;
        config.time.autosave_interval_real_seconds = 0;
        config.autosave_directory = dir.path;
        FakeClock clock;
        SimulationRunner runner(5, clock, config, 1);
        runner.configure(setup_game);
        runner.start();
        REQUIRE(runner.wait_until_idle());
        advance_to(clock, runner, 5000.0);
        CHECK(fs::is_empty(dir.path));
        runner.stop();
    }
}

TEST_CASE("runner: configure must come before start", "[runner][save]") {
    FakeClock clock;
    SimulationRunner runner(5, clock, {}, 1);
    runner.start();
    CHECK_THROWS_AS(runner.configure(setup_game), std::logic_error);
    runner.stop();
}

TEST_CASE("runner: autosave settings are validated", "[runner][save]") {
    FakeClock clock;
    RunnerConfig config;
    config.time.autosave_slots = 0;
    CHECK_THROWS_AS(SimulationRunner(5, clock, config, 1), std::invalid_argument);
}
