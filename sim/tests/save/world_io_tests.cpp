#include "citysim/core/state_hasher.hpp"
#include "citysim/core/world.hpp"
#include "citysim/save/archive.hpp"
#include "citysim/save/bind_serializer.hpp"
#include "citysim/save/world_io.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace citysim;
using namespace citysim::save;

namespace {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
};
template <class Ar> void serialize(Ar& ar, Position& p, std::uint32_t version) {
    io(ar, p.x, version);
    io(ar, p.y, version);
}
void hash_position(core::StateHasher& hasher, const Position& p) {
    hasher.add_float(p.x);
    hasher.add_float(p.y);
}

// Refers to another entity, which is what the remap exists for.
struct Follows {
    entt::entity target = entt::null;
    std::vector<std::string> notes;
};
template <class Ar> void serialize(Ar& ar, Follows& f, std::uint32_t version) {
    io(ar, f.target, version);
    io(ar, f.notes, version);
}
void hash_follows(core::StateHasher& hasher, const Follows& f) {
    hasher.add_pod(entt::to_integral(f.target));
    for (const std::string& note : f.notes) {
        hasher.add(note);
    }
}

struct Marker {};

struct Score {
    std::uint32_t points = 0;
    std::uint32_t bonus = 0; // version 2
};
template <class Ar> void serialize(Ar& ar, Score& s, std::uint32_t version) {
    io(ar, s.points, version);
    if (version >= 2) {
        io(ar, s.bonus, version);
    }
}
void hash_score(core::StateHasher& hasher, const Score& s) {
    hasher.add_pod(s.points);
    hasher.add_pod(s.bonus);
}

struct Weather {
    std::int32_t temperature = 0;
    std::optional<std::string> forecast;
};
template <class Ar> void serialize(Ar& ar, Weather& w, std::uint32_t version) {
    io(ar, w.temperature, version);
    io(ar, w.forecast, version);
}
void hash_weather(core::StateHasher& hasher, const Weather& w) {
    hasher.add_pod(w.temperature);
    hasher.add(w.forecast.value_or("(none)"));
}

struct Unserializable {
    int value = 0;
};

void register_all(core::World& world, std::uint32_t score_version = 2) {
    auto& registry = world.registry();
    core::Hooks<Position> position;
    position.hash = &hash_position;
    bind_serializer(position);
    registry.register_type<Position>("test.Position", position);

    core::Hooks<Follows> follows;
    follows.hash = &hash_follows;
    bind_serializer(follows);
    registry.register_type<Follows>("test.Follows", follows);

    registry.register_type<Marker>("test.Marker", {}); // empty: needs no hooks

    core::Hooks<Score> score;
    score.hash = &hash_score;
    bind_serializer(score, score_version);
    registry.register_type<Score>("test.Score", score);

    core::Hooks<Weather> weather;
    weather.hash = &hash_weather;
    bind_serializer(weather);
    registry.register_singleton<Weather>("test.Weather", weather);
}

// Entities 0..4 with a chain of references; never destroys anything, so entity values are dense.
void populate(core::World& world) {
    std::vector<entt::entity> e;
    for (std::size_t i = 0; i < 5; ++i) {
        e.push_back(world.create());
    }
    for (std::size_t i = 0; i < 5; ++i) {
        if (i % 2 == 0) {
            world.emplace<Position>(e[i], Position{static_cast<float>(i) * 1.5F, -0.25F});
        }
        if (i > 0) {
            world.emplace<Follows>(e[i], Follows{e[i - 1], {"follows " + std::to_string(i - 1)}});
        }
    }
    world.emplace<Marker>(e[3]);
    world.emplace<Score>(e[1], Score{10, 5});
    world.ctx().emplace<Weather>(Weather{-3, "snow"});
}

std::vector<std::byte> save_bytes(const core::World& world) {
    BinaryWriter writer;
    save_world(world, writer);
    const auto bytes = writer.bytes();
    return {bytes.begin(), bytes.end()};
}

} // namespace

TEST_CASE("a world round-trips to an equal state hash", "[save][world]") {
    core::World original;
    register_all(original);
    populate(original);

    const auto bytes = save_bytes(original);

    core::World loaded;
    register_all(loaded);
    BinaryReader reader(bytes);
    const WorldLoadReport report = load_world(loaded, reader);
    CHECK(report.skipped_types.empty());
    CHECK(reader.at_end());

    CHECK(loaded.entity_count() == original.entity_count());
    CHECK(loaded.state_hash() == original.state_hash());
}

