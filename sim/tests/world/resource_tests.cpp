#include "citysim/content/content_database.hpp"
#include "citysim/content/resource_def.hpp"
#include "citysim/core/job_system.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/runner/render_snapshot.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/world/resource_fields.hpp"
#include "citysim/world/terrain_publisher.hpp"
#include "citysim/world/world_module.hpp"
#include "support/map_content.hpp"
#include "support/synthetic_map.hpp"
#include "support/temp_dir.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace citysim;
using namespace citysim::world;
using citysim::content::ResourceDef;
using citysim::test::MapContent;
using citysim::test::TempDir;

namespace {

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

const char* k_defs = R"({ "defs": [
  { "type": "resource", "id": "test:resource.ore_copper", "name_key": "resource.ore_copper.name",
    "desc_key": "resource.ore_copper.desc", "kind": "ore", "ramp": ["#2e1f14", "#b87333", "#f2c58a"] },
  { "type": "resource", "id": "test:resource.stone_limestone", "name_key": "resource.stone_limestone.name",
    "desc_key": "resource.stone_limestone.desc", "kind": "stone", "ramp": ["#555555", "#cccccc"] },
  { "type": "resource", "id": "test:resource.fertile_soil", "name_key": "resource.fertile_soil.name",
    "desc_key": "resource.fertile_soil.desc", "kind": "soil", "layer": "fertility",
    "ramp": ["#3d2b1f", "#6b8e23", "#c8e6a0"], "renewable": "silt", "renewal_per_year": 0.5 }
] })";

const char* k_loc = R"({
  "resource.ore_copper.name": "Copper ore", "resource.ore_copper.desc": "Green-stained rock.",
  "resource.stone_limestone.name": "Limestone", "resource.stone_limestone.desc": "Pale building stone.",
  "resource.fertile_soil.name": "Fertile soil", "resource.fertile_soil.desc": "Dark river silt."
})";

// A world game on the full synthetic map with three resource definitions in its test mod. `with_copper`
// false leaves the copper definition out (for the "content changed" case).
struct Setup {
    TempDir dir;
    MapContent map;
    std::unique_ptr<content::ContentDatabase> db = std::make_unique<content::ContentDatabase>();
    std::shared_ptr<const LoadedMap> loaded;
    std::unique_ptr<core::Simulation> sim;

    explicit Setup(const char* label, bool with_copper = true)
        : dir(label), map(dir, citysim::test::synthetic_map_dir()) {
        std::string defs = k_defs;
        if (!with_copper) {
            const std::size_t a =
                defs.find("  { \"type\": \"resource\", \"id\": \"test:resource.ore_copper\"");
            const std::size_t b =
                defs.find("  { \"type\": \"resource\", \"id\": \"test:resource.stone_limestone\"");
            defs.erase(a, b - a);
        }
        write_text(map.mod_dir() / "defs" / "resources.json", defs);
        write_text(map.mod_dir() / "loc" / "en" / "resources.json", k_loc);
        const auto result = db->load({map.mod_dir()});
        std::string errors;
        for (const auto& issue : result.errors) {
            errors += issue.format() + "\n";
        }
        INFO(errors);
        REQUIRE(result.errors.empty());
        loaded = open_map(*db, MapContent::map_id());
        sim = make_simulation(2);
    }

    std::unique_ptr<core::Simulation> make_simulation(std::size_t workers) const {
        auto simulation = std::make_unique<core::Simulation>(7, workers);
        install(*simulation, *db, WorldConfig{MapContent::map_id(), loaded});
        return simulation;
    }
    ResourceFields& fields() { return sim->world().ctx().get<ResourceFields>(); }
    ResourceFields& fields_of(core::Simulation& other) { return other.world().ctx().get<ResourceFields>(); }
};

