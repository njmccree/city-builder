#include "citysim/world/terrain_publisher.hpp"

#include "citysim/profiling/profile.hpp"
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

} // namespace citysim::world