TEST_CASE("saving is repeatable: the same state gives the same bytes", "[save][world]") {
    core::World a;
    register_all(a);
    populate(a);
    core::World b;
    register_all(b);
    populate(b);
    CHECK(save_bytes(a) == save_bytes(b));

    // Save, load and save again: the second file is identical to the first.
    const auto first = save_bytes(a);
    core::World loaded;
    register_all(loaded);
    BinaryReader reader(first);
    load_world(loaded, reader);
    CHECK(save_bytes(loaded) == first);
}

TEST_CASE("registration order does not change the bytes", "[save][world]") {
    core::World a;
    register_all(a);
    populate(a);

    // The same types registered in the opposite order.
    core::World b;
    {
        auto& registry = b.registry();
        core::Hooks<Weather> weather;
        weather.hash = &hash_weather;
        bind_serializer(weather);
        registry.register_singleton<Weather>("test.Weather", weather);
        core::Hooks<Score> score;
        score.hash = &hash_score;
        bind_serializer(score, 2);
        registry.register_type<Score>("test.Score", score);
        registry.register_type<Marker>("test.Marker", {});
        core::Hooks<Follows> follows;
        follows.hash = &hash_follows;
        bind_serializer(follows);
        registry.register_type<Follows>("test.Follows", follows);
        core::Hooks<Position> position;
        position.hash = &hash_position;
        bind_serializer(position);
        registry.register_type<Position>("test.Position", position);
    }
    populate(b);
    CHECK(save_bytes(a) == save_bytes(b));
}

TEST_CASE("entity references are remapped when ids differ", "[save][world]") {
    // Build a world whose entity ids are not dense (some were destroyed and recycled), then load it into a
    // fresh one: references must still point at the right entities.
    core::World original;
    register_all(original);
    const auto doomed = original.create();
    const auto a = original.create();
    original.destroy(doomed);
    const auto recycled = original.create(); // reuses doomed's index with a new version
    const auto b = original.create();
    original.emplace<Position>(a, Position{1.0F, 1.0F});
    original.emplace<Position>(recycled, Position{2.0F, 2.0F});
    original.emplace<Follows>(b, Follows{recycled, {"to recycled"}});
    original.emplace<Follows>(recycled, Follows{a, {"to a"}});

    const auto bytes = save_bytes(original);
    core::World loaded;
    register_all(loaded);
    BinaryReader reader(bytes);
    load_world(loaded, reader);

    REQUIRE(loaded.entity_count() == 3);
    // Find the loaded entity holding each position and follow the references.
    entt::entity at_one = entt::null;
    entt::entity at_two = entt::null;
    for (const auto entity : loaded.view<Position>()) {
        (loaded.get<Position>(entity).x == 1.0F ? at_one : at_two) = entity;
    }
    REQUIRE(at_one != entt::null);
    REQUIRE(at_two != entt::null);
    CHECK(loaded.get<Follows>(at_two).target == at_one);
    CHECK(loaded.get<Follows>(at_two).notes == std::vector<std::string>{"to a"});
    entt::entity follower = entt::null;
    for (const auto entity : loaded.view<Follows>()) {
        if (loaded.try_get<Position>(entity) == nullptr) {
            follower = entity;
        }
    }
    REQUIRE(follower != entt::null);
    CHECK(loaded.get<Follows>(follower).target == at_two);

    // And the saved form is canonical: saving the loaded world gives the same bytes.
    CHECK(save_bytes(loaded) == bytes);
}

TEST_CASE("empty entities, tag components and absent singletons are kept", "[save][world]") {
    core::World original;
    register_all(original);
    original.create(); // no components at all
    const auto tagged = original.create();
    original.emplace<Marker>(tagged);

    const auto bytes = save_bytes(original);
    core::World loaded;
    register_all(loaded);
    BinaryReader reader(bytes);
    load_world(loaded, reader);
    CHECK(loaded.entity_count() == 2);
    CHECK(loaded.view<Marker>().size() == 1);
    CHECK(loaded.ctx().find<Weather>() == nullptr);
    CHECK(loaded.state_hash() == original.state_hash());
}

