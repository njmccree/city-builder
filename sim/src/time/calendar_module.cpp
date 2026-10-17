#include "citysim/time/calendar_module.hpp"

#include "citysim/core/log.hpp"
#include "citysim/core/state_hasher.hpp"
#include "citysim/time/calendar_scheduler.hpp"
#include "citysim/time/calendar_state.hpp"
#include "citysim/time/calendar_system.hpp"
#include "citysim/time/time_commands.hpp"
#include "citysim/time/time_defines.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace citysim::time {

namespace {

void hash_calendar_state(core::StateHasher& hasher, const CalendarState& state) {
    hasher.add_pod(state.now);
    hasher.add_pod(state.remainder);
    hasher.add_pod(state.era.index);
}

void hash_calendar_scheduler(core::StateHasher& hasher, const CalendarScheduler& scheduler) {
    hasher.add_pod(scheduler.next_sequence());
    hasher.add_pod(static_cast<std::uint64_t>(scheduler.size()));
    for (const CalendarScheduler::Entry& entry : scheduler.entries()) {
        hasher.add_pod(entry.when);
        hasher.add_pod(entry.sequence);
        hasher.add_pod(static_cast<std::uint64_t>(entry.action.index()));
        std::visit(
            [&hasher](const auto& action) {
                using Action = std::decay_t<decltype(action)>;
                if constexpr (std::is_same_v<Action, DebugAction>) {
                    hasher.add(action.tag);
                }
            },
            entry.action);
    }
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

    core::Hooks<CalendarScheduler> scheduler_hooks;
    scheduler_hooks.hash = &hash_calendar_scheduler;
    simulation.world().registry().register_singleton<CalendarScheduler>("time.CalendarScheduler",
                                                                        scheduler_hooks);
    simulation.world().ctx().emplace<CalendarScheduler>();

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
