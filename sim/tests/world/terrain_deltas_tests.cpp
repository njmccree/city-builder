#include "citysim/content/content_database.hpp"
#include "citysim/core/job_system.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/save/save_format_error.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/world/terrain_deltas.hpp"
#include "citysim/world/world_module.hpp"
#include "citysim/world/world_terrain.hpp"
#include "support/map_content.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

using namespace citysim;
using namespace citysim::world;
using citysim::test::MapContent;
using citysim::test::TempDir;

namespace {

constexpr std::uint32_t k_last = k_city_tile_samples - 1;

// The small map: 8 x 8 tiles of which (2..5, 3..6) have detail data, so these tiles are well inside.
const TileCoord k_a{3, 4};
const TileCoord k_east{4, 4};
const TileCoord k_north{3, 5};
const TileCoord k_north_east{4, 5};
const TileCoord k_west{2, 4};
const TileCoord k_south{3, 3};

struct Fixture {
    TempDir dir;
    std::unique_ptr<MapContent> map;
    std::unique_ptr<content::ContentDatabase> db;
    std::shared_ptr<const LoadedMap> loaded;
    core::JobSystem jobs{2};

    explicit Fixture(const char* label) : dir(label) {
        map = std::make_unique<MapContent>(dir);
        db = std::make_unique<content::ContentDatabase>();
        REQUIRE(db->load({map->mod_dir()}).ok());
        loaded = open_map(*db, MapContent::map_id());
    }
    WorldTerrain terrain(std::vector<TileCoord> active = {}) {
        WorldTerrain t(loaded, 1);
        t.activate_tiles(jobs, active);
        return t;
    }
};

const WorldTerrain::HeightFn raise(float meters) {
    return [meters](std::uint32_t, std::uint32_t, float height) { return height + meters; };
}

bool same_heights(const CityTile& a, const CityTile& b) {
    return std::memcmp(a.heights().data(), b.heights().data(), a.heights().size_bytes()) == 0;
}

std::vector<float> zeros() {
    return std::vector<float>(TileDelta::k_sample_count, 0.0F);
}

} // namespace

TEST_CASE("an unedited tile allocates no delta layer", "[world][deltas]") {
    Fixture f("deltas_none");
    WorldTerrain terrain = f.terrain({k_a, k_east, k_north});
    CHECK(terrain.deltas().empty());
    CHECK(terrain.deltas().find(k_a) == nullptr);
    CHECK(terrain.saved_state().deltas.empty());
    // An edit that changes nothing stays free: no layer is made for a zero offset.
    terrain.apply_delta(k_a, SampleRect{10, 10, 20, 20}, raise(0.0F));
    CHECK(terrain.deltas().empty());
    // A real edit makes exactly one layer.
    terrain.apply_delta(k_a, SampleRect{10, 10, 20, 20}, raise(1.0F));
    CHECK(terrain.deltas().size() == 1);
    CHECK(terrain.deltas().find(k_a) != nullptr);
    CHECK(terrain.deltas().find(k_east) == nullptr);
}

TEST_CASE("an edit shows in height_at, makes a new tile version and leaves the other tiles alone",
          "[world][deltas]") {
    Fixture f("deltas_edit");
    WorldTerrain terrain = f.terrain({k_a, k_east, k_north});
    const auto before = terrain.tile(k_a);
    const auto other = terrain.tile(k_east);
    const WorldPos origin = tile_origin(k_a);
    const WorldPos inside{origin.x + 40.0, origin.z + 40.0}; // sample (10, 10)
    const float was = terrain.height_at(inside);

    terrain.apply_delta(k_a, SampleRect{8, 8, 12, 12}, raise(2.5F));

    const auto after = terrain.tile(k_a);
    CHECK(after != before);
    CHECK(after->version() == before->version() + 1);
    CHECK(terrain.tile(k_east) == other); // the same pointer: the renderer rebuilds nothing there
    CHECK(terrain.height_at(inside) == after->sample(10, 10));
    CHECK(std::abs(terrain.height_at(inside) - (was + 2.5F)) < 1e-4F);
    CHECK(after->sample(7, 8) == before->sample(7, 8)); // outside the rectangle: untouched
    CHECK(after->sample(13, 12) == before->sample(13, 12));
    // The old tile is immutable: whoever holds it still sees the old heights.
    CHECK(before->sample(10, 10) == was);
    // The dirty rectangle is the box around the edit.
    const TileDelta* layer = terrain.deltas().find(k_a);
    REQUIRE(layer != nullptr);
    REQUIRE(layer->dirty().has_value());
    CHECK(*layer->dirty() == SampleRect{8, 8, 12, 12});
    CHECK(layer->nonzero_count() == 25);
}

