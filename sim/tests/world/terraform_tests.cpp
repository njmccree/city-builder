#include "citysim/content/content_database.hpp"
#include "citysim/core/commands.hpp"
#include "citysim/core/isystem.hpp"
#include "citysim/core/job_system.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/core/tick_context.hpp"
#include "citysim/core/ui_notice.hpp"
#include "citysim/runner/render_snapshot.hpp"
#include "citysim/save/simulation_io.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/flood_map.hpp"
#include "citysim/world/terraform_commands.hpp"
#include "citysim/world/terraform_events.hpp"
#include "citysim/world/terraform_report.hpp"
#include "citysim/world/terrain_publisher.hpp"
#include "citysim/world/water_commands.hpp"
#include "citysim/world/water_events.hpp"
#include "citysim/world/world_module.hpp"
#include "support/map_content.hpp"
#include "support/synthetic_map.hpp"
#include "support/temp_dir.hpp"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <variant>
#include <vector>

using namespace citysim;
using namespace citysim::world;
using citysim::test::MapContent;
using citysim::test::TempDir;

namespace {

// Remembers the events a test wants to look at.
class Recorder final : public core::ISystem {
  public:
    Recorder(std::vector<TerraformApplied>* terraform, std::vector<WaterChanged>* water)
        : terraform_(terraform), water_(water) {}
    std::string_view name() const override { return "test.terraform_events"; }
    void update(core::TickContext& context) override {
        for (const core::Event& event : context.events.readable()) {
            if (const auto* applied = std::get_if<TerraformApplied>(&event)) {
                terraform_->push_back(*applied);
            } else if (const auto* changed = std::get_if<WaterChanged>(&event)) {
                water_->push_back(*changed);
            }
        }
    }

  private:
    std::vector<TerraformApplied>* terraform_;
    std::vector<WaterChanged>* water_;
};

// A game on the full-size synthetic map with one city whose area holds a stretch of the main river.
struct Game {
    TempDir dir;
    MapContent map;
    std::unique_ptr<content::ContentDatabase> db = std::make_unique<content::ContentDatabase>();
    std::shared_ptr<const LoadedMap> loaded;
    TerrainDefines defines;
    std::unique_ptr<core::Simulation> sim;
    std::vector<TerraformApplied> applied;
    std::vector<WaterChanged> water_events;
    std::uint64_t sequence = 1;
    TileCoord origin_tile;

    explicit Game(const char* label, TerrainDefines terrain_defines = {}, std::size_t workers = 2)
        : dir(label), map(dir, citysim::test::synthetic_map_dir()), defines(terrain_defines) {
        REQUIRE(db->load({map.mod_dir()}).ok());
        loaded = open_map(*db, MapContent::map_id());
        sim = make_simulation(workers);
        const auto& vertices = loaded->rivers().reaches().at(0).vertices;
        const RiverVertex& vertex = *std::min_element(
            vertices.begin(), vertices.end(), [](const RiverVertex& a, const RiverVertex& b) {
                return std::abs(a.z - 45000.0) < std::abs(b.z - 45000.0);
            });
        const TileCoord home = tile_of(WorldPos{vertex.x, vertex.z});
        origin_tile = TileCoord{std::clamp(home.tx - 1, 16, 27), std::clamp(home.ty - 1, 18, 39)};
        run(core::Command{FoundCity{origin_tile.tx, origin_tile.ty, "Test"}});
        REQUIRE(sim->world().ctx().get<ActiveCity>().city_id == 1);
    }

    std::unique_ptr<core::Simulation> make_simulation(std::size_t workers = 2) {
        auto simulation = std::make_unique<core::Simulation>(7, workers);
        WorldConfig config{MapContent::map_id(), loaded};
        config.terrain = defines;
        install(*simulation, *db, config);
        simulation->add_system(core::Phase::Publish, std::make_unique<Recorder>(&applied, &water_events));
        return simulation;
    }

