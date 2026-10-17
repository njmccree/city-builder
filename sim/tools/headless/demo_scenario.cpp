#include "headless/demo_scenario.hpp"

#include "citysim/core/rng.hpp"
#include "citysim/core/state_hasher.hpp"

#include <memory>

namespace citysim::headless {

using namespace citysim::core;

namespace {

struct DemoPosition {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

void hash_demo_position(StateHasher& hasher, const DemoPosition& position) {
    hasher.add_pod(position.x);
    hasher.add_pod(position.y);
}

class SpawnSystem final : public ISystem {
  public:
    std::string_view name() const override { return "demo.spawn"; }
    void update(TickContext& context) override {
        if (context.tick != 0) {
            return;
        }
        for (std::uint32_t i = 0; i < k_demo_population; ++i) {
            context.world.emplace<DemoPosition>(context.world.create());
        }
    }
};

class WanderSystem final : public ISystem {
  public:
    std::string_view name() const override { return "demo.wander"; }
    void update(TickContext& context) override {
        const std::uint64_t stream = rng::stream("demo.wander");
        for (const auto entity : context.world.view<DemoPosition>()) {
            // Keyed by entity, so the result does not depend on the order entities are visited.
            const std::uint64_t h = context.rng.at(stream, entt::to_integral(entity));
            auto& position = context.world.get<DemoPosition>(entity);
            position.x += static_cast<std::int32_t>(rng::uniform_int(h, -1, 1));
            position.y += static_cast<std::int32_t>(rng::uniform_int(h >> 11, -1, 1));
        }
    }
};

} // namespace

void setup_demo(Simulation& sim) {
    Hooks<DemoPosition> hooks;
    hooks.hash = &hash_demo_position;
    sim.world().registry().register_type<DemoPosition>("demo.Position", hooks);

    sim.add_system(Phase::ApplyCommands, std::make_unique<SpawnSystem>());
    sim.add_system(Phase::Update, std::make_unique<WanderSystem>());

    sim.dispatcher().register_handler<NoOp>([](TickContext&, const NoOp&) {});
    sim.dispatcher().register_handler<DebugSetComponentValue>(
        [](TickContext& context, const DebugSetComponentValue& cmd) {
            // The command carries the entity's index; rebuild the handle from the live entities.
            for (const auto entity : context.world.view<DemoPosition>()) {
                if (entt::to_entity(entity) == cmd.entity) {
                    context.world.get<DemoPosition>(entity).x = cmd.value;
                    return;
                }
            }
        });
}

std::vector<Command> demo_input(std::uint64_t seed, std::uint64_t tick) {
    std::vector<Command> commands;
    if (tick == 0) {
        return commands;
    }
    const std::uint64_t stream = rng::stream("demo.input");
    const std::uint64_t h = rng::hash64(seed, stream, tick, 0, 0);
    if (tick % 25 == 0) {
        commands.push_back(
            DebugSetComponentValue{static_cast<std::uint32_t>(rng::uniform_int(h, 0, k_demo_population - 1)),
                                   static_cast<std::int32_t>(rng::uniform_int(h >> 9, -100, 100))});
    }
    if (tick % 40 == 0) {
        commands.push_back(NoOp{});
    }
    return commands;
}

} // namespace citysim::headless
