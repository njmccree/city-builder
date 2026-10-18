#pragma once

#include "citysim/time/calendar.hpp"
#include "citysim/world/tile_coord.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace citysim::world {
class CityTile;
}

namespace citysim::runner {

enum class UiEventKind { Message, SaveCompleted, LoadCompleted, StateHash };

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
    // Set by the runner on SaveCompleted / LoadCompleted / StateHash: these events stay in snapshots until
    // the consumer has read one, and latest_snapshot() hands each to the consumer once. 0 for other events.
    std::uint64_t serial = 0;
    std::vector<std::string> warnings; // LoadCompleted: e.g. the content changed since the save
    std::uint64_t hash = 0;            // StateHash: World::state_hash() at the moment it was taken
    std::uint64_t tick = 0;            // StateHash: the tick that state was at
};

// A named block of flat numbers for a renderer (architecture §7.3): `data` holds records of `stride` floats,
// for example 4 per agent (x, z, yaw, kind). Filled by publish hooks; a renderer looks its channel up by
// name. The layout of each channel is a contract between its hook and its renderer, documented where the hook
// is.
struct SnapshotChannel {
    std::string name;
    std::vector<float> data;
    std::uint32_t stride = 1;
    // Optional: one id per record (so ids.size() == record_count() when used), in the same order, so a
    // renderer can match records between two snapshots: things appear and disappear, so the index alone does
    // not identify them. Kept as integers beside the floats because a float cannot hold every 32-bit id.
    std::vector<std::uint32_t> ids;

    std::size_t record_count() const { return stride == 0 ? 0 : data.size() / stride; }
};

// A fixed 2D grid for a renderer, such as which cells of the test map are walkable. Big and rarely changing,
// so it is shared rather than copied: a publish hook keeps the same `cells` pointer until the grid changes,
// and a renderer rebuilds only when the pointer differs from the one it last saw.
struct SnapshotGrid {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float cell_size = 1.0F; // meters
    std::shared_ptr<const std::vector<std::uint8_t>>
        cells; // width * height values, row by row (y * width + x)
};

// One active 4 m city terrain tile for the terrain renderer (architecture §7.3, §15.4). The heights are
// shared, never copied: `tile` is the simulation's own immutable CityTile, and it is replaced by a new one
// (with a higher `version`) only when the tile changes, so a renderer rebuilds a tile's mesh only when its
// pointer differs from the one it last saw. A tile missing from the list is no longer active.
struct SnapshotTerrainTile {
    world::TileCoord coord;
    // CityTile::version(), for logs and debugging; compare pointers to detect a change.
    std::uint64_t version = 0;
    std::shared_ptr<const world::CityTile> tile;
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
    // Data for renderers, added by publish hooks. Cleared before every publish: a hook fills it each time.
    std::vector<SnapshotChannel> channels;
    // Shared grids for renderers, added by publish hooks. Cleared before every publish.
    std::vector<SnapshotGrid> grids;
    // "terrain.tiles": the active city terrain tiles sorted by coordinate, added by
    // world::publish_terrain_tiles when the world module is installed. Cleared before every publish.
    std::vector<SnapshotTerrainTile> terrain_tiles;

    // The channel with this name, or nullptr.
    const SnapshotChannel* find_channel(const std::string& name) const {
        for (const SnapshotChannel& channel : channels) {
            if (channel.name == name) {
                return &channel;
            }
        }
        return nullptr;
    }

    const SnapshotGrid* find_grid(const std::string& name) const {
        for (const SnapshotGrid& grid : grids) {
            if (grid.name == name) {
                return &grid;
            }
        }
        return nullptr;
    }
};

} // namespace citysim::runner