    std::vector<core::UiNotice> run(core::Simulation& simulation, core::Command command) {
        simulation.enqueue({core::SequencedCommand{sequence++, simulation.tick(), std::move(command)}});
        simulation.step();
        return simulation.world().ctx().get<core::UiOutbox>().take();
    }
    std::vector<core::UiNotice> run(core::Command command) { return run(*sim, std::move(command)); }

    WorldTerrain& terrain() { return sim->world().ctx().get<WorldTerrain>(); }
    CityArea area() const { return CityArea{origin_tile}; }
    WorldPos world_of(float x, float z) const { return to_world(area(), LocalPos{x, z}); }
    float height(float x, float z) { return terrain().height_at(world_of(x, z)); }
    std::uint64_t hash() const { return sim->world().state_hash(); }
};

bool has_notice(const std::vector<core::UiNotice>& notices, const std::string& tag,
                const std::string& key = {}) {
    return std::any_of(notices.begin(), notices.end(), [&](const core::UiNotice& notice) {
        return notice.tag == tag && (key.empty() || notice.key == key);
    });
}

Terraform stroke(TerraformOp op, float x, float z, float radius, float strength, float target = 0.0F,
                 std::uint32_t city = 1) {
    return Terraform{city, op, LocalPos{x, z}, radius, strength, target};
}

// A spot in the city's local frame on the 4 m grid and away from the river, in the south-west corner.
constexpr float k_spot_x = 600.0F;
constexpr float k_spot_z = 600.0F;

// Heights of the (2n+1) x (2n+1) samples around a spot, row by row.
std::vector<float> heights_around(Game& game, float x, float z, int n) {
    std::vector<float> out;
    for (int dz = -n; dz <= n; ++dz) {
        for (int dx = -n; dx <= n; ++dx) {
            out.push_back(game.height(x + 4.0F * static_cast<float>(dx), z + 4.0F * static_cast<float>(dz)));
        }
    }
    return out;
}

} // namespace

TEST_CASE("raise and lower move the ground under a soft brush", "[world][terraform][synthetic_map]") {
    Game game("terraform_raise_lower");
    const float before = game.height(k_spot_x, k_spot_z);
    const float far_before = game.height(k_spot_x + 40.0F, k_spot_z);
    const float mid_before = game.height(k_spot_x + 20.0F, k_spot_z);

    CHECK(game.run(core::Command{stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 40.0F, 1.0F)}).size() == 1);
    CHECK(game.height(k_spot_x, k_spot_z) == Catch::Approx(before + 1.0F).margin(1e-4));
    const float mid_gain = game.height(k_spot_x + 20.0F, k_spot_z) - mid_before;
    CHECK(mid_gain > 0.2F);
    CHECK(mid_gain < 0.8F);                                       // softer towards the edge
    CHECK(game.height(k_spot_x + 40.0F, k_spot_z) == far_before); // the edge and beyond stay

    game.run(core::Command{stroke(TerraformOp::Lower, k_spot_x, k_spot_z, 40.0F, 0.5F)});
    CHECK(game.height(k_spot_x, k_spot_z) == Catch::Approx(before + 0.5F).margin(1e-4));
    game.run(core::Command{stroke(TerraformOp::Lower, k_spot_x, k_spot_z, 40.0F, 1.0F)});
    CHECK(game.height(k_spot_x, k_spot_z) == Catch::Approx(before - 0.5F).margin(1e-4));
}