// The region cell of a layer with the largest value, as a world position (the centre of the cell).
WorldPos richest_spot(const LoadedMap& map, const char* layer) {
    const Raster<float>* raster = map.region_layer(layer);
    REQUIRE(raster != nullptr);
    float best = -1.0F;
    WorldPos where;
    const double cell = map.manifest().region.cell_size_m;
    for (std::uint32_t y = 0; y < raster->height(); ++y) {
        for (std::uint32_t x = 0; x < raster->width(); ++x) {
            if (raster->value(x, y) > best) {
                best = raster->value(x, y);
                where =
                    WorldPos{(static_cast<double>(x) + 0.5) * cell, (static_cast<double>(y) + 0.5) * cell};
            }
        }
    }
    REQUIRE(best > 0.0F);
    return where;
}

// A city area that has the position inside it, a few hundred meters from its south-west corner at least.
CityArea area_around(WorldPos pos) {
    const TileCoord tile = tile_of(pos);
    return CityArea{TileCoord{tile.tx - 1, tile.ty - 1}};
}

std::uint64_t total(ResourceFields& fields, FieldId field, const CityArea& city, LocalPos center,
                    float radius) {
    // The sum of the current values of the samples a brush covers inside the area, by sampling the field.
    std::uint64_t sum = 0;
    for (float dz = -radius; dz <= radius; dz += 4.0F) {
        for (float dx = -radius; dx <= radius; dx += 4.0F) {
            if (dx * dx + dz * dz < radius * radius) {
                sum += fields.sample_local(field, city, LocalPos{center.x + dx, center.z + dz});
            }
        }
    }
    return sum;
}

} // namespace

TEST_CASE("the fields are fertility, vegetation, wetness and one per resource layer",
          "[world][resources][synthetic_map]") {
    Setup setup("resources_fields");
    const auto& fields = setup.fields().fields();
    std::vector<std::string> names;
    for (const FieldInfo& field : fields) {
        names.push_back(field.name);
    }
    CHECK(names ==
          std::vector<std::string>{"fertility", "ore_copper", "stone_limestone", "vegetation", "wetness"});
    // A resource id finds its field, and two names can lead to one field.
    CHECK((setup.fields().find("ore_copper") == setup.fields().find("test:resource.ore_copper")));
    CHECK((setup.fields().find("fertility") == setup.fields().find("test:resource.fertile_soil")));
    CHECK(!setup.fields().find("test:resource.nothing").has_value());
    CHECK(!setup.fields().find("granite").has_value());
}

TEST_CASE("the base follows the region layer with seeded detail on top",
          "[world][resources][synthetic_map]") {
    Setup setup("resources_base");
    const FieldId copper = *setup.fields().find("ore_copper");
    const Raster<float>& layer = *setup.loaded->region_layer("ore_copper");
    const WorldPos rich = richest_spot(*setup.loaded, "ore_copper");

    // Within 30 percent of the region value (the detail amplitude), around the richest place.
    std::size_t inside = 0;
    bool bounded = true;
    bool varied = false;
    std::uint8_t first = setup.fields().base_sample(copper, rich);
    for (double dz = -600.0; dz <= 600.0; dz += 37.0) {
        for (double dx = -600.0; dx <= 600.0; dx += 37.0) {
            const WorldPos pos{rich.x + dx, rich.z + dz};
            const double region = static_cast<double>(setup.loaded->region_value(layer, pos)) * 255.0;
            const double value = static_cast<double>(setup.fields().base_sample(copper, pos));
            bounded = bounded && std::abs(value - region) <= 0.3 * region + 1.5;
            varied = varied || setup.fields().base_sample(copper, pos) != first;
            inside += value > 0.0 ? 1U : 0U;
        }
    }
    CHECK(bounded);
    CHECK(varied); // the detail is not flat
    CHECK(inside > 100);
    // Far from any deposit it is 0, and so is a place off the map.
    CHECK(setup.fields().base_sample(copper, WorldPos{3000.0, 3000.0}) == 0);
    CHECK(setup.fields().base_sample(copper, WorldPos{-5000.0, 100.0}) == 0);

    // A second instance, and another map seed, and worker counts: same base, except for the seed.
    Setup other("resources_base_other");
    CHECK(other.fields().base_sample(*other.fields().find("ore_copper"), rich) == first);
    ResourceFields reseeded(setup.loaded, 99, setup.fields().fields());
    bool differs = false;
    for (double dx = 0.0; dx < 400.0 && !differs; dx += 4.0) {
        differs = reseeded.base_sample(copper, WorldPos{rich.x + dx, rich.z}) !=
                  setup.fields().base_sample(copper, WorldPos{rich.x + dx, rich.z});
    }
    CHECK(differs);
}

