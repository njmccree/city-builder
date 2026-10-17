#include "citysim/time/calendar_module.hpp"

#include "citysim/core/log.hpp"
#include "citysim/core/state_hasher.hpp"
#include "citysim/time/calendar_state.hpp"
#include "citysim/time/calendar_system.hpp"
#include "citysim/time/time_commands.hpp"
#include "citysim/time/time_defines.hpp"

#include <memory>
#include <stdexcept>
#include <string>

namespace citysim::time {

namespace {

void hash_calendar_state(core::StateHasher& hasher, const CalendarState& state) {
    hasher.add_pod(state.now);
    hasher.add_pod(state.remainder);
    hasher.add_pod(state.era.index);
}

} // namespace

void install_calendar(core::Simulation& simulation, const content::ContentDatabase& content,
                      std::string_view era_id, std::optional<Date> start) {
    if (simulation.world().ctx().find<TimeDefines>() == nullptr) {
        throw std::logic_error("install_calendar needs time::install() to run first");
    }
    const auto& eras = content.registry<content::EraDef>();
    const auto handle = eras.find(era_id);
    if (!handle) {
        throw std::invalid_argument("unknown era '" + std::string(era_id) + "'");
    }
    const Date start_date = start.value_or(Date{eras.get(*handle).default_start_year, 1, 1});
    if (!is_valid(start_date)) {
        throw std::invalid_argument("invalid start date");
    }

    core::Hooks<CalendarState> hooks;
    hooks.hash = &hash_calendar_state;
    simulation.world().registry().register_singleton<CalendarState>("time.CalendarState", hooks);

    CalendarState state;
    state.now = to_calendar_time(start_date);
    state.era = *handle;
    simulation.world().ctx().emplace<CalendarState>(state);

    simulation.add_system(core::Phase::Calendar, std::make_unique<CalendarSystem>(content));

    simulation.dispatcher().register_handler<SetEra>(
        [&content](core::TickContext& context, const SetEra& cmd) {
            const auto& registry = content.registry<content::EraDef>();
            const auto id = content::ContentId::parse(cmd.era_id);
            const auto target = id ? registry.find(*id) : std::nullopt;
            if (!target) {
                CS_LOG_WARN(Sim, "SetEra: unknown era '{}'; ignored", cmd.era_id);
                return;
            }
            auto& calendar = context.world.ctx().get<CalendarState>();
            const std::uint32_t rate = context.world.ctx().get<TimeDefines>().base_tick_rate;
            const std::uint64_t old_denominator = calendar_denominator(registry.get(calendar.era), rate);
            const std::uint64_t new_denominator = calendar_denominator(registry.get(*target), rate);
            calendar.remainder = rescale_remainder(calendar.remainder, old_denominator, new_denominator);
            calendar.era = *target;
        });
}

} // namespace citysim::time
