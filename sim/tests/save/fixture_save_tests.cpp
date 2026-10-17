// Loads a save written by an older layout of the same types: sim/tests/fixtures/saves/villagers_v1.csav.
//
// POLICY (architecture §10): whenever a serialized layout changes, bump the type's version, add a migration
// from the old version, and add a fixture written by the OLD layout so every migration stays tested forever.
// To make a new fixture, copy the old struct into the generator below as it was, run
//     citysim_tests "[generate]"
// and commit the file it writes. The generator is hidden from normal runs.

#include "citysim/core/simulation.hpp"
#include "citysim/core/state_hasher.hpp"
#include "citysim/save/archive.hpp"
#include "citysim/save/bind_serializer.hpp"
#include "citysim/save/simulation_io.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

using namespace citysim;
using namespace citysim::save;

namespace {

// ---- the current layout (version 2)
// --------------------------------------------------------------------------
struct Villager {
    std::string name;
    std::uint32_t age = 0;
    std::int32_t mood = 0; // added in version 2
};
template <class Ar> void serialize(Ar& ar, Villager& v, std::uint32_t version) {
    io(ar, v.name, version);
    io(ar, v.age, version);
    io(ar, v.mood, version);
}
void hash_villager(core::StateHasher& hasher, const Villager& v) {
    hasher.add(v.name);
    hasher.add_pod(v.age);
    hasher.add_pod(v.mood);
}

// ---- the layout the fixture was written with (version 1), kept for the generator
// ---------------------------------
struct VillagerV1 {
    std::string name;
    std::uint32_t age = 0;
};
template <class Ar> void serialize(Ar& ar, VillagerV1& v, std::uint32_t version) {
    io(ar, v.name, version);
    io(ar, v.age, version);
}

// ---- unchanged
// ---------------------------------------------------------------------------------------------------
struct Town {
    std::string name;
    std::uint32_t founded = 0;
};
template <class Ar> void serialize(Ar& ar, Town& t, std::uint32_t version) {
    io(ar, t.name, version);
    io(ar, t.founded, version);
}
void hash_town(core::StateHasher& hasher, const Town& t) {
    hasher.add(t.name);
    hasher.add_pod(t.founded);
}

constexpr std::int32_t k_migrated_mood = 50;

void install_town(core::Simulation& sim) {
    core::Hooks<Town> hooks;
    hooks.hash = &hash_town;
    bind_serializer(hooks);
    sim.world().registry().register_singleton<Town>("test.Town", hooks);
    sim.world().ctx().emplace<Town>();
    sim.dispatcher().register_handler<core::NoOp>([](core::TickContext&, const core::NoOp&) {});
}

void install_current(core::Simulation& sim) {
    install_town(sim);
    core::Hooks<Villager> hooks;
    hooks.hash = &hash_villager;
    bind_serializer(hooks, 2);
    sim.world().registry().register_type<Villager>("test.Villager", hooks);
}

} // namespace

TEST_CASE("a version 1 save loads through its migration", "[save][migration][fixture]") {
    MigrationRegistry registry;
    registry.add_type("test.Villager", 1, [](BinaryReader& in, BinaryWriter& out) {
        std::string name;
        std::uint32_t age = 0;
        in.string(name);
        in.fixed(age);
        out.string(name);
        out.fixed(age);
        out.fixed(k_migrated_mood); // io() writes int32 as fixed width
    });

    core::Simulation sim(1, 1);
    install_current(sim);
    LoadOptions options;
    options.migrations = &registry;
    const LoadOutcome outcome = load_simulation(
        sim, std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "saves" / "villagers_v1.csav", options);

    CHECK(sim.tick() == 5);
    CHECK(sim.world_seed() == 99);
    CHECK(outcome.header.world_seed == 99);
    CHECK(sim.world().ctx().get<Town>().name == "Hearthstone");
    CHECK(sim.world().ctx().get<Town>().founded == 1203);

    std::vector<std::string> names;
    for (const auto entity : sim.world().view<Villager>()) {
        const Villager& villager = sim.world().get<Villager>(entity);
        names.push_back(villager.name + ":" + std::to_string(villager.age));
        CHECK(villager.mood == k_migrated_mood); // the default the migration gives old villagers
    }
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"Ada:34", "Bo:7", "Cy:61"});

    CHECK(sim.pending_commands().size() == 1);
    CHECK(sim.input_log().entries().size() == 1);

    // And it is an ordinary save from here on: saving and loading again keeps everything.
    const auto bytes = save_simulation_bytes(sim, SaveOptions{});
    core::Simulation again(1, 1);
    install_current(again);
    load_simulation_bytes(again, bytes, LoadOptions{}); // current version: no migration needed
    CHECK(again.world().state_hash() == sim.world().state_hash());
}

TEST_CASE("a version 1 save without its migration fails clearly", "[save][migration][fixture]") {
    core::Simulation sim(1, 1);
    install_current(sim);
    try {
        load_simulation(sim, std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "saves" / "villagers_v1.csav",
                        LoadOptions{});
        FAIL("expected an error");
    } catch (const SaveFormatError& error) {
        const std::string message = error.what();
        CHECK(message.find("test.Villager") != std::string::npos);
        CHECK(message.find("no migration from 1 to 2") != std::string::npos);
    }
}

// Hidden by default (the "[.]" tag): rewrites the fixture. Run with: citysim_tests "[generate]"
TEST_CASE("generate the villagers v1 fixture", "[.][generate]") {
    core::Simulation sim(99, 1);
    install_town(sim);
    core::Hooks<VillagerV1> hooks;
    bind_serializer(hooks, 1);
    sim.world().registry().register_type<VillagerV1>("test.Villager", hooks);

    sim.world().ctx().get<Town>() = Town{"Hearthstone", 1203};
    const std::pair<const char*, std::uint32_t> people[] = {{"Ada", 34}, {"Bo", 7}, {"Cy", 61}};
    for (const auto& [name, age] : people) {
        sim.world().emplace<VillagerV1>(sim.world().create(), VillagerV1{name, age});
    }
    sim.enqueue({core::SequencedCommand{1, 0, core::NoOp{}}});
    sim.step(); // applies it, so the input log has one entry
    sim.step();
    sim.step();
    sim.step();
    sim.step();
    sim.enqueue({core::SequencedCommand{2, 5, core::NoOp{}}}); // left waiting, so CMDQ is not empty

    SaveOptions options;
    options.saved_at_unix_seconds = 1'700'000'000; // fixed, so regenerating gives the same bytes
    const std::filesystem::path path =
        std::filesystem::path(CITYSIM_TEST_FIXTURES_DIR) / "saves" / "villagers_v1.csav";
    std::filesystem::create_directories(path.parent_path());
    save_simulation(sim, path, options);
    SUCCEED("wrote " + path.generic_string());
}