TEST_CASE("edits add up from the synthesized base and can be undone", "[world][deltas]") {
    Fixture f("deltas_undo");
    WorldTerrain terrain = f.terrain({k_a});
    const auto base = terrain.tile(k_a);
    terrain.apply_delta(k_a, SampleRect{0, 0, 30, 30}, raise(1.0F));
    terrain.apply_delta(k_a, SampleRect{0, 0, 30, 30}, raise(1.0F));
    CHECK(std::abs(terrain.tile(k_a)->sample(5, 5) - (base->sample(5, 5) + 2.0F)) < 1e-4F);

    // Setting the samples back to the base heights removes the layer and restores the tile exactly.
    const WorldTerrain::HeightFn original = [&](std::uint32_t sx, std::uint32_t sy, float) {
        return base->sample(sx, sy);
    };
    terrain.apply_delta(k_a, SampleRect{0, 0, 30, 30}, original);
    CHECK(terrain.deltas().empty());
    CHECK(same_heights(*terrain.tile(k_a), *base));
    CHECK(terrain.tile(k_a)->version() == base->version() + 3);
}

TEST_CASE("edits at a tile edge keep the shared samples equal in every neighbor", "[world][deltas]") {
    Fixture f("deltas_edges");
    WorldTerrain terrain = f.terrain({k_a, k_east, k_north, k_north_east, k_west, k_south});
    const auto south_before = terrain.tile(k_south);
    // The east edge column of A, the north edge row, and the north-east corner shared by four tiles.
    terrain.apply_delta(k_a, SampleRect{k_last, 100, k_last, 200}, raise(3.0F));
    terrain.apply_delta(k_a, SampleRect{50, k_last, 60, k_last}, raise(-2.0F));
    terrain.apply_delta(k_a, SampleRect{k_last, k_last, k_last, k_last}, raise(5.0F));

    const auto a = terrain.tile(k_a);
    const auto east = terrain.tile(k_east);
    const auto north = terrain.tile(k_north);
    const auto north_east = terrain.tile(k_north_east);
    for (std::uint32_t i = 0; i < k_city_tile_samples; ++i) {
        REQUIRE(a->sample(k_last, i) == east->sample(0, i));
        REQUIRE(a->sample(i, k_last) == north->sample(i, 0));
    }
    // The corner is the same height in all four tiles.
    CHECK(a->sample(k_last, k_last) == east->sample(0, k_last));
    CHECK(a->sample(k_last, k_last) == north->sample(k_last, 0));
    CHECK(a->sample(k_last, k_last) == north_east->sample(0, 0));
    // And a tile that does not touch the edit is untouched.
    CHECK(terrain.tile(k_south) == south_before);
    CHECK(terrain.deltas().find(k_south) == nullptr);
    CHECK(terrain.deltas().find(k_west) == nullptr);

    // An edit on the west edge reaches the west neighbor, and one on the south edge the south neighbor.
    terrain.apply_delta(k_a, SampleRect{0, 300, 0, 310}, raise(1.5F));
    terrain.apply_delta(k_a, SampleRect{400, 0, 410, 0}, raise(1.5F));
    for (std::uint32_t i = 300; i <= 310; ++i) {
        REQUIRE(terrain.tile(k_a)->sample(0, i) == terrain.tile(k_west)->sample(k_last, i));
    }
    for (std::uint32_t i = 400; i <= 410; ++i) {
        REQUIRE(terrain.tile(k_a)->sample(i, 0) == terrain.tile(k_south)->sample(i, k_last));
    }
}

