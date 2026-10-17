#pragma once

#include "citysim/time/calendar.hpp"
#include "citysim/time/scheduled_action.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace citysim::time {

// Dated triggers (architecture §6): "do this when the calendar reaches that moment". Singleton state in the
// World context, registered so it is hashed and, later, saved. Tick clock: CalendarSystem fires what is due
// each tick.
//
// Actions are kept sorted by (time, insertion sequence), so the order they fire in never depends on when in
// the tick they were added or on any container's iteration order.
class CalendarScheduler {
  public:
    struct Entry {
        CalendarTime when = 0;
        std::uint64_t sequence =
            0; // insertion order, breaks ties between actions scheduled for the same time
        ScheduledAction action;
    };

    // Schedules `action` for `when`. A time that has already passed fires on the next calendar tick.
    void schedule(CalendarTime when, ScheduledAction action);

    // Removes and returns every entry with when <= now, in (when, sequence) order.
    std::vector<Entry> pop_due(CalendarTime now);

    const std::vector<Entry>& entries() const { return entries_; }
    std::size_t size() const { return entries_.size(); }
    std::uint64_t next_sequence() const { return next_sequence_; }

  private:
    std::vector<Entry> entries_; // sorted by (when, sequence)
    std::uint64_t next_sequence_ = 0;
};

} // namespace citysim::time
