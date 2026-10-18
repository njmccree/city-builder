#include "citysim/core/state_hasher.hpp"
#include "citysim/core/world.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using citysim::core::Hooks;
using citysim::core::StateHasher;
using citysim::core::World;

namespace {
struct Position {
    std::int32_t x = 0;
    std::int32_t y = 0;
};
struct Health {
    float value = 0.0F;
};
struct Marker {};
struct Calendar {
    std::int32_t day = 0;
};

void hash_position(StateHasher& hasher, const Position& p) {
    hasher.add_pod(p.x);
    hasher.add_pod(p.y);
}
void hash_health(StateHasher& hasher, const Health& h) {
    hasher.add_float(h.value);
}
void hash_calendar(StateHasher& hasher, const Calendar& c) {
    hasher.add_pod(c.day);
}

void register_all(World& world, bool reversed = false) {
    Hooks<Position> position;
    position.hash = &hash_position;
    Hooks<Health> health;
    health.hash = &hash_health;
    Hooks<Calendar> calendar;
    calendar.hash = &hash_calendar;
    auto& registry = world.registry();
    if (!reversed) {
        registry.register_type<Position>("test.Position", position);
        registry.register_type<Health>("test.Health", health);
        registry.register_type<Marker>("test.Marker", {});
        registry.register_singleton<Calendar>("test.Calendar", calendar);
    } else {
        registry.register_singleton<Calendar>("test.Calendar", calendar);
        registry.register_type<Marker>("test.Marker", {});
        registry.register_type<Health>("test.Health", health);
        registry.register_type<Position>("test.Position", position);
    }
}

void populate(World& world) {
    for (int i = 0; i < 4; ++i) {
        const auto e = world.create();
        world.emplace<Position>(e, i, i * 2);
        if (i % 2 == 0) {
            world.emplace<Health>(e, 10.0F + static_cast<float>(i));
        }
    }
    world.ctx().emplace<Calendar>(Calendar{3});
}
} // namespace

TEST_CASE("same operations give the same state hash", "[core][hash]") {
    World a;
    World b;
    register_all(a);
    register_all(b);
    populate(a);
    populate(b);
    REQUIRE(a.state_hash() == b.state_hash());
    REQUIRE(a.state_hash() == a.state_hash());
}

TEST_CASE("state hash golden value", "[core][hash]") {
    // Changing this means every saved-game hash and golden scenario changes with it.
    World world;
    register_all(world);
    populate(world);
    REQUIRE(world.state_hash() == 0x1aeb3961ad1c19acULL);
}

TEST_CASE("changing one field changes the hash", "[core][hash]") {
    World a;
    World b;
    register_all(a);
    register_all(b);
    populate(a);
    populate(b);

    const auto before = a.state_hash();
    const auto entity = *a.view<Position>().begin();
    a.get<Position>(entity).y += 1;
    REQUIRE(a.state_hash() != before);
    REQUIRE(a.state_hash() != b.state_hash());

    a.get<Position>(entity).y -= 1;
    REQUIRE(a.state_hash() == before);
}

TEST_CASE("singletons, entities and components all affect the hash", "[core][hash]") {
    World world;
    register_all(world);
    populate(world);
    const auto base = world.state_hash();

    world.ctx().get<Calendar>().day = 4;
    const auto after_singleton = world.state_hash();
    REQUIRE(after_singleton != base);

    const auto extra = world.create();
    const auto after_entity = world.state_hash();
    REQUIRE(after_entity != after_singleton);

    world.emplace<Marker>(extra);
    REQUIRE(world.state_hash() != after_entity);

    world.destroy(extra);
    REQUIRE(world.state_hash() != after_entity);
}

TEST_CASE("an absent singleton hashes differently from a present one", "[core][hash]") {
    World with;
    World without;
    register_all(with);
    register_all(without);
    with.ctx().emplace<Calendar>(Calendar{0});
    REQUIRE(with.state_hash() != without.state_hash());
}

TEST_CASE("registration order does not matter", "[core][hash]") {
    World a;
    World b;
    register_all(a, false);
    register_all(b, true);
    populate(a);
    populate(b);
    REQUIRE(a.state_hash() == b.state_hash());
}

TEST_CASE("component insertion order does not matter for the same final state", "[core][hash]") {
    World a;
    World b;
    register_all(a);
    register_all(b);

    std::array<World::Entity, 3> ea{};
    std::array<World::Entity, 3> eb{};
    for (std::size_t i = 0; i < 3; ++i) {
        ea[i] = a.create();
        eb[i] = b.create();
    }
    REQUIRE(ea == eb); // fresh worlds hand out the same ids

    // Same final components, attached in opposite orders, so the dense storage orders differ.
    for (std::size_t i = 0; i < 3; ++i) {
        a.emplace<Position>(ea[i], static_cast<std::int32_t>(i), 5);
    }
    for (std::size_t i = 3; i-- > 0;) {
        b.emplace<Position>(eb[i], static_cast<std::int32_t>(i), 5);
    }
    a.emplace<Health>(ea[0], 1.0F);
    a.emplace<Health>(ea[2], 3.0F);
    b.emplace<Health>(eb[2], 3.0F);
    b.emplace<Health>(eb[0], 1.0F);

    REQUIRE(a.state_hash() == b.state_hash());
}

TEST_CASE("unregistered components are not part of the hash", "[core][hash]") {
    // Documents the rule from architecture §4 rule 8: unregistered state is a bug, and the hash cannot see
    // it.
    World world;
    register_all(world);
    populate(world);
    const auto base = world.state_hash();
    world.emplace<std::int64_t>(world.create(), 5);
    REQUIRE(world.state_hash() != base); // the new entity itself is hashed
}
