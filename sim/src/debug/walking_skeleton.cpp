#include "citysim/debug/walking_skeleton.hpp"

#include "citysim/core/rng.hpp"
#include "citysim/core/state_hasher.hpp"
#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/debug_serialize.hpp"
#include "citysim/debug/movement_system.hpp"
#include "citysim/debug/terrain_walk_grid.hpp"
#include "citysim/debug/terrain_walk_system.hpp"
#include "citysim/debug/test_map.hpp"
#include "citysim/debug/walk_grid.hpp"
#include "citysim/save/bind_serializer.hpp"
#include "citysim/time/time_defines.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/world_terrain.hpp"

#include <entt/entt.hpp>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace citysim::debug {

namespace {

void hash_position(core::StateHasher& hasher, const Position& position) {
    hasher.add_float(position.x);
    hasher.add_float(position.z);
}

void hash_heading(core::StateHasher& hasher, const Heading& heading) {
    hasher.add_float(heading.yaw);
}

void hash_follower(core::StateHasher& hasher, const PathFollower& follower) {
    hasher.add_pod(static_cast<std::uint64_t>(follower.cells.size()));
    hasher.add(std::as_bytes(std::span<const std::uint32_t>(follower.cells)));
    hasher.add_pod(follower.next);
    hasher.add_float(follower.speed_mps);
}

void hash_wanderer(core::StateHasher& hasher, const DebugWanderer& wanderer) {
    hasher.add_pod(wanderer.trips);
}

void hash_counter(core::StateHasher& hasher, const DebugAgentCounter& counter) {
    hasher.add_pod(counter.spawned);
}

void hash_map(core::StateHasher& hasher, const TestMap& map) {
    hasher.add_pod(map.width());
    hasher.add_pod(map.height());
    hasher.add_float(map.cell_size());
    hasher.add_pod(static_cast<std::uint64_t>(map.words().size()));
    hasher.add(std::as_bytes(std::span<const std::uint64_t>(map.words())));
}

template <class T>
void register_component(core::World& world, const char* name, void (*hash)(core::StateHasher&, const T&)) {
    core::Hooks<T> hooks;
    hooks.hash = hash;
    save::bind_serializer(hooks);
    world.registry().register_type<T>(name, hooks);
}

template <class T>
void register_singleton(core::World& world, const char* name, void (*hash)(core::StateHasher&, const T&)) {
    core::Hooks<T> hooks;
    hooks.hash = hash;
    save::bind_serializer(hooks);
    world.registry().register_singleton<T>(name, hooks);
}

void spawn_agents(core::TickContext& context, const SpawnDebugAgents& command) {
    const WalkGrid* map = find_walk_grid(context.world);
    auto* counter = context.world.ctx().find<DebugAgentCounter>();
    if (map == nullptr || counter == nullptr) {
        return;
    }
    const std::uint64_t stream = rng::stream("debug.spawn");
    for (std::uint32_t i = 0; i < command.count; ++i) {
        // Every agent ever spawned has its own serial, so draws never repeat, even after a clear.
        const std::uint64_t serial = counter->spawned++;
        const std::uint64_t hash = context.rng.at(stream, serial);
        const std::optional<std::uint32_t> cell = pick_walkable_cell(*map, hash);
        if (!cell) {
            return; // a map with nowhere to stand
        }
        const entt::entity entity = context.world.create();
        context.world.emplace<Position>(entity, Position{map->center_x(*cell), map->center_z(*cell)});
        context.world.emplace<Heading>(entity);
        PathFollower follower;
        follower.speed_mps =
            static_cast<float>(rng::uniform_int(rng::hash64(hash, 4, 5, 6, 7), 100, 180)) / 100.0F;
        context.world.emplace<PathFollower>(entity, std::move(follower));
        context.world.emplace<DebugWanderer>(entity);
    }
}

void clear_agents(core::TickContext& context) {
    std::vector<entt::entity> agents;
    for (const entt::entity entity : context.world.view<DebugWanderer>()) {
        agents.push_back(entity);
    }
    for (const entt::entity entity : agents) {
        context.world.destroy(entity);
    }
}

} // namespace

WalkingSkeleton install(core::Simulation& simulation, const WalkingSkeletonConfig& config) {
    core::World& world = simulation.world();
    if (world.ctx().find<time::TimeDefines>() == nullptr) {
        throw std::logic_error("debug::install needs time::install() to run first");
    }

    register_singleton<DebugAgentCounter>(world, "debug.AgentCounter", &hash_counter);
    register_component<Position>(world, "debug.Position", &hash_position);
    register_component<Heading>(world, "debug.Heading", &hash_heading);
    register_component<PathFollower>(world, "debug.PathFollower", &hash_follower);
    register_component<DebugWanderer>(world, "debug.Wanderer", &hash_wanderer);

    world.ctx().emplace<DebugAgentCounter>();
    if (config.source == WalkGridSource::Terrain) {
        if (world.ctx().find<world::WorldTerrain>() == nullptr ||
            world.ctx().find<world::ActiveCity>() == nullptr) {
            throw std::logic_error(
                "debug::install with the terrain as its walk grid needs world::install() to run first");
        }
        // Derived from the terrain, the water and the floods; not registered, so neither hashed nor saved.
        world.ctx().emplace<TerrainWalkGrid>();
        simulation.add_system(core::Phase::PostUpdate, std::make_unique<TerrainWalkSystem>());
        // A loaded game builds its grid at once, so its first tick plans and moves the agents like the tick
        // of the game it was saved from did.
        simulation.add_after_load_hook([](core::Simulation& loaded, std::vector<std::string>&) {
            follow_active_city(loaded.world(), loaded.jobs());
        });
    } else {
        register_singleton<TestMap>(world, "debug.TestMap", &hash_map);
        world.ctx().emplace<TestMap>(
            make_test_map(simulation.world_seed(), config.map_width, config.map_height, config.cell_size));
    }

    auto planner =
        std::make_unique<WanderPlannerSystem>(config.max_searches_per_tick, config.path_node_budget);
    WalkingSkeleton installed;
    installed.planner = planner.get();
    simulation.add_system(core::Phase::Update, std::move(planner));
    simulation.add_system(core::Phase::Update, std::make_unique<MovementSystem>());

    simulation.dispatcher().register_handler<SpawnDebugAgents>(
        [](core::TickContext& context, const SpawnDebugAgents& command) { spawn_agents(context, command); });
    simulation.dispatcher().register_handler<ClearDebugAgents>(
        [](core::TickContext& context, const ClearDebugAgents&) { clear_agents(context); });
    return installed;
}

} // namespace citysim::debug