TEST_CASE("flatten pulls towards a height softly and level cuts a flat disc",
          "[world][terraform][synthetic_map]") {
    Game game("terraform_flatten_level");
    const float ground = game.height(k_spot_x, k_spot_z);
    const float target = ground + 6.0F;

    game.run(core::Command{stroke(TerraformOp::Flatten, k_spot_x, k_spot_z, 40.0F, 0.5F, target)});
    CHECK(game.height(k_spot_x, k_spot_z) == Catch::Approx(ground + 3.0F).margin(1e-3)); // halfway there
    game.run(core::Command{stroke(TerraformOp::Flatten, k_spot_x, k_spot_z, 40.0F, 1.0F, target)});
    CHECK(game.height(k_spot_x, k_spot_z) ==
          Catch::Approx(target).margin(1e-3)); // at the middle, all the way

    // Level at full strength: every sample within half the radius is exactly the target.
    const float other_x = k_spot_x + 300.0F;
    const float other_target = game.height(other_x, k_spot_z) - 4.0F;
    game.run(core::Command{stroke(TerraformOp::Level, other_x, k_spot_z, 60.0F, 1.0F, other_target)});
    bool plateau = true;
    for (float dx = -28.0F; dx <= 28.0F; dx += 4.0F) {
        for (float dz = -28.0F; dz <= 28.0F; dz += 4.0F) {
            if (std::hypot(dx, dz) > 30.0F) {
                continue; // beyond half the radius the skirt begins
            }
            plateau = plateau && std::abs(game.height(other_x + dx, k_spot_z + dz) - other_target) < 1e-3F;
        }
    }
    CHECK(plateau);
    CHECK(game.height(other_x + 60.0F, k_spot_z) !=
          Catch::Approx(other_target).margin(1e-3)); // the edge is not
}

TEST_CASE("smooth evens out a bump", "[world][terraform][synthetic_map]") {
    Game game("terraform_smooth");
    for (int i = 0; i < 6; ++i) {
        game.run(core::Command{stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 12.0F, 1.0F)});
    }
    const float peak = game.height(k_spot_x, k_spot_z);
    const float side = game.height(k_spot_x + 12.0F, k_spot_z);
    game.run(core::Command{stroke(TerraformOp::Smooth, k_spot_x, k_spot_z, 24.0F, 1.0F)});
    CHECK(game.height(k_spot_x, k_spot_z) < peak - 0.5F);
    CHECK(std::abs(game.height(k_spot_x, k_spot_z) - game.height(k_spot_x + 12.0F, k_spot_z)) <
          std::abs(peak - side));
}

TEST_CASE("a brush changes only samples within its radius", "[world][terraform][synthetic_map]") {
    Game game("terraform_radius");
    const std::vector<float> before = heights_around(game, k_spot_x, k_spot_z, 20);
    game.run(core::Command{stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 30.0F, 1.0F)});
    const std::vector<float> after = heights_around(game, k_spot_x, k_spot_z, 20);
    bool outside_same = true;
    bool inside_changed = false;
    for (int dz = -20; dz <= 20; ++dz) {
        for (int dx = -20; dx <= 20; ++dx) {
            const auto i = static_cast<std::size_t>((dz + 20) * 41 + (dx + 20));
            const double distance = 4.0 * std::hypot(dx, dz);
            if (distance >= 30.0) {
                outside_same = outside_same && before[i] == after[i];
            } else if (distance < 28.0) {
                inside_changed = inside_changed || before[i] != after[i];
            }
        }
    }
    CHECK(outside_same);
    CHECK(inside_changed);
}

TEST_CASE("a brush is clipped to the city's area and refused outside it",
          "[world][terraform][synthetic_map]") {
    Game game("terraform_jurisdiction");
    // Across the west border: x < 0 is another city's land (or nobody's), x >= 0 is ours.
    const float z = 800.0F;
    const WorldPos out_pos{game.area().origin().x - 8.0, game.area().origin().z + static_cast<double>(z)};
    const WorldPos in_pos{game.area().origin().x + 8.0, game.area().origin().z + static_cast<double>(z)};
    const float out_before = game.terrain().height_at(out_pos);
    const float in_before = game.terrain().height_at(in_pos);
    CHECK(game.run(core::Command{stroke(TerraformOp::Raise, 0.0F, z, 40.0F, 1.0F)}).size() == 1);
    CHECK(game.terrain().height_at(out_pos) == out_before); // outside the area: untouched
    CHECK(game.terrain().height_at(in_pos) > in_before);    // inside: raised

    // Entirely outside: refused, nothing changes.
    const auto notices = game.run(core::Command{stroke(TerraformOp::Raise, -300.0F, z, 100.0F, 1.0F)});
    CHECK(has_notice(notices, "terraform_failed", "terraform.error.outside_city"));
    const auto far_notices =
        game.run(core::Command{stroke(TerraformOp::Raise, 9000.0F, 9000.0F, 100.0F, 1.0F)});
    CHECK(has_notice(far_notices, "terraform_failed", "terraform.error.outside_city"));
    CHECK(game.applied.size() == 1);
}

