#include "citysim/world/terrain_publisher.hpp"

#include "citysim/profiling/profile.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/flood_map.hpp"
#include "citysim/world/resource_fields.hpp"
#include "citysim/world/terraform_report.hpp"
#include "citysim/world/world_terrain.hpp"

namespace citysim::world {

void publish_terrain_tiles(core::Simulation& simulation, runner::RenderSnapshot& snapshot) {
    CS_PROFILE_ZONE("world.publish_terrain");
    const auto* terrain = simulation.world().ctx().find<WorldTerrain>();
    if (terrain == nullptr) {
        return;
    }
    snapshot.terrain_tiles.reserve(terrain->tiles().size());
    for (const auto& [coord, tile] : terrain->tiles()) {
        snapshot.terrain_tiles.push_back(runner::SnapshotTerrainTile{coord, tile->version(), tile});
    }
}

void publish_cities(core::Simulation& simulation, runner::RenderSnapshot& snapshot) {
    const auto* registry = simulation.world().ctx().find<CityRegistry>();
    const auto* active = simulation.world().ctx().find<ActiveCity>();
    if (registry == nullptr || active == nullptr) {
        return;
    }
    snapshot.active_city_id = active->city_id;
    snapshot.cities.reserve(registry->size());
    for (const CityInfo& city : registry->cities()) {
        snapshot.cities.push_back(runner::SnapshotCity{city.id, city.name, city.first_tile, city.founded_on,
                                                       city.id == active->city_id});
    }
}

void publish_flood_extents(core::Simulation& simulation, runner::RenderSnapshot& snapshot) {
    const auto* flood = simulation.world().ctx().find<FloodMap>();
    if (flood == nullptr) {
        return;
    }
    for (const auto& [coord, grid] : flood->grids()) { // sorted by TileCoord
        snapshot.grids.push_back(
            runner::SnapshotGrid{"water.flood." + std::to_string(coord.tx) + "_" + std::to_string(coord.ty),
                                 k_city_tile_samples, k_city_tile_samples, 4.0F, grid});
    }
}

void publish_terraform_report(core::Simulation& simulation, runner::RenderSnapshot& snapshot) {
    const auto* report = simulation.world().ctx().find<TerraformReport>();
    if (report == nullptr || report->serial == 0) {
        return;
    }
    runner::SnapshotChannel channel;
    channel.name = "world.terraform.last";
    channel.stride = 5;
    channel.data = {static_cast<float>(report->serial), static_cast<float>(report->city),
                    static_cast<float>(report->volume_m3), static_cast<float>(report->cost),
                    static_cast<float>(report->total_cost)};
    snapshot.channels.push_back(std::move(channel));
}

void publish_resource_grids(core::Simulation& simulation, runner::RenderSnapshot& snapshot) {
    CS_PROFILE_ZONE("world.publish_resources");
    auto* fields = simulation.world().ctx().find<ResourceFields>();
    const auto* terrain = simulation.world().ctx().find<WorldTerrain>();
    if (fields == nullptr || terrain == nullptr) {
        return;
    }
    for (const auto& entry : terrain->tiles()) { // sorted by TileCoord
        for (FieldId field = 0; field < fields->fields().size(); ++field) {
            if (std::shared_ptr<const ResourceFields::Grid> grid = fields->grid(field, entry.first)) {
                snapshot.grids.push_back(runner::SnapshotGrid{
                    "resource." + fields->fields()[field].name + "." + std::to_string(entry.first.tx) + "_" +
                        std::to_string(entry.first.ty),
                    ResourceFields::k_tile_cells, ResourceFields::k_tile_cells, 4.0F, std::move(grid)});
            }
        }
    }
}

} // namespace citysim::world
