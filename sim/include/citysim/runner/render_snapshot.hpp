#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace citysim::runner {

// Something the UI should react to (a toast, a sound). Placeholder shape; real event kinds arrive with
// the UI tasks.
struct UiEvent {
    std::string text;
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
    std::vector<UiEvent> ui_events;
};

} // namespace citysim::runner
