#include "citysim/debug/wander_planner_system.hpp"

#include "citysim/core/rng.hpp"
#include "citysim/core/tick_context.hpp"
#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/terrain_walk_grid.hpp"
#include "citysim/debug/walk_grid.hpp"
#include "citysim/profiling/profile.hpp"

#include <algorithm>
#include <entt/entt.hpp>

namespace citysim::debug {

void WanderPlannerSystem::update(core::TickContext& context) {
    CS_PROFILE_ZONE("debug.wander_planner");
    searches_last_tick_ = 0;
    const WalkGrid* map = find_walk_grid(context.world);
    if (map == nullptr || map->cell_count() == 0) {
        return;
    }

    // The world changed under the agents (a flood, an edit): a path with a blocked cell left on it is no use.
    // Dropping it makes the agent idle, so it is planned again below.
    auto view = context.world.view<DebugWanderer, PathFollower, Position>();
    if (map->version() != checked_version_) {
        checked_version_ = map->version();
        for (const entt::entity entity : view) {
            PathFollower& follower = view.get<PathFollower>(entity);
            for (std::uint32_t i = follower.next; i < follower.cells.size(); ++i) {
                if (!map->walkable(follower.cells[i])) {
                    follower.cells.clear();
                    follower.next = 0;
                    ++paths_invalidated_total_;
                    break;
                }
            }
        }
    }

    // The agents with nothing to walk to. Only entity values are collected; the order of the view is not
    // used.
    candidates_.clear();
    for (const entt::entity entity : view) {
        if (view.get<PathFollower>(entity).finished()) {
            candidates_.push_back(entt::to_integral(entity));
        }
    }
    const auto by_index = [](std::uint32_t a, std::uint32_t b) {
        return entt::to_entity(entt::entity{a}) < entt::to_entity(entt::entity{b});
    };
    if (candidates_.size() > max_searches_per_tick_) {
        std::nth_element(candidates_.begin(), candidates_.begin() + max_searches_per_tick_, candidates_.end(),
                         by_index);
        candidates_.resize(max_searches_per_tick_);
    }
    std::sort(candidates_.begin(), candidates_.end(), by_index);

    // On real terrain a river can cut the area in two: destinations come from the agent's own region.
    auto* terrain_grid = context.world.ctx().find<TerrainWalkGrid>();
    if (terrain_grid != nullptr && !candidates_.empty()) {
        terrain_grid->update_regions();
    }

    const std::uint64_t stream = rng::stream("debug.wander");
    for (const std::uint32_t value : candidates_) {
        const entt::entity entity{value};
        auto& wanderer = view.get<DebugWanderer>(entity);
        auto& follower = view.get<PathFollower>(entity);
        const Position& position = view.get<Position>(entity);

        // Standing on a blocked cell (flooded under the agent): the way out comes first. It draws no
        // destination, so the agent's trips do not change.
        const std::uint32_t here = map->cell_at(position.x, position.z);
        if (!map->walkable(here)) {
            ++searches_last_tick_;
            PathResult out = find_escape_path(*map, here, scratch_, node_budget_);
            if (out.found) {
                follower.cells = std::move(out.cells);
                follower.next = 0;
            }
            continue;
        }

        // Counted whether or not a path is found, so the next attempt draws a different destination.
        const std::uint64_t hash = context.rng.at(stream, value, wanderer.trips);
        ++wanderer.trips;
        ++searches_last_tick_;

        const std::optional<std::uint32_t> destination =
            terrain_grid != nullptr ? pick_walkable_cell_in_region(*terrain_grid, here, hash)
                                    : pick_walkable_cell(*map, hash);
        if (!destination) {
            continue;
        }
        PathResult path = find_path(*map, here, *destination, scratch_, node_budget_);
        if (path.found) {
            follower.cells = std::move(path.cells);
            follower.next = 0;
        }
    }
    searches_total_ += searches_last_tick_;
}

} // namespace citysim::debug