TEST_CASE("a neighbor that is not active keeps the shared edit for when it is", "[world][deltas]") {
    Fixture f("deltas_inactive_neighbor");
    WorldTerrain terrain = f.terrain({k_a});
    terrain.apply_delta(k_a, SampleRect{k_last, 10, k_last, 20}, raise(4.0F));
    REQUIRE(terrain.deltas().find(k_east) != nullptr);
    CHECK(terrain.deltas().find(k_east)->nonzero_count() == 11);

    terrain.activate_tiles(f.jobs, std::vector<TileCoord>{k_east});
    for (std::uint32_t i = 10; i <= 20; ++i) {
        REQUIRE(terrain.tile(k_a)->sample(k_last, i) == terrain.tile(k_east)->sample(0, i));
    }
    // Further edits in the late tile still start from its synthesized base.
    terrain.apply_delta(k_east, SampleRect{0, 10, 0, 10}, raise(1.0F));
    CHECK(terrain.tile(k_a)->sample(k_last, 10) == terrain.tile(k_east)->sample(0, 10));
}

TEST_CASE("edits of a tile that is not active survive deactivate and activate", "[world][deltas]") {
    Fixture f("deltas_survive");
    WorldTerrain terrain = f.terrain({k_a});
    terrain.apply_delta(k_a, SampleRect{100, 100, 140, 140}, raise(-6.0F));
    const auto edited = terrain.tile(k_a);

    terrain.deactivate_tiles(std::vector<TileCoord>{k_a});
    CHECK(!terrain.is_active(k_a));
    CHECK(terrain.deltas().find(k_a) != nullptr); // still there
    CHECK_THROWS_AS(terrain.apply_delta(k_a, SampleRect{0, 0, 1, 1}, raise(1.0F)), std::logic_error);

    terrain.activate_tiles(f.jobs, std::vector<TileCoord>{k_a});
    CHECK(same_heights(*terrain.tile(k_a), *edited));
    // The same after deactivate_all.
    terrain.deactivate_all();
    CHECK(terrain.deltas().size() == 1);
    terrain.activate_tiles(f.jobs, std::vector<TileCoord>{k_a});
    CHECK(same_heights(*terrain.tile(k_a), *edited));
}

TEST_CASE("a bad edit changes nothing", "[world][deltas]") {
    Fixture f("deltas_errors");
    WorldTerrain terrain = f.terrain({k_a, k_east});
    const auto before = terrain.tile(k_a);

    CHECK_THROWS_AS(terrain.apply_delta(k_a, SampleRect{5, 5, 4, 9}, raise(1.0F)), std::invalid_argument);
    CHECK_THROWS_AS(terrain.apply_delta(k_a, SampleRect{0, 0, 513, 0}, raise(1.0F)), std::invalid_argument);
    CHECK_THROWS_AS(terrain.apply_delta(TileCoord{0, 0}, SampleRect{0, 0, 1, 1}, raise(1.0F)),
                    std::logic_error);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    // The last sample of the rectangle is the bad one; the earlier ones must not have been stored.
    const WorldTerrain::HeightFn bad_last = [&](std::uint32_t sx, std::uint32_t sy, float height) {
        return (sx == 9 && sy == 9) ? nan : height + 1.0F;
    };
    CHECK_THROWS_AS(terrain.apply_delta(k_a, SampleRect{0, 0, 9, 9}, bad_last), std::invalid_argument);
    CHECK_THROWS_AS(terrain.apply_delta(k_a, SampleRect{0, 0, 1, 1},
                                        [&](std::uint32_t, std::uint32_t, float) { return inf; }),
                    std::invalid_argument);
    CHECK(terrain.tile(k_a) == before);
    CHECK(terrain.deltas().empty());

    WorldTerrain no_map;
    CHECK_THROWS_AS(no_map.apply_delta(k_a, SampleRect{0, 0, 1, 1}, raise(1.0F)), std::logic_error);
}

