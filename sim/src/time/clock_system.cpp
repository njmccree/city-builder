#include "citysim/time/clock_system.hpp"

#include "citysim/core/tick_context.hpp"
#include "citysim/time/game_clock.hpp"

namespace citysim::time {

void ClockSystem::update(core::TickContext& context) {
    auto& clock = context.world.ctx().get<GameClock>();
    const auto& defines = context.world.ctx().get<TimeDefines>();

    ++clock.day_tick;
    if (clock.day_tick >= defines.day_length_ticks) {
        clock.day_tick = 0;
        ++clock.day_index;
    }
}

} // namespace citysim::time