TEST_CASE(
    "a tile's grid is the same numbers as the samples, and prepare gives the same grids with any workers",
    "[world][resources][synthetic_map]") {
    Setup setup("resources_grid");
    const FieldId copper = *setup.fields().find("ore_copper");
    const FieldId fertility = *setup.fields().find("fertility");
    const WorldPos rich = richest_spot(*setup.loaded, "ore_copper");
    const TileCoord tile = tile_of(rich);

    const auto grid = setup.fields().grid(copper, tile);
    REQUIRE(grid != nullptr);
    REQUIRE(grid->size() == ResourceFields::k_cells_per_tile);
    const WorldPos origin = tile_origin(tile);
    bool same = true;
    for (std::uint32_t sy = 0; sy < 512; sy += 29) {
        for (std::uint32_t sx = 0; sx < 512; sx += 31) {
            const WorldPos pos{origin.x + 4.0 * sx, origin.z + 4.0 * sy};
            same = same &&
                   (*grid)[static_cast<std::size_t>(sy) * 512 + sx] == setup.fields().sample(copper, pos);
        }
    }
    CHECK(same);
    CHECK(setup.fields().grid(copper, tile) == grid); // the same pointer until something changes
    CHECK(setup.fields().grid(copper, TileCoord{0, 0}) == nullptr); // nothing there

    const std::vector<TileCoord> tiles{tile, TileCoord{tile.tx + 1, tile.ty},
                                       TileCoord{tile.tx, tile.ty + 1}};
    ResourceFields one(setup.loaded, 5, setup.fields().fields());
    ResourceFields eight(setup.loaded, 5, setup.fields().fields());
    core::JobSystem serial(1);
    core::JobSystem parallel(8);
    one.prepare(serial, tiles);
    eight.prepare(parallel, tiles);
    for (const TileCoord t : tiles) {
        for (const FieldId field : {copper, fertility}) {
            const auto a = one.grid(field, t);
            const auto b = eight.grid(field, t);
            REQUIRE(((a == nullptr) == (b == nullptr)));
            if (a != nullptr) {
                CHECK(*a == *b);
            }
        }
    }
}