TEST_CASE("only the active city, and only a sane brush, can be used", "[world][terraform][synthetic_map]") {
    Game game("terraform_refused");
    const auto refused = [&](Terraform command, const char* key) {
        const float before = game.height(k_spot_x, k_spot_z);
        const auto notices = game.run(core::Command{command});
        INFO(key);
        CHECK(has_notice(notices, "terraform_failed", key));
        CHECK(game.height(k_spot_x, k_spot_z) == before);
    };
    refused(stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 20.0F, 1.0F, 0.0F, 7),
            "terraform.error.unknown_city");
    refused(stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 1.0F, 1.0F), "terraform.error.parameters");
    refused(stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 201.0F, 1.0F), "terraform.error.parameters");
    refused(stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 20.0F, 1.5F), "terraform.error.parameters");
    refused(stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 20.0F, -0.1F), "terraform.error.parameters");
    refused(stroke(TerraformOp::Raise, std::numeric_limits<float>::quiet_NaN(), k_spot_z, 20.0F, 1.0F),
            "terraform.error.parameters");
    refused(stroke(TerraformOp::Flatten, k_spot_x, k_spot_z, 20.0F, 1.0F, 1.0e9F),
            "terraform.error.parameters");
    refused(stroke(static_cast<TerraformOp>(9), k_spot_x, k_spot_z, 20.0F, 1.0F),
            "terraform.error.parameters");

    // A second city that is not the active one.
    const TileCoord second{game.origin_tile.tx >= 22 ? game.origin_tile.tx - 4 : game.origin_tile.tx + 4,
                           game.origin_tile.ty};
    game.run(core::Command{FoundCity{second.tx, second.ty, "Other"}});
    REQUIRE(game.sim->world().ctx().get<CityRegistry>().size() == 2);
    refused(stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 20.0F, 1.0F, 0.0F, 2),
            "terraform.error.not_active");
    CHECK(game.applied.empty());
}

TEST_CASE("the ground stays within -50 and +200 m of its base", "[world][terraform][synthetic_map]") {
    TerrainDefines strong;
    strong.terraform_step_m = 50.0F; // a full stroke moves 50 m, so a few strokes hit the limits
    Game game("terraform_limits", strong);
    const float base = game.height(k_spot_x, k_spot_z);
    for (int i = 0; i < 8; ++i) {
        game.run(core::Command{stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 30.0F, 1.0F)});
    }
    CHECK(game.height(k_spot_x, k_spot_z) == Catch::Approx(base + 200.0F).margin(1e-3));
    for (int i = 0; i < 8; ++i) {
        game.run(core::Command{stroke(TerraformOp::Lower, k_spot_x, k_spot_z, 30.0F, 1.0F)});
    }
    // 8 x 50 m down from +200 would be -200: held at -50 from the base.
    CHECK(game.height(k_spot_x, k_spot_z) == Catch::Approx(base - 50.0F).margin(1e-3));
    bool within = true;
    for (const auto& [tile, layer] : game.terrain().deltas().tiles()) {
        for (const float offset : layer.offsets()) {
            within = within && offset >= -50.0F - 1e-3F && offset <= 200.0F + 1e-3F;
        }
    }
    CHECK(within);
}

