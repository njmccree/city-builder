#include "citysim/time/time_module.hpp"

#include "citysim/core/log.hpp"
#include "citysim/core/state_hasher.hpp"
#include "citysim/time/clock_system.hpp"
#include "citysim/time/game_clock.hpp"
#include "citysim/time/time_commands.hpp"

#include <memory>
#include <stdexcept>

namespace citysim::time {

namespace {

void hash_game_clock(core::StateHasher& hasher, const GameClock& clock) {
    hasher.add_pod(clock.speed);
    hasher.add_pod(static_cast<std::uint8_t>(clock.paused ? 1 : 0));
    hasher.add_pod(clock.day_tick);
    hasher.add_pod(clock.day_index);
}

void hash_time_defines(core::StateHasher& hasher, const TimeDefines& defines) {
    hasher.add_pod(defines.base_tick_rate);
    for (const std::uint32_t multiplier : defines.speed_multipliers) {
        hasher.add_pod(multiplier);
    }
    hasher.add_float(defines.movement_scale);
    hasher.add_pod(defines.day_length_ticks);
}

} // namespace

void validate(const TimeDefines& defines) {
    if (defines.base_tick_rate == 0) {
        throw std::invalid_argument("TimeDefines: base_tick_rate must be positive");
    }
    if (defines.day_length_ticks == 0) {
        throw std::invalid_argument("TimeDefines: day_length_ticks must be positive");
    }
    for (std::size_t speed = 1; speed < defines.speed_multipliers.size(); ++speed) {
        if (defines.speed_multipliers[speed] == 0) {
            throw std::invalid_argument("TimeDefines: every speed from 1 to 3 needs a positive multiplier");
        }
    }
}

void install(core::Simulation& simulation, const TimeDefines& defines) {
    validate(defines);

    auto& registry = simulation.world().registry();
    core::Hooks<GameClock> clock_hooks;
    clock_hooks.hash = &hash_game_clock;
    registry.register_singleton<GameClock>("time.GameClock", clock_hooks);
    core::Hooks<TimeDefines> defines_hooks;
    defines_hooks.hash = &hash_time_defines;
    registry.register_singleton<TimeDefines>("time.TimeDefines", defines_hooks);

    simulation.world().ctx().emplace<GameClock>();
    simulation.world().ctx().emplace<TimeDefines>(defines);

    simulation.add_system(core::Phase::PreUpdate, std::make_unique<ClockSystem>());

    simulation.dispatcher().register_handler<SetSpeed>([](core::TickContext& context, const SetSpeed& cmd) {
        const auto& table = context.world.ctx().get<TimeDefines>().speed_multipliers;
        if (cmd.speed < 1 || cmd.speed >= table.size()) {
            CS_LOG_WARN(Sim, "SetSpeed {} is out of range (1..{}); ignored", cmd.speed, table.size() - 1);
            return;
        }
        context.world.ctx().get<GameClock>().speed = cmd.speed;
    });
    simulation.dispatcher().register_handler<TogglePause>([](core::TickContext& context, const TogglePause&) {
        auto& clock = context.world.ctx().get<GameClock>();
        clock.paused = !clock.paused;
    });
}

} // namespace citysim::time
