#include "citysim/time/calendar_scheduler.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace citysim::time {

void CalendarScheduler::schedule(CalendarTime when, ScheduledAction action) {
    Entry entry{when, next_sequence_++, std::move(action)};
    // After every entry that is due no later than this one: equal times stay in insertion order.
    const auto position = std::upper_bound(entries_.begin(), entries_.end(), when,
                                           [](CalendarTime value, const Entry& e) { return value < e.when; });
    entries_.insert(position, std::move(entry));
}

std::vector<CalendarScheduler::Entry> CalendarScheduler::pop_due(CalendarTime now) {
    const auto first_not_due =
        std::find_if(entries_.begin(), entries_.end(), [now](const Entry& e) { return e.when > now; });
    std::vector<Entry> due(std::make_move_iterator(entries_.begin()), std::make_move_iterator(first_not_due));
    entries_.erase(entries_.begin(), first_not_due);
    return due;
}

} // namespace citysim::time
