#pragma once

#include "citysim/core/job_system.hpp"
#include "citysim/world/frames.hpp"
#include "citysim/world/loaded_map.hpp"
#include "citysim/world/tile_coord.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace citysim::core {
class StateHasher;
}

namespace citysim::world {

// A resource field (architecture §15.8) is one number per 4 m sample of the land: how much of something is
// there. Fertility, vegetation and wetness always exist; every ResourceDef adds the field of its package
// layer (several definitions may share one, a "fertile soil" resource uses the fertility layer).
using FieldId = std::uint32_t;

struct FieldInfo {
    std::string name;                   // the package region layer: "fertility", "ore_copper", ...
    std::vector<std::string> resources; // ids of the ResourceDefs that use it, sorted
};

// One field's changes on one tile, as a save holds them.
struct SavedFieldDelta {
    std::string field;
    TileCoord coord;
    std::vector<std::int16_t> offsets; // ResourceFields::k_tile_cells squared

    friend bool operator==(const SavedFieldDelta&, const SavedFieldDelta&) = default;
};

// The resource fields of the land (architecture §15.8, same pattern as the terrain, §15.4):
//   - the **base** of a field is a pure function of the map package, the map seed and the position: the
//     package's region layer (bilinear, 1 km cells) times seeded value noise at 64 m and 16 m, rounded to
//     0..255 ("units"; 255 is the richest). It is never saved, and where the region layer is 0 it is 0, so a
//     deposit keeps the shape of its polygon. Detail comes from `rng` (stream "resource.detail", the map's
//     seed, not the game's), so every game on a map sees the same land.
//   - **deltas** are what the game did to it: depletion by extraction, damage, renewal. A sparse int16 layer
//     per (field, tile) and only for what was touched. Only deltas are hashed and saved; the value of a
//     sample is clamp(base + delta, 0, 255). Depletion persists.
// A tile holds 512 x 512 samples, sample (sx, sy) of tile (tx, ty) at world (tx * 2048 + 4 sx, ty * 2048 +
// 4 sy): unlike terrain heights, the fields have no shared edge, so every sample belongs to exactly one tile.
//
// Clock: tick (it changes only inside command handlers and systems that call extract / renew / add_delta).
// Single-threaded: the simulation thread and the publish hooks that run between its ticks use it, never
// two threads at once, except prepare(), which fills its caches in parallel and joins before it returns.
class ResourceFields {
  public:
    static constexpr std::uint32_t k_tile_cells = 512;
    static constexpr std::size_t k_cells_per_tile = static_cast<std::size_t>(k_tile_cells) * k_tile_cells;
    using Grid = std::vector<std::uint8_t>; // k_cells_per_tile values, row-major, row 0 at the south

    ResourceFields() = default;
    // `fields` is sorted by name (as install_resources makes it). `map` may be null: every base is then 0.
    ResourceFields(std::shared_ptr<const LoadedMap> map, std::uint64_t map_seed,
                   std::vector<FieldInfo> fields);

    const std::vector<FieldInfo>& fields() const { return fields_; }
    // The field with this layer name or this resource id.
    std::optional<FieldId> find(std::string_view layer_or_resource_id) const;

    // --- reading -------------------------------------------------------------------------------------------
    // The base value at a sample (a pure function of the inputs), and the value now (base + delta). The
    // sample is the one nearest the point. Both are 0 for a point off the map.
    std::uint8_t base_sample(FieldId field, WorldPos pos) const;
    std::uint8_t sample(FieldId field, WorldPos pos) const;
    std::uint8_t sample_local(FieldId field, const CityArea& city, LocalPos pos) const {
        return sample(field, to_world(city, pos));
    }