TEST_CASE("the same edits give the same hash, and a different edit a different one", "[world][deltas]") {
    Fixture f("deltas_hash");
    const content::ContentDatabase& db = *f.db;
    core::Simulation a(1, 1);
    core::Simulation b(1, 1);
    for (core::Simulation* sim : {&a, &b}) {
        install(*sim, db, WorldConfig{MapContent::map_id(), f.loaded});
        sim->world().ctx().get<WorldTerrain>().activate_tiles(sim->jobs(),
                                                              std::vector<TileCoord>{k_a, k_east});
    }
    CHECK(a.world().state_hash() == b.world().state_hash());

    auto& ta = a.world().ctx().get<WorldTerrain>();
    auto& tb = b.world().ctx().get<WorldTerrain>();
    ta.apply_delta(k_a, SampleRect{1, 1, 5, 5}, raise(1.0F));
    CHECK(a.world().state_hash() != b.world().state_hash());
    tb.apply_delta(k_a, SampleRect{1, 1, 5, 5}, raise(1.0F));
    CHECK(a.world().state_hash() == b.world().state_hash());
    tb.apply_delta(k_a, SampleRect{1, 1, 1, 1}, raise(0.25F));
    CHECK(a.world().state_hash() != b.world().state_hash());
}

TEST_CASE("save and load keep the edits, the heights and the hash", "[world][deltas]") {
    Fixture f("deltas_save_load");
    const content::ContentDatabase& db = *f.db;
    core::Simulation original(5, 2);
    install(original, db, WorldConfig{MapContent::map_id(), f.loaded});
    auto& terrain = original.world().ctx().get<WorldTerrain>();
    terrain.activate_tiles(original.jobs(), std::vector<TileCoord>{k_a, k_east, k_north});
    terrain.apply_delta(k_a, SampleRect{20, 20, 90, 60},
                        [](std::uint32_t sx, std::uint32_t sy, float height) {
                            return height + 0.01F * static_cast<float>(sx) - 0.02F * static_cast<float>(sy);
                        });
    terrain.apply_delta(k_a, SampleRect{k_last, 5, k_last, 9}, raise(2.0F)); // shared with the east tile
    // And an edit on a tile that is then deactivated: it is saved with the rest.
    terrain.apply_delta(k_north, SampleRect{200, 200, 210, 210}, raise(-1.0F));
    terrain.deactivate_tiles(std::vector<TileCoord>{k_north});
    for (int i = 0; i < 3; ++i) {
        original.step();
    }

    save::SaveOptions save_options;
    save_options.content = &db;
    const std::vector<std::byte> bytes = save::save_simulation_bytes(original, save_options);
    // Sparse and compressed: three small edited layers are nowhere near 3 x 1 MB.
    CHECK(bytes.size() < 64 * 1024);

    core::Simulation loaded(5, 3);
    install(loaded, db, WorldConfig{MapContent::map_id(), f.loaded});
    save::LoadOptions load_options;
    load_options.content = &db;
    const save::LoadOutcome outcome = save::load_simulation_bytes(loaded, bytes, load_options);
    CHECK(outcome.warnings.empty());

    const WorldTerrain& before = original.world().ctx().get<WorldTerrain>();
    const WorldTerrain& after = loaded.world().ctx().get<WorldTerrain>();
    CHECK(after.deltas() == before.deltas());
    CHECK(after.saved_state() == before.saved_state());
    CHECK(loaded.world().state_hash() == original.world().state_hash());
    REQUIRE(after.active_tiles() == before.active_tiles());
    for (const auto& [coord, tile] : before.tiles()) {
        CHECK(same_heights(*tile, *after.tile(coord)));
    }
    // The edit on the inactive tile came back too, and applies when it is activated.
    CHECK(after.deltas().find(k_north) != nullptr);
    auto& reloaded = loaded.world().ctx().get<WorldTerrain>();
    reloaded.activate_tiles(loaded.jobs(), std::vector<TileCoord>{k_north});
    terrain.activate_tiles(original.jobs(), std::vector<TileCoord>{k_north});
    CHECK(same_heights(*reloaded.tile(k_north), *terrain.tile(k_north)));

    // And both go on the same way, edits included.
    for (int i = 0; i < 4; ++i) {
        original.step();
        loaded.step();
    }
    terrain.apply_delta(k_a, SampleRect{0, 0, 3, 3}, raise(1.0F));
    reloaded.apply_delta(k_a, SampleRect{0, 0, 3, 3}, raise(1.0F));
    CHECK(loaded.world().state_hash() == original.world().state_hash());
}