TEST_CASE("extraction removes what is there, in a fixed order, and never goes below nothing",
          "[world][resources][synthetic_map]") {
    Setup setup("resources_extract");
    ResourceFields& fields = setup.fields();
    const FieldId copper = *fields.find("ore_copper");
    const WorldPos rich = richest_spot(*setup.loaded, "ore_copper");
    const CityArea city = area_around(rich);
    const LocalPos center = to_local(city, rich);
    const float radius = 60.0F;

    const std::uint64_t before = total(fields, copper, city, center, radius + 8.0F);
    REQUIRE(before > 1000);
    // Asking for a little takes it from the first samples (south row first, west to east): the sample at the
    // south-west end of the brush is emptied before one at the far end is touched.
    const std::uint8_t first_before = fields.sample_local(copper, city, LocalPos{center.x, center.z - 56.0F});
    const std::uint8_t last_before = fields.sample_local(copper, city, LocalPos{center.x, center.z + 56.0F});
    const auto small = fields.extract(copper, city, center, radius, 300);
    CHECK(small.amount == 300);
    CHECK(small.samples >= 1);
    CHECK(total(fields, copper, city, center, radius + 8.0F) == before - 300);
    CHECK(fields.sample_local(copper, city, LocalPos{center.x, center.z - 56.0F}) <= first_before);
    CHECK(fields.sample_local(copper, city, LocalPos{center.x, center.z + 56.0F}) == last_before);

    // Asking for more than there is gives back what there was, and leaves nothing in the brush.
    const std::uint64_t left = total(fields, copper, city, center, radius);
    const auto all = fields.extract(copper, city, center, radius, 10'000'000);
    CHECK(all.amount <= left + 255 * 4);
    CHECK(all.amount > 0);
    CHECK(total(fields, copper, city, center, radius - 4.0F) == 0);
    CHECK(fields.extract(copper, city, center, radius, 100).amount == 0); // depletion persists
    // The base is what it was: only the delta changed.
    CHECK(fields.base_sample(copper, rich) > 0);
    CHECK(fields.sample(copper, rich) == 0);
}

TEST_CASE("extraction stays inside the city's area", "[world][resources][synthetic_map]") {
    Setup setup("resources_area");
    ResourceFields& fields = setup.fields();
    const FieldId copper = *fields.find("ore_copper");
    const WorldPos rich = richest_spot(*setup.loaded, "ore_copper");
    // An area whose west edge passes through the deposit's middle: the part to the west is another city's.
    const TileCoord tile = tile_of(rich);
    const CityArea city{TileCoord{tile.tx, tile.ty - 1}};
    const WorldPos edge{city.origin().x - 40.0, rich.z};
    const WorldPos inside{city.origin().x + 40.0, rich.z};
    const std::uint8_t outside_before = fields.sample(copper, edge);
    const std::uint8_t inside_before = fields.sample(copper, inside);

    const LocalPos center = to_local(city, WorldPos{city.origin().x, rich.z});
    fields.extract(copper, city, center, 120.0F, 10'000'000);
    CHECK(fields.sample(copper, edge) == outside_before);
    CHECK((inside_before == 0 || fields.sample(copper, inside) == 0));
}

