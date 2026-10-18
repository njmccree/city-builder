#include "citysim/world/terrain_publisher.hpp"

#include "citysim/profiling/profile.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/flood_map.hpp"
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

} // namespace citysim::world
