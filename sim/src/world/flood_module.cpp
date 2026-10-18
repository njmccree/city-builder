#include "citysim/world/flood_module.hpp"

#include "citysim/core/isystem.hpp"
#include "citysim/core/log.hpp"
#include "citysim/core/tick_context.hpp"
#include "citysim/core/ui_notice.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/flood_map.hpp"
#include "citysim/world/water_commands.hpp"
#include "citysim/world/water_events.hpp"
#include "citysim/world/water_state.hpp"
#include "citysim/world/world_terrain.hpp"

#include <cmath>
#include <memory>

namespace citysim::world {

namespace {

class FloodSystem final : public core::ISystem {
  public:
    std::string_view name() const override { return "world.flood"; }

    void update(core::TickContext& context) override {
        core::World& world = context.world;
        const auto* terrain = world.ctx().find<WorldTerrain>();
        const auto* water = world.ctx().find<WaterState>();
        auto* flood = world.ctx().find<FloodMap>();
        if (terrain == nullptr || water == nullptr || flood == nullptr || !terrain->has_map()) {
            return;
        }
        const auto* active = world.ctx().find<ActiveCity>();
        const std::uint32_t city = active != nullptr ? active->city_id : 0;
        const std::size_t budget = terrain->terrain_defines().flood_samples_per_tick;
        for (const FloodMap::Finished& done : flood->advance(*terrain, *water, budget)) {
            context.events.emit(WaterChanged{city, done.reach, done.flooded_area_m2});
        }
    }
};

void set_river_level(core::TickContext& context, const SetRiverLevel& command) {
    core::World& world = context.world;
    auto& water = world.ctx().get<WaterState>();
    const char* problem = nullptr;
    if (command.reach_index >= water.reach_count()) {
        problem = k_error_unknown_reach;
    } else if (!std::isfinite(command.offset_m)) {
        problem = k_error_offset;
    }
    if (problem != nullptr) {
        CS_LOG_INFO(Sim, "SetRiverLevel reach {} refused: {}", command.reach_index, problem);
        world.ctx().get<core::UiOutbox>().push(
            core::UiNotice{"river_level_failed", problem, command.reach_index});
        return;
    }
    water.set_level_offset(command.reach_index, command.offset_m);
}

} // namespace

void install_flood(core::Simulation& simulation) {
    simulation.world().ctx().emplace<FloodMap>();
    simulation.add_system(core::Phase::PostUpdate, std::make_unique<FloodSystem>());
    simulation.dispatcher().register_handler<SetRiverLevel>(&set_river_level);
}

} // namespace citysim::world