TEST_CASE("the rivers stay fixed", "[world][terraform][synthetic_map]") {
    TerrainDefines strong;
    strong.terraform_step_m = 50.0F;
    Game game("terraform_rivers", strong);
    // A vertex of the main reach inside the city's area.
    const RiverNetwork& rivers = game.loaded->rivers();
    const WorldPos origin = game.area().origin();
    const RiverVertex* vertex = nullptr;
    for (const RiverVertex& candidate : rivers.reaches()[0].vertices) {
        if (candidate.x > origin.x + 800.0 && candidate.x < origin.x + 7000.0 &&
            candidate.z > origin.z + 800.0 && candidate.z < origin.z + 7000.0) {
            vertex = &candidate;
            break;
        }
    }
    REQUIRE(vertex != nullptr);
    // The brush sits on the channel's east edge, so it covers some channel, its bank and the land beyond.
    const WorldPos center_world{vertex->x + 0.5 * static_cast<double>(vertex->width_m), vertex->z};
    const LocalPos local = to_local(game.area(), center_world);

    // Samples around it, with their distance to the nearest centerline and the bed there.
    struct Spot {
        WorldPos pos;
        float before = 0.0F;
        float distance = 0.0F;
        float half_width = 0.0F;
        float bed = 0.0F;
    };
    std::vector<Spot> spots;
    const double x0 = std::floor(center_world.x / 4.0) * 4.0;
    const double z0 = std::floor(center_world.z / 4.0) * 4.0;
    for (int dz = -55; dz <= 55; ++dz) {
        for (int dx = -55; dx <= 55; ++dx) {
            Spot spot;
            spot.pos = WorldPos{x0 + 4.0 * dx, z0 + 4.0 * dz};
            spot.before = game.terrain().height_at(spot.pos);
            spot.distance = 1.0e9F;
            rivers.for_each_near(spot.pos, 700.0, [&](const ChannelSample& channel) {
                if (channel.distance_m < spot.distance) {
                    spot.distance = channel.distance_m;
                    spot.half_width = channel.half_width_m;
                    spot.bed = channel.level_m - channel.depth_m;
                }
            });
            spots.push_back(spot);
        }
    }
    game.run(core::Command{stroke(TerraformOp::Lower, local.x, local.z, 200.0F, 1.0F)});
    game.run(core::Command{stroke(TerraformOp::Lower, local.x, local.z, 200.0F, 1.0F)});

    std::size_t channel_samples = 0;
    std::size_t bank_samples = 0;
    std::size_t lowered = 0;
    bool channel_fixed = true;
    bool above_bed = true;
    for (const Spot& spot : spots) {
        const float after = game.terrain().height_at(spot.pos);
        if (spot.distance <= spot.half_width) {
            ++channel_samples;
            channel_fixed = channel_fixed && after == spot.before;
        } else {
            lowered += after < spot.before ? 1 : 0;
            if (spot.distance <= spot.half_width + 24.0F) {
                ++bank_samples;
                above_bed = above_bed && after >= spot.bed - 1e-3F;
            }
        }
    }
    CHECK(channel_samples > 100); // the test really covers the channel
    CHECK(bank_samples > 100);
    CHECK(lowered > 100); // and the land beside it did get cut
    CHECK(channel_fixed);
    CHECK(above_bed);
}

TEST_CASE("the cost follows the volume moved and is reported", "[world][terraform][synthetic_map]") {
    Game game("terraform_cost");
    // The volume, found independently: the sum of |change| x 16 over the samples around the brush.
    const std::vector<float> before = heights_around(game, k_spot_x, k_spot_z, 25);
    game.run(core::Command{stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 60.0F, 0.8F)});
    const std::vector<float> after = heights_around(game, k_spot_x, k_spot_z, 25);
    double volume = 0.0;
    for (std::size_t i = 0; i < before.size(); ++i) {
        volume += std::abs(static_cast<double>(after[i]) - static_cast<double>(before[i])) * 16.0;
    }
    REQUIRE(game.applied.size() == 1);
    CHECK(game.applied[0].city == 1);
    CHECK(game.applied[0].volume_m3 == Catch::Approx(volume).epsilon(1e-4));
    CHECK(volume > 1000.0);
    CHECK(game.applied[0].cost == Catch::Approx(volume * 0.1).epsilon(1e-4)); // the default 0.1 per m3

    const auto& report = game.sim->world().ctx().get<TerraformReport>();
    CHECK(report.serial == 1);
    CHECK(report.cost == game.applied[0].cost);
    game.run(core::Command{stroke(TerraformOp::Lower, k_spot_x, k_spot_z, 60.0F, 0.8F)});
    CHECK(report.serial == 2);
    CHECK(report.total_cost == Catch::Approx(game.applied[0].cost + game.applied[1].cost));

    // A stroke that moves nothing costs nothing and says nothing.
    const auto quiet = game.run(core::Command{stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 60.0F, 0.0F)});
    CHECK(quiet.empty());
    CHECK(game.applied.size() == 2);

    // Published for the UI.
    runner::RenderSnapshot snapshot;
    publish_terraform_report(*game.sim, snapshot);
    const runner::SnapshotChannel* channel = snapshot.find_channel("world.terraform.last");
    REQUIRE(channel != nullptr);
    REQUIRE(channel->data.size() == 5);
    CHECK(channel->data[0] == 2.0F);
    CHECK(channel->data[1] == 1.0F);
}

