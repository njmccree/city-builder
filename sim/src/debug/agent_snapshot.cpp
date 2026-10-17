#include "citysim/debug/agent_snapshot.hpp"

#include "citysim/debug/agent_components.hpp"
#include "citysim/profiling/profile.hpp"

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

    runner::SnapshotChannel channel;
    channel.name = k_agent_channel;
    channel.stride = 4;
    channel.data.reserve(agents.size() * 4);
    channel.ids.reserve(agents.size());
    for (const entt::entity entity : agents) {
        const Position& position = view.get<Position>(entity);
        channel.data.push_back(position.x);
        channel.data.push_back(0.0F);
        channel.data.push_back(position.z);
        channel.data.push_back(view.get<Heading>(entity).yaw);
        channel.ids.push_back(static_cast<std::uint32_t>(entt::to_entity(entity)));
    }
    snapshot.channels.push_back(std::move(channel));
}

} // namespace citysim::debug