TEST_CASE("an older section version is passed to serialize", "[save][world]") {
    core::World original;
    register_all(original, /*score_version=*/1); // as an older build
    const auto e = original.create();
    original.emplace<Score>(e, Score{10, 0});
    const auto bytes = save_bytes(original);

    core::World loaded;
    register_all(loaded, /*score_version=*/2);
    BinaryReader reader(bytes);
    load_world(loaded, reader);
    const auto entity = *loaded.view<Score>().begin();
    CHECK(loaded.get<Score>(entity).points == 10);
    CHECK(loaded.get<Score>(entity).bonus == 0); // default for the field the old save lacks
}

TEST_CASE("a section from a newer build is refused", "[save][world]") {
    core::World newer;
    register_all(newer, /*score_version=*/3);
    newer.emplace<Score>(newer.create(), Score{1, 1});
    const auto bytes = save_bytes(newer);

    core::World loaded;
    register_all(loaded, /*score_version=*/2);
    BinaryReader reader(bytes);
    CHECK_THROWS_AS(load_world(loaded, reader), SaveFormatError);
}

TEST_CASE("types this build does not register are skipped and reported", "[save][world]") {
    core::World original;
    register_all(original);
    populate(original);
    const auto bytes = save_bytes(original);

    core::World loaded; // no Score, no Weather
    {
        auto& registry = loaded.registry();
        core::Hooks<Position> position;
        position.hash = &hash_position;
        bind_serializer(position);
        registry.register_type<Position>("test.Position", position);
        core::Hooks<Follows> follows;
        follows.hash = &hash_follows;
        bind_serializer(follows);
        registry.register_type<Follows>("test.Follows", follows);
        registry.register_type<Marker>("test.Marker", {});
    }
    BinaryReader reader(bytes);
    const WorldLoadReport report = load_world(loaded, reader);
    CHECK(report.skipped_types == std::vector<std::string>{"test.Score", "test.Weather"});
    CHECK(loaded.entity_count() == 5);
    CHECK(loaded.view<Position>().size() == 3);
}

TEST_CASE("every truncation of a world save is a SaveFormatError", "[save][world]") {
    core::World original;
    register_all(original);
    populate(original);
    const auto bytes = save_bytes(original);
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        core::World loaded;
        register_all(loaded);
        BinaryReader reader(std::span<const std::byte>(bytes.data(), length));
        CHECK_THROWS_AS(load_world(loaded, reader), SaveFormatError);
    }
}

TEST_CASE("a corrupt entity reference is rejected", "[save][world]") {
    core::World original;
    register_all(original);
    const auto a = original.create();
    const auto b = original.create();
    original.emplace<Follows>(b, Follows{a, {}});
    auto bytes = save_bytes(original);

    // The reference is written as index + 1, so entity 0 is the byte 0x01; make it point past the end.
    bool changed = false;
    for (std::size_t i = 0; i + 1 < bytes.size() && !changed; ++i) {
        if (bytes[i] == std::byte{0x01} && bytes[i + 1] == std::byte{0x00}) { // target 0, then zero notes
            bytes[i] = std::byte{0x7F};
            changed = true;
        }
    }
    REQUIRE(changed);
    core::World loaded;
    register_all(loaded);
    BinaryReader reader(bytes);
    CHECK_THROWS_AS(load_world(loaded, reader), SaveFormatError);
}

TEST_CASE("a registered type without serialize hooks cannot be saved", "[save][world]") {
    core::World world;
    register_all(world);
    world.registry().register_type<Unserializable>("test.Unserializable", {});
    BinaryWriter writer;
    CHECK_THROWS_AS(save_world(world, writer), std::logic_error);
    BinaryReader reader(std::span<const std::byte>{});
    CHECK_THROWS_AS(load_world(world, reader), std::logic_error);
}

TEST_CASE("loading needs an empty world", "[save][world]") {
    core::World original;
    register_all(original);
    populate(original);
    const auto bytes = save_bytes(original);
    BinaryReader reader(bytes);
    CHECK_THROWS_AS(load_world(original, reader), std::logic_error);
}

TEST_CASE("saving a reference to a missing entity is a logic error", "[save][world]") {
    core::World world;
    register_all(world);
    const auto a = world.create();
    const auto b = world.create();
    world.emplace<Follows>(a, Follows{b, {}});
    world.destroy(b);
    BinaryWriter writer;
    CHECK_THROWS_AS(save_world(world, writer), std::logic_error);
}
