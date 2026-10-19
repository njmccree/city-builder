#include "citysim/world/resource_fields.hpp"

#include "citysim/core/rng.hpp"
#include "citysim/core/state_hasher.hpp"
#include "citysim/profiling/profile.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <span>
#include <stdexcept>
#include <zstd.h>

namespace citysim::world {

namespace {

constexpr double k_cell_m = 4.0;
constexpr std::int64_t k_tile_cells = ResourceFields::k_tile_cells;
constexpr std::int64_t k_area_cells = k_tile_cells * k_city_area_tiles;
// How much the seeded detail changes a value: the base is the region value times 1 +- this.
constexpr double k_detail_amplitude = 0.3;
// Value noise lattice spacings, in samples (64 m and 16 m), and their weights.
constexpr std::int64_t k_coarse_spacing = 16;
constexpr std::int64_t k_fine_spacing = 4;
constexpr double k_coarse_weight = 0.65;

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

// The one sample of a field on a tile that holds the global sample (gx, gz).
struct SampleAt {
    TileCoord tile;
    std::size_t index = 0;
};

SampleAt locate(std::int64_t gx, std::int64_t gz) {
    const std::int64_t tx = floor_div(gx, k_tile_cells);
    const std::int64_t tz = floor_div(gz, k_tile_cells);
    return SampleAt{
        TileCoord{static_cast<std::int32_t>(tx), static_cast<std::int32_t>(tz)},
        static_cast<std::size_t>((gz - tz * k_tile_cells) * k_tile_cells + (gx - tx * k_tile_cells))};
}

std::uint8_t clamp_u8(std::int32_t value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

} // namespace

ResourceFields::ResourceFields(std::shared_ptr<const LoadedMap> map, std::uint64_t map_seed,
                               std::vector<FieldInfo> fields)
    : map_(std::move(map)), map_seed_(map_seed), fields_(std::move(fields)) {
    for (const FieldInfo& field : fields_) {
        field_keys_.push_back(rng::stream(field.name));
        layers_.push_back(map_ != nullptr ? map_->region_layer(field.name) : nullptr);
    }
}

std::optional<FieldId> ResourceFields::find(std::string_view layer_or_resource_id) const {
    for (FieldId i = 0; i < fields_.size(); ++i) {
        if (fields_[i].name == layer_or_resource_id) {
            return i;
        }
        const auto& ids = fields_[i].resources;
        if (std::find(ids.begin(), ids.end(), layer_or_resource_id) != ids.end()) {
            return i;
        }
    }
    return std::nullopt;
}

namespace {

// The seeded value noise lattice: one number 0..1 at every lattice point of an octave.
double lattice_value(std::uint64_t map_seed, std::uint64_t field_key, std::uint64_t octave, std::int64_t i,
                     std::int64_t j) {
    static constexpr std::uint64_t k_stream = rng::stream("resource.detail");
    return rng::uniform_double01(rng::hash64(map_seed, k_stream, static_cast<std::uint64_t>(i),
                                             field_key + octave, static_cast<std::uint64_t>(j)));
}

// The detail noise 0..1 at a sample: two octaves of smoothly interpolated lattice values. `lattice(octave, i,
// j)` gives the value of a lattice point; the single-sample path computes it, the tile path reads it from a
// table made once, and both give the same bits.
template <class Lattice> double detail_noise(std::int64_t gx, std::int64_t gz, Lattice&& lattice) {
    const auto octave = [&](std::uint64_t which, std::int64_t spacing) {
        const std::int64_t i = floor_div(gx, spacing);
        const std::int64_t j = floor_div(gz, spacing);
        const double fx = static_cast<double>(gx - i * spacing) / static_cast<double>(spacing);
        const double fz = static_cast<double>(gz - j * spacing) / static_cast<double>(spacing);
        const double sx = fx * fx * (3.0 - 2.0 * fx);
        const double sz = fz * fz * (3.0 - 2.0 * fz);
        const double bottom = lattice(which, i, j) * (1.0 - sx) + lattice(which, i + 1, j) * sx;
        const double top = lattice(which, i, j + 1) * (1.0 - sx) + lattice(which, i + 1, j + 1) * sx;
        return bottom * (1.0 - sz) + top * sz;
    };
    return k_coarse_weight * octave(0, k_coarse_spacing) +
           (1.0 - k_coarse_weight) * octave(1, k_fine_spacing);
}

template <class Lattice>
std::uint8_t shaded_value(const LoadedMap& map, const Raster<float>& layer, std::int64_t gx, std::int64_t gz,
                          Lattice&& lattice) {
    const WorldPos pos{static_cast<double>(gx) * k_cell_m, static_cast<double>(gz) * k_cell_m};
    const float region = map.region_value(layer, pos);
    if (!(region > 0.0F)) {
        return 0;
    }
    const double factor = 1.0 + k_detail_amplitude * (2.0 * detail_noise(gx, gz, lattice) - 1.0);
    const double value = std::clamp(static_cast<double>(region) * factor, 0.0, 1.0);
    return clamp_u8(static_cast<std::int32_t>(std::floor(value * 255.0 + 0.5)));
}

} // namespace

std::uint8_t ResourceFields::base_value(FieldId field, std::int64_t gx, std::int64_t gz) const {
    const Raster<float>* layer = layers_[field];
    if (layer == nullptr || !map_->contains_tile(locate(gx, gz).tile)) {
        return 0;
    }
    const std::uint64_t key = field_keys_[field];
    return shaded_value(*map_, *layer, gx, gz, [&](std::uint64_t which, std::int64_t i, std::int64_t j) {
        return lattice_value(map_seed_, key, which, i, j);
    });
}

std::uint8_t ResourceFields::value_at(FieldId field, std::int64_t gx, std::int64_t gz) const {
    const std::int32_t base = base_value(field, gx, gz);
    const SampleAt at = locate(gx, gz);
    const auto it = deltas_.find(DeltaKey{fields_[field].name, at.tile});
    return clamp_u8(base + (it == deltas_.end() ? 0 : it->second.offsets[at.index]));
}

std::uint8_t ResourceFields::base_sample(FieldId field, WorldPos pos) const {
    if (field >= fields_.size() || !std::isfinite(pos.x) || !std::isfinite(pos.z)) {
        return 0;
    }
    return base_value(field, static_cast<std::int64_t>(std::floor(pos.x / k_cell_m + 0.5)),
                      static_cast<std::int64_t>(std::floor(pos.z / k_cell_m + 0.5)));
}

std::uint8_t ResourceFields::sample(FieldId field, WorldPos pos) const {
    if (field >= fields_.size() || !std::isfinite(pos.x) || !std::isfinite(pos.z)) {
        return 0;
    }
    return value_at(field, static_cast<std::int64_t>(std::floor(pos.x / k_cell_m + 0.5)),
                    static_cast<std::int64_t>(std::floor(pos.z / k_cell_m + 0.5)));
}

std::int32_t ResourceFields::add_delta(FieldId field, std::int64_t gx, std::int64_t gz, std::int32_t delta,
                                       bool above_base) {
    if (field >= fields_.size()) {
        throw std::out_of_range("ResourceFields::add_delta: no such field");
    }
    const std::int32_t base = base_value(field, gx, gz);
    const SampleAt at = locate(gx, gz);
    const DeltaKey key{fields_[field].name, at.tile};
    auto it = deltas_.find(key);
    const std::int32_t old_offset = it == deltas_.end() ? 0 : it->second.offsets[at.index];
    const std::int32_t current = std::clamp(base + old_offset, 0, 255);
    std::int32_t target = std::clamp(current + delta, 0, 255);
    if (delta > 0 && !above_base) {
        target = std::min(target, std::max(base, current)); // up to what the land had, never past it
    }
    const std::int32_t offset = target - base;
    if (offset == old_offset) {
        return 0;
    }
    if (it == deltas_.end()) {
        Layer layer;
        layer.offsets.assign(k_cells_per_tile, 0);
        it = deltas_.emplace(key, std::move(layer)).first;
    }
    Layer& layer = it->second;
    layer.nonzero += (offset != 0 ? 1U : 0U);
    layer.nonzero -= (old_offset != 0 ? 1U : 0U);
    layer.offsets[at.index] = static_cast<std::int16_t>(offset);
    if (layer.nonzero == 0) {
        deltas_.erase(it);
    }
    if (const auto cached_it = cache_.find(CacheKey{field, at.tile}); cached_it != cache_.end()) {
        cached_it->second.current_valid = false;
    }
    return target - current;
}

namespace {

// The samples of a round brush inside a city's area, in the order extraction and renewal visit them: south
// row first, west to east within a row. Calls fn(gx, gz) for each.
template <class Fn>
void for_each_brush_sample(const CityArea& city, LocalPos center, float radius_m, Fn&& fn) {
    if (!std::isfinite(center.x) || !std::isfinite(center.z) || !std::isfinite(radius_m) ||
        !(radius_m > 0.0F)) {
        return;
    }
    const WorldPos origin = city.origin();
    const double cx = origin.x + static_cast<double>(center.x);
    const double cz = origin.z + static_cast<double>(center.z);
    const double radius = static_cast<double>(radius_m);
    const auto area_x0 = static_cast<std::int64_t>(std::llround(origin.x / k_cell_m));
    const auto area_z0 = static_cast<std::int64_t>(std::llround(origin.z / k_cell_m));
    const std::int64_t gx_lo =
        std::max(static_cast<std::int64_t>(std::ceil((cx - radius) / k_cell_m)), area_x0);
    const std::int64_t gx_hi =
        std::min(static_cast<std::int64_t>(std::floor((cx + radius) / k_cell_m)), area_x0 + k_area_cells - 1);
    const std::int64_t gz_lo =
        std::max(static_cast<std::int64_t>(std::ceil((cz - radius) / k_cell_m)), area_z0);
    const std::int64_t gz_hi =
        std::min(static_cast<std::int64_t>(std::floor((cz + radius) / k_cell_m)), area_z0 + k_area_cells - 1);
    for (std::int64_t gz = gz_lo; gz <= gz_hi; ++gz) {
        for (std::int64_t gx = gx_lo; gx <= gx_hi; ++gx) {
            const double dx = static_cast<double>(gx) * k_cell_m - cx;
            const double dz = static_cast<double>(gz) * k_cell_m - cz;
            if (dx * dx + dz * dz < radius * radius) {
                fn(gx, gz);
            }
        }
    }
}

} // namespace

ResourceFields::Change ResourceFields::extract(FieldId field, const CityArea& city, LocalPos center,
                                               float radius_m, std::uint64_t amount) {
    CS_PROFILE_ZONE("world.resource_extract");
    Change change;
    for_each_brush_sample(city, center, radius_m, [&](std::int64_t gx, std::int64_t gz) {
        if (change.amount >= amount) {
            return;
        }
        const std::uint64_t want = amount - change.amount;
        const auto ask = static_cast<std::int32_t>(std::min<std::uint64_t>(want, 255));
        const std::int32_t removed = -add_delta(field, gx, gz, -ask);
        if (removed > 0) {
            change.amount += static_cast<std::uint64_t>(removed);
            ++change.samples;
        }
    });
    return change;
}

ResourceFields::Change ResourceFields::renew(FieldId field, const CityArea& city, LocalPos center,
                                             float radius_m, std::uint64_t amount, bool above_base) {
    CS_PROFILE_ZONE("world.resource_renew");
    Change change;
    for_each_brush_sample(city, center, radius_m, [&](std::int64_t gx, std::int64_t gz) {
        if (change.amount >= amount) {
            return;
        }
        const std::uint64_t want = amount - change.amount;
        const auto ask = static_cast<std::int32_t>(std::min<std::uint64_t>(want, 255));
        const std::int32_t added = add_delta(field, gx, gz, ask, above_base);
        if (added > 0) {
            change.amount += static_cast<std::uint64_t>(added);
            ++change.samples;
        }
    });
    return change;
}

std::shared_ptr<const ResourceFields::Grid> ResourceFields::make_base(FieldId field, TileCoord tile) const {
    const Raster<float>* layer = layers_[field];
    if (layer == nullptr || !map_->contains_tile(tile)) {
        return nullptr;
    }
    // Skip the tile when the region layer is 0 everywhere that can reach it (the cells under it and one more
    // around, because the values are bilinear between cell centres).
    const double cell = map_->manifest().region.cell_size_m;
    const WorldPos origin = tile_origin(tile);
    const auto first = [&](double meters) {
        return static_cast<std::int64_t>(std::floor(meters / cell)) - 1;
    };
    const auto last = [&](double meters) { return static_cast<std::int64_t>(std::floor(meters / cell)) + 1; };
    bool any = false;
    for (std::int64_t y = first(origin.z); y <= last(origin.z + k_tile_size_m) && !any; ++y) {
        for (std::int64_t x = first(origin.x); x <= last(origin.x + k_tile_size_m) && !any; ++x) {
            if (layer->in_bounds(x, y)) {
                const float value =
                    layer->value(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                any = value > 0.0F;
            }
        }
    }
    if (!any) {
        return nullptr;
    }
    auto grid = std::make_shared<Grid>(k_cells_per_tile, 0);
    const std::int64_t gx0 = static_cast<std::int64_t>(tile.tx) * k_tile_cells;
    const std::int64_t gz0 = static_cast<std::int64_t>(tile.ty) * k_tile_cells;

    // The lattice values of both octaves under the tile, made once (the single-sample path hashes them one by
    // one and gets the same numbers).
    struct Table {
        std::int64_t i0 = 0;
        std::int64_t j0 = 0;
        std::int64_t columns = 0;
        std::vector<double> values;
    };
    const std::uint64_t key = field_keys_[field];
    Table tables[2];
    const std::int64_t spacings[2] = {k_coarse_spacing, k_fine_spacing};
    for (std::uint64_t which = 0; which < 2; ++which) {
        Table& table = tables[which];
        const std::int64_t spacing = spacings[which];
        table.i0 = floor_div(gx0, spacing);
        table.j0 = floor_div(gz0, spacing);
        table.columns = floor_div(gx0 + k_tile_cells - 1, spacing) + 2 - table.i0;
        const std::int64_t rows = floor_div(gz0 + k_tile_cells - 1, spacing) + 2 - table.j0;
        table.values.resize(static_cast<std::size_t>(table.columns * rows));
        for (std::int64_t j = 0; j < rows; ++j) {
            for (std::int64_t i = 0; i < table.columns; ++i) {
                table.values[static_cast<std::size_t>(j * table.columns + i)] =
                    lattice_value(map_seed_, key, which, table.i0 + i, table.j0 + j);
            }
        }
    }
    const auto lattice = [&](std::uint64_t which, std::int64_t i, std::int64_t j) {
        const Table& table = tables[which];
        return table.values[static_cast<std::size_t>((j - table.j0) * table.columns + (i - table.i0))];
    };
    bool nonzero = false;
    for (std::int64_t sy = 0; sy < k_tile_cells; ++sy) {
        for (std::int64_t sx = 0; sx < k_tile_cells; ++sx) {
            const std::uint8_t value = shaded_value(*map_, *layer, gx0 + sx, gz0 + sy, lattice);
            (*grid)[static_cast<std::size_t>(sy * k_tile_cells + sx)] = value;
            nonzero = nonzero || value != 0;
        }
    }
    return nonzero ? grid : nullptr;
}

ResourceFields::Cached& ResourceFields::cached(FieldId field, TileCoord tile) {
    Cached& entry = cache_[CacheKey{field, tile}];
    if (!entry.base_made) {
        entry.base = make_base(field, tile);
        entry.base_made = true;
        entry.current_valid = false;
    }
    return entry;
}

void ResourceFields::refresh_current(FieldId field, TileCoord tile, Cached& entry) {
    if (entry.current_valid) {
        return;
    }
    const auto delta = deltas_.find(DeltaKey{fields_[field].name, tile});
    if (delta == deltas_.end()) {
        entry.current = entry.base;
    } else {
        auto grid = std::make_shared<Grid>(k_cells_per_tile, 0);
        bool nonzero = false;
        for (std::size_t i = 0; i < k_cells_per_tile; ++i) {
            const std::int32_t base = entry.base != nullptr ? (*entry.base)[i] : 0;
            const std::uint8_t value = clamp_u8(base + delta->second.offsets[i]);
            (*grid)[i] = value;
            nonzero = nonzero || value != 0;
        }
        entry.current = nonzero ? grid : nullptr;
    }
    entry.current_valid = true;
}

std::shared_ptr<const ResourceFields::Grid> ResourceFields::grid(FieldId field, TileCoord tile) {
    if (field >= fields_.size()) {
        return nullptr;
    }
    Cached& entry = cached(field, tile);
    refresh_current(field, tile, entry);
    return entry.current;
}

void ResourceFields::prepare(core::JobSystem& jobs, const std::vector<TileCoord>& tiles) {
    CS_PROFILE_ZONE("world.resource_prepare");
    const std::set<TileCoord> wanted(tiles.begin(), tiles.end());
    for (auto it = cache_.begin(); it != cache_.end();) {
        it = wanted.count(it->first.second) != 0 ? std::next(it) : cache_.erase(it);
    }
    struct Job {
        FieldId field;
        TileCoord tile;
        std::shared_ptr<const Grid> base;
    };
    std::vector<Job> missing;
    for (const TileCoord tile : wanted) { // sorted
        for (FieldId field = 0; field < fields_.size(); ++field) {
            const auto it = cache_.find(CacheKey{field, tile});
            if (it == cache_.end() || !it->second.base_made) {
                missing.push_back(Job{field, tile, nullptr});
            }
        }
    }
    jobs.parallel_for(missing.size(), 1, [&](std::size_t begin, std::size_t end, std::size_t) {
        for (std::size_t i = begin; i < end; ++i) {
            missing[i].base = make_base(missing[i].field, missing[i].tile);
        }
    });
    for (Job& job : missing) { // merged serially, in order
        Cached& entry = cache_[CacheKey{job.field, job.tile}];
        entry.base = std::move(job.base);
        entry.base_made = true;
        entry.current_valid = false;
    }
}

std::vector<SavedFieldDelta> ResourceFields::saved_state() const {
    std::vector<SavedFieldDelta> out;
    out.reserve(deltas_.size());
    for (const auto& [key, layer] : deltas_) { // sorted by field name, then tile
        out.push_back(SavedFieldDelta{key.first, key.second, layer.offsets});
    }
    return out;
}

void ResourceFields::restore(std::vector<SavedFieldDelta> saved) {
    std::map<DeltaKey, Layer> next;
    for (SavedFieldDelta& item : saved) {
        if (item.offsets.size() != k_cells_per_tile) {
            throw std::invalid_argument("a saved resource layer has the wrong size");
        }
        const bool known = std::any_of(fields_.begin(), fields_.end(),
                                       [&](const FieldInfo& field) { return field.name == item.field; });
        if (!known) {
            load_warnings_.push_back("the save has changes to the resource '" + item.field +
                                     "', which this game does not have; they were dropped");
            continue;
        }
        Layer layer;
        layer.nonzero = static_cast<std::size_t>(
            std::count_if(item.offsets.begin(), item.offsets.end(), [](std::int16_t v) { return v != 0; }));
        if (layer.nonzero == 0) {
            throw std::invalid_argument("a saved resource layer has no change in it");
        }
        layer.offsets = std::move(item.offsets);
        if (!next.emplace(DeltaKey{item.field, item.coord}, std::move(layer)).second) {
            throw std::invalid_argument("a saved resource layer is there twice");
        }
    }
    deltas_ = std::move(next);
    for (auto& entry : cache_) {
        entry.second.current_valid = false;
    }
}

void ResourceFields::hash(core::StateHasher& hasher) const {
    hasher.add_pod(static_cast<std::uint64_t>(deltas_.size()));
    for (const auto& [key, layer] : deltas_) {
        hasher.add(key.first);
        hasher.add_pod(key.second.tx);
        hasher.add_pod(key.second.ty);
        hasher.add(std::as_bytes(std::span<const std::int16_t>(layer.offsets)));
    }
}

bool operator==(const ResourceFields& a, const ResourceFields& b) {
    if (a.deltas_.size() != b.deltas_.size()) {
        return false;
    }
    auto ia = a.deltas_.begin();
    auto ib = b.deltas_.begin();
    for (; ia != a.deltas_.end(); ++ia, ++ib) {
        if (ia->first != ib->first || ia->second.offsets != ib->second.offsets) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> compress_resource_offsets(std::span<const std::int16_t> offsets) {
    if (offsets.size() != ResourceFields::k_cells_per_tile) {
        throw std::invalid_argument("a resource layer needs " +
                                    std::to_string(ResourceFields::k_cells_per_tile) + " offsets");
    }
    std::vector<std::uint8_t> raw(offsets.size() * 2);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const auto bits = static_cast<std::uint16_t>(offsets[i]);
        raw[i * 2] = static_cast<std::uint8_t>(bits & 0xFFU);
        raw[i * 2 + 1] = static_cast<std::uint8_t>(bits >> 8U);
    }
    std::vector<std::uint8_t> packed(ZSTD_compressBound(raw.size()));
    const std::size_t size = ZSTD_compress(packed.data(), packed.size(), raw.data(), raw.size(), 3);
    if (ZSTD_isError(size) != 0U) {
        throw std::runtime_error(std::string("zstd compression failed: ") + ZSTD_getErrorName(size));
    }
    packed.resize(size);
    return packed;
}

std::vector<std::int16_t> decompress_resource_offsets(std::span<const std::uint8_t> bytes) {
    constexpr std::size_t k_raw_bytes = ResourceFields::k_cells_per_tile * 2;
    const unsigned long long expected = ZSTD_getFrameContentSize(bytes.data(), bytes.size());
    if (expected != k_raw_bytes) {
        throw std::invalid_argument("a resource layer must decompress to exactly " +
                                    std::to_string(k_raw_bytes) + " bytes");
    }
    std::vector<std::uint8_t> raw(k_raw_bytes);
    const std::size_t size = ZSTD_decompress(raw.data(), raw.size(), bytes.data(), bytes.size());
    if (ZSTD_isError(size) != 0U || size != k_raw_bytes) {
        throw std::invalid_argument("a resource layer cannot be decompressed");
    }
    std::vector<std::int16_t> offsets(ResourceFields::k_cells_per_tile);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        offsets[i] =
            static_cast<std::int16_t>(static_cast<std::uint16_t>(raw[i * 2] | (raw[i * 2 + 1] << 8U)));
    }
    return offsets;
}

} // namespace citysim::world