TEST_CASE("a damaged delta layer in a save is refused", "[world][deltas]") {
    CHECK_THROWS_AS(decompress_offsets(std::vector<std::uint8_t>{1, 2, 3, 4, 5}), std::invalid_argument);
    // A valid frame of the wrong size.
    const std::vector<float> small(16, 1.0F);
    const std::vector<std::uint8_t> packed =
        compress_offsets(std::vector<float>(TileDelta::k_sample_count, 1.0F));
    std::vector<std::uint8_t> truncated(packed.begin(),
                                        packed.begin() + static_cast<std::ptrdiff_t>(packed.size() / 2));
    CHECK_THROWS_AS(decompress_offsets(truncated), std::invalid_argument);
    CHECK_THROWS_AS(compress_offsets(small), std::invalid_argument);
}

TEST_CASE("delta layers round-trip through their compressed form and stay small when sparse",
          "[world][deltas]") {
    std::vector<float> offsets = zeros();
    offsets[1000] = 1.25F;
    offsets[200000] = -3.5F;
    const std::vector<std::uint8_t> packed = compress_offsets(offsets);
    CHECK(packed.size() < 2000); // 1 MB of mostly zeros
    CHECK(decompress_offsets(packed) == offsets);

    const TileDelta layer(offsets);
    CHECK(layer.nonzero_count() == 2);
    REQUIRE(layer.dirty().has_value());
    // The box around sample (487, 1) and sample (443, 389).
    CHECK(*layer.dirty() == SampleRect{443, 1, 487, 389});
}

TEST_CASE("TerrainDeltas refuses bad input and hides negative zero", "[world][deltas]") {
    TerrainDeltas deltas;
    CHECK_THROWS_AS(deltas.set_offset(k_a, 513, 0, 1.0F), std::invalid_argument);
    CHECK_THROWS_AS(deltas.set_offset(k_a, 0, 0, std::numeric_limits<float>::quiet_NaN()),
                    std::invalid_argument);
    deltas.set_offset(k_a, 4, 4, -0.0F); // no layer for a zero
    CHECK(deltas.empty());
    deltas.set_offset(k_a, 4, 4, 1.0F);
    deltas.set_offset(k_a, 4, 4, -0.0F);
    CHECK(std::signbit(deltas.find(k_a)->offset(4, 4)) == false);
    CHECK(deltas.find(k_a)->nonzero_count() == 0);
    deltas.prune(k_a);
    CHECK(deltas.empty());

    std::vector<SavedTileDelta> saved;
    saved.push_back(SavedTileDelta{k_east, zeros()});
    CHECK_THROWS_AS(TerrainDeltas::from_saved(saved), std::invalid_argument); // an empty layer
    std::vector<float> one = zeros();
    one[7] = 1.0F;
    saved = {SavedTileDelta{k_east, one}, SavedTileDelta{k_a, one}};
    CHECK_THROWS_AS(TerrainDeltas::from_saved(saved), std::invalid_argument); // not sorted
    saved = {SavedTileDelta{k_a, std::vector<float>(10, 1.0F)}};
    CHECK_THROWS_AS(TerrainDeltas::from_saved(saved), std::invalid_argument); // wrong size
    one[8] = std::numeric_limits<float>::infinity();
    saved = {SavedTileDelta{k_a, one}};
    CHECK_THROWS_AS(TerrainDeltas::from_saved(saved), std::invalid_argument); // not finite
}