    // --- changing ------------------------------------------------------------------------------------------
    struct Change {
        std::uint64_t amount = 0; // units removed or added in total
        std::size_t samples = 0;  // samples that changed
    };
    // Removes up to `amount` units from the samples of the round brush (centre and radius in the city's local
    // frame) that lie inside the city's area, taking from each sample in turn, south row first and west to
    // east within a row, all it has before moving on, until the amount is used up or the brush is empty.
    // Returns what was removed: less than asked if the brush ran out. Depletion is permanent unless something
    // renews it.
    Change extract(FieldId field, const CityArea& city, LocalPos center, float radius_m,
                   std::uint64_t amount);
    // Puts up to `amount` units back on the same samples in the same order, each up to its base value (what
    // the land had before it was used), or, with `above_base`, up to the richest value 255 (silt that
    // enriches). Returns what was added.
    Change renew(FieldId field, const CityArea& city, LocalPos center, float radius_m, std::uint64_t amount,
                 bool above_base = false);
    // The primitive under both: changes one sample (global sample index gx, gz: x = gx * 4) by `delta`
    // units, clamped to 0..255 (and, for a positive delta without `above_base`, to the base). Returns the
    // change that was made.
    std::int32_t add_delta(FieldId field, std::int64_t gx, std::int64_t gz, std::int32_t delta,
                           bool above_base = false);

    // --- for overlays
    // ---------------------------------------------------------------------------------------- The current
    // values of a field on a tile, or null if they are all 0. The pointer changes only when the tile's values
    // do (base made, extraction, renewal), so a renderer can compare pointers (the SnapshotGrid pattern,
    // architecture §7.3).
    std::shared_ptr<const Grid> grid(FieldId field, TileCoord tile);
    // Makes the bases of every field on these tiles now, in parallel (one chunk per field and tile), and
    // forgets the caches of tiles not in the list; deltas are kept. Call it with the active tiles.
    void prepare(core::JobSystem& jobs, const std::vector<TileCoord>& tiles);

    // --- state: only the deltas
    // -----------------------------------------------------------------------------
    bool has_deltas() const { return !deltas_.empty(); }
    std::size_t delta_layers() const { return deltas_.size(); }
    // Sorted by field name, then tile.
    std::vector<SavedFieldDelta> saved_state() const;
    // Replaces all deltas (loading). Layers of fields this game does not have (the content changed) are
    // dropped with a warning that take_load_warnings() hands out. Throws std::invalid_argument for a layer of
    // the wrong size or a tile twice.
    void restore(std::vector<SavedFieldDelta> saved);
    std::vector<std::string> take_load_warnings() { return std::exchange(load_warnings_, {}); }
    // Field name, tile, every offset, in order.
    void hash(core::StateHasher& hasher) const;

    friend bool operator==(const ResourceFields& a, const ResourceFields& b);

  private:
    struct Layer {
        std::vector<std::int16_t> offsets; // k_cells_per_tile
        std::size_t nonzero = 0;
    };
    struct Cached {
        bool base_made = false;
        std::shared_ptr<const Grid> base;    // null: all zero
        std::shared_ptr<const Grid> current; // base + delta; null: all zero
        bool current_valid = false;          // false after the delta changed
    };
    using CacheKey = std::pair<FieldId, TileCoord>;
    using DeltaKey = std::pair<std::string, TileCoord>;

    std::shared_ptr<const Grid> make_base(FieldId field, TileCoord tile) const;
    Cached& cached(FieldId field, TileCoord tile);
    void refresh_current(FieldId field, TileCoord tile, Cached& entry);
    // The base of one sample, 0..255: a pure function (see the class comment).
    std::uint8_t base_value(FieldId field, std::int64_t gx, std::int64_t gz) const;
    // base + delta of one sample.
    std::uint8_t value_at(FieldId field, std::int64_t gx, std::int64_t gz) const;

    std::shared_ptr<const LoadedMap> map_;
    std::uint64_t map_seed_ = 0;
    std::vector<FieldInfo> fields_;
    std::vector<std::uint64_t> field_keys_;    // rng stream key of each field's noise
    std::vector<const Raster<float>*> layers_; // the region layer of each field, or null
    std::map<DeltaKey, Layer> deltas_;
    std::map<CacheKey, Cached> cache_;
    std::vector<std::string> load_warnings_;
};

// Offsets <-> bytes for saves: the int16 values little-endian, as one zstd frame. Throws
// std::invalid_argument for data that is not a frame of exactly one layer.
std::vector<std::uint8_t> compress_resource_offsets(std::span<const std::int16_t> offsets);
std::vector<std::int16_t> decompress_resource_offsets(std::span<const std::uint8_t> bytes);

} // namespace citysim::world
