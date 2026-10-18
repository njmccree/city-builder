#include "citysim/time/game_clock.hpp"

namespace citysim::time {

double time_of_day_fraction(const GameClock& clock, const TimeDefines& defines) {
    return static_cast<double>(clock.day_tick) / static_cast<double>(defines.day_length_ticks);
}

std::uint32_t hour(const GameClock& clock, const TimeDefines& defines) {
    return static_cast<std::uint32_t>(clock.day_tick * 24 / defines.day_length_ticks);
}

std::uint32_t minute(const GameClock& clock, const TimeDefines& defines) {
    return static_cast<std::uint32_t>(clock.day_tick * 1440 / defines.day_length_ticks % 60);
}

std::uint32_t ticks_per_second(const GameClock& clock, const TimeDefines& defines) {
    return defines.base_tick_rate * defines.speed_multipliers[clock.speed];
}

float movement_seconds_per_tick(const TimeDefines& defines) {
    return defines.movement_scale / static_cast<float>(defines.base_tick_rate);
}

} // namespace citysim::time
