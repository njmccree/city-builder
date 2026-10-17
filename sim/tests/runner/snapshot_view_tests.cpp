#include "citysim/runner/game_session.hpp"
#include "citysim/runner/snapshot_view.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>

using namespace citysim;
using Catch::Matchers::WithinAbs;
using citysim::runner::FakeClock;
using citysim::runner::RenderSnapshot;
using citysim::runner::SnapshotChannel;
using citysim::runner::SnapshotView;

namespace {

RenderSnapshot snapshot_at(std::uint64_t tick, double published_at, std::uint8_t speed = 1,
                           bool paused = false) {
    RenderSnapshot snapshot;
    snapshot.tick = tick;
    snapshot.published_at = published_at;
    snapshot.speed = speed;
    snapshot.paused = paused;
    return snapshot;
}

} // namespace

TEST_CASE("a new tick moves the current snapshot to previous", "[runner][snapshot]") {
    SnapshotView view;
    CHECK_FALSE(view.has_snapshot());

    view.update(snapshot_at(1, 0.05));
    CHECK(view.has_snapshot());
    CHECK(view.current().tick == 1);
    CHECK(view.previous().tick == 1); // nothing earlier to interpolate from

    view.update(snapshot_at(2, 0.10));
    CHECK(view.current().tick == 2);
    CHECK(view.previous().tick == 1);

    view.update(snapshot_at(5, 0.25)); // frames can skip ticks
    CHECK(view.current().tick == 5);
    CHECK(view.previous().tick == 2);
}

TEST_CASE("the same tick again keeps when the tick was first published", "[runner][snapshot]") {
    SnapshotView view;
    view.update(snapshot_at(1, 0.05));
    view.update(snapshot_at(2, 0.10));

    RenderSnapshot extra = snapshot_at(2, 0.17); // e.g. republished to report a save
    extra.stats.entity_count = 9;
    view.update(extra);
    CHECK(view.current().stats.entity_count == 9); // the data is refreshed
    CHECK(view.current().published_at == 0.10);    // the time is not
    CHECK(view.previous().tick == 1);
}

TEST_CASE("a lower tick, or a reset, forgets the history", "[runner][snapshot]") {
    SnapshotView view;
    view.update(snapshot_at(10, 1.0));
    view.update(snapshot_at(11, 1.05));
    view.update(snapshot_at(4, 2.0)); // a load went back
    CHECK(view.current().tick == 4);
    CHECK(view.previous().tick == 4);

    view.update(snapshot_at(5, 2.05));
    view.reset();
    view.update(snapshot_at(40, 3.0)); // a load went forward
    CHECK(view.previous().tick == 40);
}

TEST_CASE("alpha is the fraction of a tick interval since the tick was published", "[runner][snapshot]") {
    const time::TimeDefines defines; // 20 ticks per second at 1x: 0.05 s per tick
    SnapshotView view;
    CHECK(view.alpha(100.0, defines) == 1.0F); // no snapshot yet

    view.update(snapshot_at(3, 10.0));
    CHECK_THAT(view.alpha(10.0, defines), WithinAbs(0.0, 1e-6));
    CHECK_THAT(view.alpha(10.025, defines), WithinAbs(0.5, 1e-4));
    CHECK_THAT(view.alpha(10.05, defines), WithinAbs(1.0, 1e-4));
    CHECK(view.alpha(10.5, defines) == 1.0F); // late: clamped
    CHECK(view.alpha(9.0, defines) == 0.0F);  // clock before the stamp: clamped
}

TEST_CASE("alpha follows the speed, and is 1 while paused", "[runner][snapshot]") {
    const time::TimeDefines defines; // multipliers 0, 1, 2, 4
    SnapshotView view;

    view.update(snapshot_at(1, 10.0, 3)); // 4x: 0.0125 s per tick
    CHECK_THAT(view.alpha(10.00625, defines), WithinAbs(0.5, 1e-3));

    view.update(snapshot_at(2, 10.0125, 2, false)); // 2x: 0.025 s
    CHECK_THAT(view.alpha(10.0125 + 0.0125, defines), WithinAbs(0.5, 1e-3));

    view.update(snapshot_at(3, 10.05, 2, true)); // paused
    CHECK(view.alpha(10.0501, defines) == 1.0F);
}

TEST_CASE("publish hooks add channels, which are rebuilt for every snapshot",
          "[runner][snapshot][channels]") {
    FakeClock clock;
    runner::NewGameRequest request;
    request.seed = 3;
    const std::filesystem::path content = CITYSIM_GAME_CONTENT_DIR;
    request.content_roots = {content};
    request.enabled_mods = {"base"};
    request.era_id = "base:era.ancient";
    request.clock = &clock;
    request.worker_count = 1;
    // Two hooks, run in order: the second sees the first's channel.
    request.publish_hooks.push_back([](core::Simulation& simulation, RenderSnapshot& snapshot) {
        SnapshotChannel channel;
        channel.name = "test.pairs";
        channel.stride = 2;
        channel.data = {static_cast<float>(simulation.tick()), 1.0F, static_cast<float>(simulation.tick()),
                        2.0F};
        snapshot.channels.push_back(std::move(channel));
    });
    request.publish_hooks.push_back([](core::Simulation&, RenderSnapshot& snapshot) {
        SnapshotChannel channel;
        channel.name = "test.count";
        channel.data = {static_cast<float>(snapshot.channels.size())};
        snapshot.channels.push_back(std::move(channel));
    });

    runner::NewGameOutcome outcome = runner::start_new_game(request);
    REQUIRE(outcome.ok());
    REQUIRE(outcome.session->runner().wait_until_idle());

    for (int tick = 1; tick <= 3; ++tick) {
        clock.set_time(tick * (1.0 / 20.0));
        REQUIRE(outcome.session->runner().wait_until_idle());
    }
    const RenderSnapshot snapshot = outcome.session->latest_snapshot();
    REQUIRE(snapshot.tick == 3);
    CHECK(snapshot.channels.size() == 2); // not 2 x 4: cleared each time

    const SnapshotChannel* pairs = snapshot.find_channel("test.pairs");
    REQUIRE(pairs != nullptr);
    CHECK(pairs->stride == 2);
    CHECK(pairs->record_count() == 2);
    // The hook runs inside the tick's publish, so the simulation's tick is 3 by then.
    CHECK(pairs->data == std::vector<float>{3.0F, 1.0F, 3.0F, 2.0F});

    const SnapshotChannel* count = snapshot.find_channel("test.count");
    REQUIRE(count != nullptr);
    CHECK(count->data == std::vector<float>{1.0F});
    CHECK(snapshot.find_channel("test.nothing") == nullptr);
}