TEST_CASE("the same strokes give the same ground, whatever the worker count",
          "[world][terraform][synthetic_map]") {
    const auto play = [](const char* label, std::size_t workers) {
        Game game(label, {}, workers);
        game.run(core::Command{stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 90.0F, 0.7F)});
        game.run(core::Command{stroke(TerraformOp::Smooth, k_spot_x + 20.0F, k_spot_z, 50.0F, 1.0F)});
        game.run(core::Command{stroke(TerraformOp::Level, 1500.0F, 1500.0F, 120.0F, 0.9F, 30.0F)});
        game.run(
            core::Command{stroke(TerraformOp::Lower, 0.0F, 0.0F, 80.0F, 1.0F)}); // on the corner of the area
        return game.hash();
    };
    const std::uint64_t one = play("terraform_det_1", 1);
    CHECK(play("terraform_det_8", 8) == one);
    CHECK(play("terraform_det_2", 2) == one);
}

TEST_CASE("terraforming survives a save and a load", "[world][terraform][synthetic_map]") {
    Game game("terraform_save_load");
    game.run(core::Command{stroke(TerraformOp::Raise, k_spot_x, k_spot_z, 100.0F, 1.0F)});
    game.run(core::Command{stroke(TerraformOp::Level, 1200.0F, 900.0F, 150.0F, 1.0F, 25.0F)});
    const std::vector<float> ground = heights_around(game, k_spot_x, k_spot_z, 30);

    save::SaveOptions save_options;
    save_options.content = game.db.get();
    const std::vector<std::byte> bytes = save::save_simulation_bytes(*game.sim, save_options);
    auto loaded = game.make_simulation();
    save::LoadOptions load_options;
    load_options.content = game.db.get();
    const save::LoadOutcome outcome = save::load_simulation_bytes(*loaded, bytes, load_options);
    CHECK(outcome.warnings.empty());
    CHECK(loaded->world().state_hash() == game.hash());
    CHECK(loaded->world().ctx().get<WorldTerrain>().deltas() == game.terrain().deltas());
    bool same = true;
    std::size_t i = 0;
    for (int dz = -30; dz <= 30; ++dz) {
        for (int dx = -30; dx <= 30; ++dx) {
            same = same && loaded->world().ctx().get<WorldTerrain>().height_at(
                               game.world_of(k_spot_x + 4.0F * static_cast<float>(dx),
                                             k_spot_z + 4.0F * static_cast<float>(dz))) == ground[i++];
        }
    }
    CHECK(same);
    // And the game goes on the same way from there.
    const core::Command next{stroke(TerraformOp::Smooth, k_spot_x, k_spot_z, 70.0F, 1.0F)};
    game.run(*game.sim, next);
    game.run(*loaded, next);
    CHECK(loaded->world().state_hash() == game.hash());
}

