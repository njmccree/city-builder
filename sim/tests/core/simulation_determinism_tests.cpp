#include "citysim/core/simulation.hpp"
#include "citysim/core/state_hasher.hpp"
#include "support/fn_system.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using namespace citysim::core;
using citysim::test::make_system;

namespace {
struct Position {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

void hash_position(StateHasher& hasher, const Position& p) {
    hasher.add_pod(p.x);
    hasher.add_pod(p.y);
}

// A small but representative scenario: spawn entities, then every tick move each one by a random step
// derived from the counter-based RNG (keyed by entity, so the order of visiting does not matter), and
// occasionally remove one.
std::unique_ptr<Simulation> make_scenario(std::uint64_t seed) {
    auto sim = std::make_unique<Simulation>(seed);
    Hooks<Position> hooks;
    hooks.hash = &hash_position;
    sim->world().registry().register_type<Position>("test.Position", hooks);

    sim->add_system(Phase::ApplyCommands, make_system("spawn", [](TickContext& context) {
                        if (context.tick == 0) {
                            for (int i = 0; i < 20; ++i) {
                                context.world.emplace<Position>(context.world.create(), i, -i);
                            }
                        }
                    }));
    sim->add_system(Phase::Update, make_system("wander", [](TickContext& context) {
                        const auto stream = citysim::rng::stream("test.wander");
                        for (const auto entity : context.world.view<Position>()) {
                            const auto h = context.rng.at(stream, entt::to_integral(entity));
                            auto& p = context.world.get<Position>(entity);
                            p.x += static_cast<std::int32_t>(citysim::rng::uniform_int(h, -2, 2));
                            p.y += static_cast<std::int32_t>(citysim::rng::uniform_int(h >> 7, -2, 2));
                        }
                    }));
    sim->add_system(
        Phase::PostUpdate, make_system("cull", [](TickContext& context) {
            const auto stream = citysim::rng::stream("test.cull");
            std::vector<World::Entity> doomed;
            for (const auto entity : context.world.view<Position>()) {
                if (citysim::rng::chance(context.rng.at(stream, entt::to_integral(entity)), 0.01)) {
                    doomed.push_back(entity);
                }
            }
            for (const auto entity : doomed) {
                context.world.destroy(entity);
            }
        }));
    return sim;
}
} // namespace

TEST_CASE("same seed and systems give the same hash after 100 steps", "[core][sim][determinism]") {
    auto a = make_scenario(2024);
    auto b = make_scenario(2024);
    for (int i = 0; i < 100; ++i) {
        a->step();
        b->step();
        REQUIRE(a->world().state_hash() == b->world().state_hash()); // every tick, not just the end
    }
    REQUIRE(a->world().entity_count() < 20); // the cull system really ran
    REQUIRE(a->world().entity_count() > 0);
}

TEST_CASE("a different seed gives a different state", "[core][sim][determinism]") {
    auto a = make_scenario(1);
    auto b = make_scenario(2);
    for (int i = 0; i < 100; ++i) {
        a->step();
        b->step();
    }
    REQUIRE(a->world().state_hash() != b->world().state_hash());
}

TEST_CASE("the scenario golden hash", "[core][sim][determinism]") {
    // Changing this means simulation behavior or hashing changed; saved games and replays change with it.
    auto sim = make_scenario(2024);
    for (int i = 0; i < 100; ++i) {
        sim->step();
    }
    REQUIRE(sim->world().state_hash() == 0x99433be45f99a1d8ULL);
}
