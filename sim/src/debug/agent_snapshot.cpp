#include "citysim/debug/agent_snapshot.hpp"

#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/terrain_walk_grid.hpp"
#include "citysim/debug/test_map.hpp"
#include "citysim/profiling/profile.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/world_terrain.hpp"

#include <algorithm>
#include <entt/entt.hpp>
#include <vector>

namespace citysim::debug {

void publish_agent_snapshot(core::Simulation& simulation, runner::RenderSnapshot& snapshot) {
    CS_PROFILE_ZONE("debug.publish_agents");
    core::World& world = simulation.world();
    auto view = world.view<Position, Heading>();

    std::vector<entt::entity> agents;
    agents.reserve(view.size_hint());
    for (const entt::entity entity : view) {
        agents.push_back(entity);
    }
    std::sort(agents.begin(), agents.end(),
              [](entt::entity a, entt::entity b) { return entt::to_entity(a) < entt::to_entity(b); });

    // On real terrain the agents stand on the ground: y is the terrain height at their city-local position.
    const world::WorldTerrain* terrain = nullptr;
    const world::CityInfo* city = nullptr;
    if (const auto* grid = world.ctx().find<TerrainWalkGrid>(); grid != nullptr && grid->city_id() != 0) {
        terrain = world.ctx().find<world::WorldTerrain>();
        const auto* cities = world.ctx().find<world::CityRegistry>();
        city = cities != nullptr ? cities->find(grid->city_id()) : nullptr;
    }

    runner::SnapshotChannel channel;
    channel.name = k_agent_channel;
    channel.stride = 4;
    channel.data.reserve(agents.size() * 4);
    channel.ids.reserve(agents.size());
    for (const entt::entity entity : agents) {
        const Position& position = view.get<Position>(entity);
        channel.data.push_back(position.x);
        channel.data.push_back(
            terrain != nullptr && city != nullptr
                ? terrain->height_at_local(city->area(), world::LocalPos{position.x, position.z})
                : 0.0F);
        channel.data.push_back(position.z);
        channel.data.push_back(view.get<Heading>(entity).yaw);
        channel.ids.push_back(static_cast<std::uint32_t>(entt::to_entity(entity)));
    }
    snapshot.channels.push_back(std::move(channel));
}

void TestMapPublisher::operator()(core::Simulation& simulation, runner::RenderSnapshot& snapshot) {
    const TestMap* map = simulation.world().ctx().find<TestMap>();
    if (map == nullptr) {
        return;
    }
    // A cheap fingerprint of the cells (a few thousand words even for a big map) tells us when to rebuild.
    std::uint64_t fingerprint = 1469598103934665603ULL;
    for (const std::uint64_t word : map->words()) {
        fingerprint = (fingerprint ^ word) * 1099511628211ULL;
    }
    if (cells_ == nullptr || fingerprint != fingerprint_ || map->width() != width_ ||
        map->height() != height_ || map->cell_size() != cell_size_) {
        auto cells = std::make_shared<std::vector<std::uint8_t>>(map->cell_count());
        for (std::uint32_t index = 0; index < map->cell_count(); ++index) {
            (*cells)[index] = map->walkable(index) ? 1 : 0;
        }
        cells_ = std::move(cells);
        fingerprint_ = fingerprint;
        width_ = map->width();
        height_ = map->height();
        cell_size_ = map->cell_size();
    }
    runner::SnapshotGrid grid;
    grid.name = k_test_map_grid;
    grid.width = width_;
    grid.height = height_;
    grid.cell_size = cell_size_;
    grid.cells = cells_;
    snapshot.grids.push_back(std::move(grid));
}

} // namespace citysim::debug
