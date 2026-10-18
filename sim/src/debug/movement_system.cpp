#include "citysim/debug/movement_system.hpp"

#include "citysim/core/tick_context.hpp"
#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/walk_grid.hpp"
#include "citysim/profiling/profile.hpp"
#include "citysim/time/game_clock.hpp"
#include "citysim/time/time_defines.hpp"

#include <algorithm>
#include <cmath>
#include <entt/entt.hpp>

namespace citysim::debug {

namespace {

constexpr std::size_t k_chunk_size = 1024;

// Walks one agent for `seconds` of simulated time.
void advance(const WalkGrid& map, Position& position, Heading& heading, PathFollower& follower,
             float seconds) {
    float budget = follower.speed_mps * seconds;
    while (budget > 0.0F && !follower.finished()) {
        const std::uint32_t cell = follower.cells[follower.next];
        const float dx = map.center_x(cell) - position.x;
        const float dz = map.center_z(cell) - position.z;
        const float distance = std::sqrt(dx * dx + dz * dz);
        if (distance > 0.0F) {
            heading.yaw = std::atan2(dx, dz);
        }
        if (distance <= budget) {
            position.x = map.center_x(cell);
            position.z = map.center_z(cell);
            budget -= distance;
            ++follower.next;
        } else {
            position.x += dx / distance * budget;
            position.z += dz / distance * budget;
            budget = 0.0F;
        }
    }
}

} // namespace

void MovementSystem::update(core::TickContext& context) {
    CS_PROFILE_ZONE("debug.movement");
    const WalkGrid* map = find_walk_grid(context.world);
    const time::TimeDefines* defines = context.world.ctx().find<time::TimeDefines>();
    if (map == nullptr || map->cell_count() == 0 || defines == nullptr) {
        return;
    }
    const float seconds = time::movement_seconds_per_tick(*defines);

    auto view = context.world.view<Position, Heading, PathFollower>();
    agents_.clear();
    for (const entt::entity entity : view) {
        if (!view.get<PathFollower>(entity).finished()) {
            agents_.push_back(entt::to_integral(entity));
        }
    }

    // Each chunk touches only its own agents' components, so no output needs merging.
    context.parallel_for(agents_.size(), k_chunk_size, [&](std::size_t begin, std::size_t end, std::size_t) {
        for (std::size_t i = begin; i < end; ++i) {
            const entt::entity entity{agents_[i]};
            advance(*map, view.get<Position>(entity), view.get<Heading>(entity),
                    view.get<PathFollower>(entity), seconds);
        }
    });
}

} // namespace citysim::debug
