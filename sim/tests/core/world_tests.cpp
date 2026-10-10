#include "citysim/core/world.hpp"

#include <catch2/catch_test_macros.hpp>

using citysim::core::World;

namespace {
struct Position {
    int x = 0;
    int y = 0;
};
struct Tag {};
struct Calendar {
    int day = 0;
};
} // namespace

TEST_CASE("World creates and destroys entities", "[core][world]") {
    World world;
    REQUIRE(world.entity_count() == 0);

    const auto a = world.create();
    const auto b = world.create();
    REQUIRE(world.valid(a));
    REQUIRE(world.valid(b));
    REQUIRE(world.entity_count() == 2);

    world.destroy(a);
    REQUIRE_FALSE(world.valid(a));
    REQUIRE(world.valid(b));
    REQUIRE(world.entity_count() == 1);
}

TEST_CASE("World component access", "[core][world]") {
    World world;
    const auto e = world.create();

    REQUIRE(world.try_get<Position>(e) == nullptr);
    world.emplace<Position>(e, 3, 4);
    REQUIRE(world.get<Position>(e).x == 3);
    REQUIRE(world.try_get<Position>(e) != nullptr);

    world.get<Position>(e).y = 9;
    const World& cworld = world;
    REQUIRE(cworld.get<Position>(e).y == 9);

    REQUIRE(world.remove<Position>(e) == 1);
    REQUIRE(world.try_get<Position>(e) == nullptr);
}

TEST_CASE("World views iterate matching entities", "[core][world]") {
    World world;
    const auto a = world.create();
    const auto b = world.create();
    world.emplace<Position>(a);
    world.emplace<Position>(b);
    world.emplace<Tag>(b);

    int positions = 0;
    for (auto entity : world.view<Position>()) {
        (void)entity;
        ++positions;
    }
    REQUIRE(positions == 2);

    int tagged = 0;
    for (auto entity : world.view<Position, Tag>()) {
        REQUIRE(entity == b);
        ++tagged;
    }
    REQUIRE(tagged == 1);
}

TEST_CASE("World context holds singleton state", "[core][world]") {
    World world;
    REQUIRE(world.ctx().find<Calendar>() == nullptr);
    world.ctx().emplace<Calendar>(Calendar{7});
    REQUIRE(world.ctx().get<Calendar>().day == 7);
}

TEST_CASE("World is movable and owns a component registry", "[core][world]") {
    static_assert(!std::is_copy_constructible_v<World>);
    static_assert(std::is_nothrow_move_constructible_v<World>);

    World world;
    world.registry().register_type<Position>("core.Position", {});
    const auto e = world.create();
    world.emplace<Position>(e, 1, 2);

    World moved = std::move(world);
    REQUIRE(moved.valid(e));
    REQUIRE(moved.get<Position>(e).y == 2);
    REQUIRE(moved.registry().find("core.Position") != nullptr);
}
