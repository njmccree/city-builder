#pragma once

// Save/load of the time module's state (architecture §10; pattern in citysim/save/archive.hpp). Field order
// is the save format: append with a version bump, never reorder.

#include "citysim/save/archive.hpp"
#include "citysim/save/def_handle_io.hpp"
#include "citysim/time/calendar_scheduler.hpp"
#include "citysim/time/calendar_state.hpp"
#include "citysim/time/game_clock.hpp"
#include "citysim/time/scheduled_action.hpp"
#include "citysim/time/time_commands.hpp"
#include "citysim/time/time_defines.hpp"

#include <stdexcept>
#include <vector>

namespace citysim::time {

template <class Ar> void serialize(Ar& ar, GameClock& clock, std::uint32_t version) {
    save::io(ar, clock.speed, version);
    save::io(ar, clock.paused, version);
    save::io(ar, clock.day_tick, version);
    save::io(ar, clock.day_index, version);
}

template <class Ar> void serialize(Ar& ar, TimeDefines& defines, std::uint32_t version) {
    save::io(ar, defines.base_tick_rate, version);
    save::io(ar, defines.speed_multipliers, version);
    save::io(ar, defines.movement_scale, version);
    save::io(ar, defines.day_length_ticks, version);
}

template <class Ar> void serialize(Ar& ar, CalendarState& state, std::uint32_t version) {
    save::io(ar, state.now, version);
    save::io(ar, state.remainder, version);
    save::io(ar, state.era, version);
}

template <class Ar> void serialize(Ar& ar, SetSpeed& command, std::uint32_t version) {
    save::io(ar, command.speed, version);
}

template <class Ar> void serialize(Ar&, TogglePause&, std::uint32_t) {}

template <class Ar> void serialize(Ar& ar, SetTimeScale& command, std::uint32_t version) {
    save::io(ar, command.movement_scale, version);
    save::io(ar, command.day_length_ticks, version);
}

template <class Ar> void serialize(Ar& ar, SetEra& command, std::uint32_t version) {
    save::io(ar, command.era_id, version);
}

template <class Ar> void serialize(Ar& ar, DebugAction& action, std::uint32_t version) {
    save::io(ar, action.tag, version);
}

template <class Ar> void serialize(Ar& ar, CalendarScheduler::Entry& entry, std::uint32_t version) {
    save::io(ar, entry.when, version);
    save::io(ar, entry.sequence, version);
    save::io(ar, entry.action, version);
}

template <class Ar> void serialize(Ar& ar, CalendarScheduler& scheduler, std::uint32_t version) {
    std::vector<CalendarScheduler::Entry> entries = scheduler.entries();
    std::uint64_t next_sequence = scheduler.next_sequence();
    save::io(ar, entries, version);
    save::io(ar, next_sequence, version);
    if constexpr (Ar::is_loading) {
        try {
            scheduler.restore(std::move(entries), next_sequence);
        } catch (const std::invalid_argument& error) {
            ar.fail(error.what()); // a corrupt save, not a bug in the caller
        }
    }
}

} // namespace citysim::time
