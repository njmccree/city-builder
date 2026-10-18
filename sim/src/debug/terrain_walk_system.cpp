#include "citysim/debug/terrain_walk_system.hpp"

#include "citysim/core/tick_context.hpp"
#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/terrain_walk_grid.hpp"
#include "citysim/profiling/profile.hpp"
#include "citysim/world/city_registry.hpp"

#include <entt/entt.hpp>
#include <vector>

namespace citysim::debug {

void follow_active_city(core::World& world, core::JobSystem& jobs) {
    auto* grid = world.ctx().find<TerrainWalkGrid>();
    const auto* active = world.ctx().find<world::ActiveCity>();
    const auto* cities = world.ctx().find<world::CityRegistry>();
    if (grid == nullptr || active == nullptr || cities == nullptr) {
        return;
    }
    const world::CityInfo* city = cities->find(active->city_id);
    const std::uint32_t city_id = city != nullptr ? city->id : 0;
    const std::uint32_t previous = grid->city_id();
    const TerrainWalkGrid::SyncResult result =
        grid->sync(world, jobs, city_id, city != nullptr ? city->area() : world::CityArea{});
    if (result.city_changed && previous != 0) {
        std::vector<entt::entity> agents;
        for (const entt::entity entity : world.view<DebugWanderer>()) {
            agents.push_back(entity);
        }
        for (const entt::entity entity : agents) {
            world.destroy(entity);
        }
    }
}

void TerrainWalkSystem::update(core::TickContext& context) {
    CS_PROFILE_ZONE("debug.terrain_walk");
    follow_active_city(context.world, context.jobs);
}

} // namespace citysim::debug
