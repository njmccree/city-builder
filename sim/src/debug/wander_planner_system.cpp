#include "citysim/debug/wander_planner_system.hpp"

#include "citysim/core/rng.hpp"
#include "citysim/core/tick_context.hpp"
#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/test_map.hpp"
#include "citysim/profiling/profile.hpp"

#include <algorithm>
#include <entt/entt.hpp>

namespace citysim::debug {

void WanderPlannerSystem::update(core::TickContext& context) {
    CS_PROFILE_ZONE("debug.wander_planner");
    searches_last_tick_ = 0;
    const TestMap* map = context.world.ctx().find<TestMap>();
    if (map == nullptr) {
        return;
    }

    // The agents with nothing to walk to. Only entity values are collected; the order of the view is not
    // used.
    auto view = context.world.view<DebugWanderer, PathFollower, Position>();
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

    const std::uint64_t stream = rng::stream("debug.wander");
    for (const std::uint32_t value : candidates_) {
        const entt::entity entity{value};
        auto& wanderer = view.get<DebugWanderer>(entity);
        auto& follower = view.get<PathFollower>(entity);
        const Position& position = view.get<Position>(entity);

        // Counted whether or not a path is found, so the next attempt draws a different destination.
        const std::uint64_t hash = context.rng.at(stream, value, wanderer.trips);
        ++wanderer.trips;
        ++searches_last_tick_;

        const std::optional<std::uint32_t> destination = pick_walkable_cell(*map, hash);
        if (!destination) {
            continue;
        }
        PathResult path =
            find_path(*map, map->cell_at(position.x, position.z), *destination, scratch_, node_budget_);
        if (path.found) {
            follower.cells = std::move(path.cells);
            follower.next = 0;
        }
    }
    searches_total_ += searches_last_tick_;
}

} // namespace citysim::debug