TEST_CASE("renewal puts back what was taken, no more than the land had",
          "[world][resources][synthetic_map]") {
    Setup setup("resources_renew");
    ResourceFields& fields = setup.fields();
    const FieldId soil = *fields.find("test:resource.fertile_soil"); // the fertility layer
    const WorldPos spot = richest_spot(*setup.loaded, "fertility");
    const CityArea city = area_around(spot);
    const LocalPos center = to_local(city, spot);
    const float radius = 80.0F;

    const std::uint64_t before = total(fields, soil, city, center, radius);
    const auto taken = fields.extract(soil, city, center, radius, 4000);
    REQUIRE(taken.amount == 4000);
    CHECK(total(fields, soil, city, center, radius) == before - 4000);

    const auto half = fields.renew(soil, city, center, radius, 1500);
    CHECK(half.amount == 1500);
    CHECK(total(fields, soil, city, center, radius) == before - 2500);
    // More than was taken: it stops at the base.
    const auto rest = fields.renew(soil, city, center, radius, 100'000);
    CHECK(rest.amount == 2500);
    CHECK(total(fields, soil, city, center, radius) == before);
    CHECK(!fields.has_deltas()); // back where it started: no change left to keep
    CHECK(fields.renew(soil, city, center, radius, 100).amount == 0);

    // Silt that enriches can go past the base, up to 255.
    const auto rich = fields.renew(soil, city, center, 20.0F, 100'000, true);
    CHECK(rich.amount > 0);
    CHECK(fields.sample_local(soil, city, center) == 255);
}

TEST_CASE("add_delta changes one sample and says by how much", "[world][resources][synthetic_map]") {
    Setup setup("resources_delta");
    ResourceFields& fields = setup.fields();
    const FieldId copper = *fields.find("ore_copper");
    const WorldPos rich = richest_spot(*setup.loaded, "ore_copper");
    const auto gx = static_cast<std::int64_t>(std::floor(rich.x / 4.0 + 0.5));
    const auto gz = static_cast<std::int64_t>(std::floor(rich.z / 4.0 + 0.5));
    const int base = fields.base_sample(copper, rich);
    REQUIRE(base > 20);
    CHECK(fields.add_delta(copper, gx, gz, -10) == -10);
    CHECK(fields.sample(copper, rich) == base - 10);
    CHECK(fields.add_delta(copper, gx, gz, -1000) == -(base - 10));
    CHECK(fields.sample(copper, rich) == 0);
    CHECK(fields.add_delta(copper, gx, gz, 7) == 7);
    CHECK(fields.add_delta(copper, gx, gz, 1000) == base - 7); // up to the base, not past it
    CHECK(fields.add_delta(copper, gx, gz, 1000) == 0);
    CHECK_THROWS_AS(fields.add_delta(99, gx, gz, 1), std::out_of_range);
}

TEST_CASE("only the changes are state: hashed, saved, and the same after a load",
          "[world][resources][synthetic_map]") {
    Setup setup("resources_save");
    ResourceFields& fields = setup.fields();
    const FieldId copper = *fields.find("ore_copper");
    const FieldId limestone = *fields.find("stone_limestone");
    const WorldPos rich = richest_spot(*setup.loaded, "ore_copper");
    const CityArea city = area_around(rich);
    const LocalPos center = to_local(city, rich);

    // Reading, making bases and grids is not a change of state.
    const std::uint64_t untouched = setup.sim->world().state_hash();
    core::JobSystem jobs(2);
    fields.prepare(jobs, {tile_of(rich)});
    static_cast<void>(fields.grid(copper, tile_of(rich)));
    static_cast<void>(fields.sample(limestone, rich));
    CHECK(setup.sim->world().state_hash() == untouched);

    fields.extract(copper, city, center, 50.0F, 3000);
    const std::uint64_t changed = setup.sim->world().state_hash();
    CHECK(changed != untouched);

    save::SaveOptions save_options;
    save_options.content = setup.db.get();
    const std::vector<std::byte> bytes = save::save_simulation_bytes(*setup.sim, save_options);
    auto loaded = setup.make_simulation(2);
    save::LoadOptions load_options;
    load_options.content = setup.db.get();
    const save::LoadOutcome outcome = save::load_simulation_bytes(*loaded, bytes, load_options);
    CHECK(outcome.warnings.empty());
    CHECK(loaded->world().state_hash() == changed);
    CHECK(setup.fields_of(*loaded) == fields);
    CHECK(setup.fields_of(*loaded).sample(copper, rich) == fields.sample(copper, rich));
    // The same extraction afterwards gives the same game.
    fields.extract(copper, city, center, 50.0F, 500);
    setup.fields_of(*loaded).extract(copper, city, center, 50.0F, 500);
    CHECK(loaded->world().state_hash() == setup.sim->world().state_hash());
}

TEST_CASE("changes to a resource the content no longer has are dropped with a warning",
          "[world][resources][synthetic_map]") {
    Setup with("resources_with");
    const FieldId copper = *with.fields().find("ore_copper");
    const WorldPos rich = richest_spot(*with.loaded, "ore_copper");
    const CityArea city = area_around(rich);
    with.fields().extract(copper, city, to_local(city, rich), 40.0F, 500);
    save::SaveOptions save_options;
    save_options.content = with.db.get();
    const std::vector<std::byte> bytes = save::save_simulation_bytes(*with.sim, save_options);

    Setup without("resources_without", false);
    REQUIRE(!without.fields().find("test:resource.ore_copper").has_value());
    auto loaded = without.make_simulation(2);
    save::LoadOptions load_options;
    load_options.content = without.db.get();
    const save::LoadOutcome outcome = save::load_simulation_bytes(*loaded, bytes, load_options);
    const bool warned =
        std::any_of(outcome.warnings.begin(), outcome.warnings.end(),
                    [](const std::string& text) { return text.find("ore_copper") != std::string::npos; });
    CHECK(warned);
    CHECK(!without.fields_of(*loaded).has_deltas());
}

TEST_CASE("the fields are published as shared grids that change only with the land",
          "[world][resources][synthetic_map]") {
    Setup setup("resources_publish");
    ResourceFields& fields = setup.fields();
    const FieldId copper = *fields.find("ore_copper");
    const WorldPos rich = richest_spot(*setup.loaded, "ore_copper");
    const TileCoord tile = tile_of(rich);
    const CityArea city = area_around(rich);

    core::JobSystem jobs(2);
    auto& terrain = setup.sim->world().ctx().get<WorldTerrain>();
    terrain.activate_tiles(jobs, std::vector<TileCoord>{tile});

    runner::RenderSnapshot first;
    publish_resource_grids(*setup.sim, first);
    const std::string name = "resource.ore_copper." + std::to_string(tile.tx) + "_" + std::to_string(tile.ty);
    const runner::SnapshotGrid* grid = first.find_grid(name);
    REQUIRE(grid != nullptr);
    CHECK(grid->width == 512);
    CHECK(grid->height == 512);
    CHECK(grid->cell_size == 4.0F);
    CHECK(grid->cells->size() == ResourceFields::k_cells_per_tile);
    const std::string fertility =
        "resource.fertility." + std::to_string(tile.tx) + "_" + std::to_string(tile.ty);
    REQUIRE(first.find_grid(fertility) != nullptr);

    runner::RenderSnapshot second;
    publish_resource_grids(*setup.sim, second);
    CHECK(second.find_grid(name)->cells == grid->cells); // nothing changed: same pointer

    fields.extract(copper, city, to_local(city, rich), 40.0F, 800);
    runner::RenderSnapshot third;
    publish_resource_grids(*setup.sim, third);
    REQUIRE(third.find_grid(name) != nullptr);
    CHECK(third.find_grid(name)->cells != grid->cells);
    // The other fields of the tile kept their pointers.
    REQUIRE(third.find_grid(fertility) != nullptr);
    CHECK(third.find_grid(fertility)->cells == first.find_grid(fertility)->cells);
}

// Hidden by default: run with `citysim_tests "[.bench]"`. Making the bases of a city's 36 active tiles for
// every field (what happens when a city becomes active) must take under 1.5 s on 4 workers in a Release
// build.
TEST_CASE("making the resource bases of a city is fast enough", "[.bench][synthetic_map]") {
    Setup setup("resources_bench");
    const WorldPos rich = richest_spot(*setup.loaded, "fertility");
    const TileCoord home = tile_of(rich);
    std::vector<TileCoord> tiles;
    for (std::int32_t dy = -3; dy <= 2; ++dy) {
        for (std::int32_t dx = -3; dx <= 2; ++dx) {
            tiles.push_back(TileCoord{home.tx + dx, home.ty + dy});
        }
    }
    std::sort(tiles.begin(), tiles.end());
    core::JobSystem jobs(4);
    ResourceFields fields(setup.loaded, 5, setup.fields().fields());
    const auto start = std::chrono::steady_clock::now();
    fields.prepare(jobs, tiles);
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("resource bases: %zu tiles x %zu fields in %.1f ms on 4 workers\n", tiles.size(),
                fields.fields().size(), ms);
#ifdef NDEBUG
    CHECK(ms < 1500.0);
#endif
}

// ---- the ResourceDef content type ------------------------------------------------------------------------

namespace {

// Loads a mod with one defs file and returns the problems, or loads fine and returns nothing.
std::vector<std::string> load_resources(const char* label, const std::string& defs) {
    TempDir dir(label);
    const auto mod = dir.path() / "mod";
    write_text(mod / "mod.json", R"({ "id": "t", "name_key": "mod.t.name", "version": "1.0.0",
      "enabled_by_default": true })");
    write_text(mod / "defs" / "resources.json", defs);
    write_text(mod / "loc" / "en" / "all.json", R"({ "mod.t.name": "T", "r.name": "R", "r.desc": "R." })");
    content::ContentDatabase db;
    std::vector<std::string> out;
    for (const auto& issue : db.load({mod}).errors) {
        out.push_back(issue.format());
    }
    return out;
}

std::string one(const std::string& fields) {
    return R"({ "defs": [ { "type": "resource", "id": "t:resource.x", "name_key": "r.name", "desc_key": "r.desc",)" +
           fields + " } ] }";
}

bool mentions(const std::vector<std::string>& issues, const std::string& needle) {
    return std::any_of(issues.begin(), issues.end(),
                       [&](const std::string& issue) { return issue.find(needle) != std::string::npos; });
}

} // namespace

TEST_CASE("a resource definition is read and checked", "[content][resources]") {
    // A good one, with the layer taken from the id.
    {
        TempDir dir("resource_def_ok");
        const auto mod = dir.path() / "mod";
        write_text(mod / "mod.json", R"({ "id": "t", "name_key": "mod.t.name", "version": "1.0.0",
          "enabled_by_default": true })");
        write_text(mod / "defs" / "resources.json",
                   one(R"( "kind": "ore", "ramp": ["#000000", "#FFa500", "#ffffff"], "renewable": "silt",
                           "renewal_per_year": 0.25 )"));
        write_text(mod / "loc" / "en" / "all.json",
                   R"({ "mod.t.name": "T", "r.name": "R", "r.desc": "R." })");
        content::ContentDatabase db;
        const auto result = db.load({mod});
        std::string errors;
        for (const auto& issue : result.errors) {
            errors += issue.format() + "\n";
        }
        INFO(errors);
        REQUIRE(result.errors.empty());
        const auto& defs = db.registry<ResourceDef>();
        REQUIRE(defs.size() == 1);
        const ResourceDef& def = defs.all()[0];
        CHECK(def.layer == "x");
        CHECK(def.kind == content::ResourceKind::Ore);
        CHECK(def.renewable == content::ResourceRenewal::Silt);
        CHECK(def.renewal_per_year == 0.25);
        REQUIRE(def.ramp.size() == 3);
        CHECK(def.ramp[1] == std::array<std::uint8_t, 3>{0xFF, 0xA5, 0x00});
    }
    CHECK(mentions(load_resources("rd_kind", one(R"( "kind": "gem", "ramp": ["#000000", "#ffffff"] )")),
                   "/kind"));
    CHECK(mentions(load_resources("rd_ramp_missing", one(R"( "kind": "ore" )")), "ramp"));
    CHECK(mentions(load_resources("rd_ramp_one", one(R"( "kind": "ore", "ramp": ["#000000"] )")), "2 to 8"));
    CHECK(mentions(load_resources("rd_color", one(R"( "kind": "ore", "ramp": ["#000000", "red"] )")),
                   "/ramp/1"));
    CHECK(mentions(load_resources("rd_layer", one(R"( "kind": "ore", "layer": "Ore Copper",
                                                    "ramp": ["#000000", "#ffffff"] )")),
                   "/layer"));
    CHECK(mentions(load_resources("rd_renewable", one(R"( "kind": "ore", "renewable": "magic",
                                                        "ramp": ["#000000", "#ffffff"] )")),
                   "/renewable"));
    CHECK(mentions(load_resources("rd_rate", one(R"( "kind": "ore", "renewal_per_year": 2,
                                                   "ramp": ["#000000", "#ffffff"] )")),
                   "/renewal_per_year"));
}