TEST_CASE("an edit makes only the floods it touches start again",
          "[world][terraform][flood][synthetic_map]") {
    const auto map = LoadedMap::open(citysim::test::synthetic_map_dir());
    WorldTerrain terrain{map, 1};
    core::JobSystem jobs{2};
    WaterState water{map->rivers().reach_count()};
    const auto& vertices = map->rivers().reaches()[0].vertices;
    const RiverVertex& vertex =
        *std::min_element(vertices.begin(), vertices.end(), [](const RiverVertex& a, const RiverVertex& b) {
            return std::abs(a.z - 45000.0) < std::abs(b.z - 45000.0);
        });
    const TileCoord home = tile_of(WorldPos{vertex.x, vertex.z});
    std::vector<TileCoord> tiles;
    for (std::int32_t dy = -1; dy <= 1; ++dy) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            tiles.push_back(TileCoord{home.tx + dx, home.ty + dy});
        }
    }
    std::sort(tiles.begin(), tiles.end());
    terrain.activate_tiles(jobs, tiles);

    // Reach 0 floods (the main channel), reach 1 does not (still at its base level).
    water.set_level_offset(0, 5.0F);
    FloodMap flood;
    int calls = 0;
    do {
        flood.advance(terrain, water, 100000000);
    } while (!flood.settled() && ++calls < 1000);
    REQUIRE(flood.settled());
    const double area = flood.flooded_area_m2(0);
    REQUIRE(area > 0.0);
    const auto grids = flood.grids();

    // An edit far from every floodplain (a tile the floodplain box of reach 0 does not reach, if there is
    // one).
    const Reach& main = map->rivers().reaches()[0];
    double min_x = 1e300;
    double max_x = -1e300;
    for (const WorldPos& point : main.floodplain[0]) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
    }
    TileCoord far_tile{};
    bool found_far = false;
    for (const TileCoord tile : tiles) {
        const WorldPos o = tile_origin(tile);
        if (o.x > max_x || o.x + k_tile_size_m < min_x) {
            far_tile = tile;
            found_far = true;
        }
    }
    if (found_far) {
        terrain.apply_delta(far_tile, SampleRect{10, 10, 40, 40},
                            [](std::uint32_t, std::uint32_t, float height) { return height + 3.0F; });
        CHECK(flood.advance(terrain, water, 100000000).empty()); // nothing to redo
        CHECK(flood.settled());
        CHECK(flood.grids() == grids);
    } else {
        WARN("every active tile touches the floodplain box of reach 0; the far-edit case is not covered");
    }

    // An edit in the floodplain: the whole tile raised 40 m puts it above the water, so the reach refills and
    // floods less.
    terrain.apply_delta(home, SampleRect::whole_tile(),
                        [](std::uint32_t, std::uint32_t, float height) { return height + 40.0F; });
    std::vector<FloodMap::Finished> done;
    calls = 0;
    do {
        const auto finished = flood.advance(terrain, water, 100000000);
        done.insert(done.end(), finished.begin(), finished.end());
    } while (!flood.settled() && ++calls < 1000);
    REQUIRE(done.size() == 1);
    CHECK(done[0].reach == 0);
    CHECK(done[0].flooded_area_m2 < area);
}

// Hidden by default: run with `citysim_tests "[.bench]"`. The biggest brush (200 m radius, about 10,000
// samples) must take under 20 ms in a Release build, so a held mouse button at 15 strokes per second costs
// well under a third of a second per second.
TEST_CASE("the biggest brush is fast", "[.bench][synthetic_map]") {
    Game game("terraform_bench");
    constexpr int runs = 10;
    double best_ms = 1e9;
    double total_ms = 0.0;
    for (int i = 0; i < runs; ++i) {
        const auto start = std::chrono::steady_clock::now();
        game.run(core::Command{stroke(TerraformOp::Smooth, 3000.0F, 3000.0F, 200.0F, 0.5F)});
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        best_ms = std::min(best_ms, ms);
        total_ms += ms;
    }
    std::printf("terraform brush: best %.2f ms, mean %.2f ms over %d runs\n", best_ms, total_ms / runs, runs);
#ifdef NDEBUG
    CHECK(best_ms < 20.0);
#endif
}
