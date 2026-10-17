#pragma once

#include "citysim/time/calendar.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace citysim::runner {

enum class UiEventKind { Message, SaveCompleted, LoadCompleted };

// Something the UI should react to (a toast, a sound). `text` is the message; for SaveCompleted and
// LoadCompleted it is a failure's reason (empty when ok). Real event kinds arrive with the UI tasks.
struct UiEvent {
    UiEvent() = default;
    UiEvent(std::string message) : text(std::move(message)) {} // NOLINT: UiEvent{"text"} is the common case

    std::string text;
    UiEventKind kind = UiEventKind::Message;
    std::string path;      // SaveCompleted / LoadCompleted: the file
    bool ok = true;        // SaveCompleted / LoadCompleted: whether it worked
    bool autosave = false; // SaveCompleted: written by the autosave timer
    // Set by the runner on SaveCompleted / LoadCompleted: these events stay in snapshots until the consumer
    // has read one, and latest_snapshot() hands each to the consumer once. 0 for other events.
    std::uint64_t serial = 0;
    std::vector<std::string> warnings; // LoadCompleted: e.g. the content changed since the save
};

struct SimStats {
    double last_tick_ms = 0.0;
    double avg_tick_ms = 0.0; // over the last 60 ticks
    std::size_t entity_count = 0;
    double ticks_per_second = 0.0; // measured over the last 60 ticks, in clock time
};

// What the simulation hands to the front end after each tick (architecture §7.3). A read-only copy:
// renderers never read World directly. Extensible: modules add data in the publish hook, and later
// tasks add flat arrays for renderers here.
struct RenderSnapshot {
    std::uint64_t tick = 0;
    std::uint8_t speed = 1; // 1..3, an index into the runner's speed table
    bool paused = false;
    double published_at = 0.0; // runner clock seconds, for interpolating between snapshots
    SimStats stats;
    // The calendar date and time of day, when the calendar is installed.
    bool has_date = false;
    time::Date date;
    std::uint8_t hour = 0;   // 0..23
    std::uint8_t minute = 0; // 0..59
    std::vector<UiEvent> ui_events;
};

} // namespace citysim::runner
