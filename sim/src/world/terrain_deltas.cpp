#include "citysim/world/terrain_deltas.hpp"

#include "citysim/core/state_hasher.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <string>
#include <zstd.h>

namespace citysim::world {

namespace {

constexpr int k_compression_level = 3;
constexpr std::size_t k_raw_bytes = TileDelta::k_sample_count * sizeof(float);

void extend(std::optional<SampleRect>& box, std::uint32_t sx, std::uint32_t sy) {
    if (!box) {
        box = SampleRect{sx, sy, sx, sy};
        return;
    }
    box->x0 = std::min(box->x0, sx);
    box->y0 = std::min(box->y0, sy);
    box->x1 = std::max(box->x1, sx);
    box->y1 = std::max(box->y1, sy);
}

} // namespace

TileDelta::TileDelta(std::vector<float> offsets) : offsets_(std::move(offsets)) {
    if (offsets_.size() != k_sample_count) {
        throw std::invalid_argument("a terrain delta layer needs " + std::to_string(k_sample_count) +
                                    " offsets");
    }
    for (std::size_t i = 0; i < offsets_.size(); ++i) {
        if (!std::isfinite(offsets_[i])) {
            throw std::invalid_argument("a terrain delta offset is not finite");
        }
        if (offsets_[i] == 0.0F) {
            offsets_[i] = 0.0F; // -0 becomes 0
        } else {
            ++nonzero_;
            extend(dirty_, static_cast<std::uint32_t>(i % k_city_tile_samples),
                   static_cast<std::uint32_t>(i / k_city_tile_samples));
        }
    }
}

void TileDelta::set(std::uint32_t sx, std::uint32_t sy, float offset) {
    if (!std::isfinite(offset)) {
        throw std::invalid_argument("a terrain delta offset is not finite");
    }
    if (offset == 0.0F) {
        offset = 0.0F;
    }
    float& slot = offsets_[static_cast<std::size_t>(sy) * k_city_tile_samples + sx];
    if (slot != 0.0F) {
        --nonzero_;
    }
    slot = offset;
    if (slot != 0.0F) {
        ++nonzero_;
        extend(dirty_, sx, sy);
    }
}

const TileDelta* TerrainDeltas::find(TileCoord tile) const {
    const auto it = tiles_.find(tile);
    return it == tiles_.end() ? nullptr : &it->second;
}

void TerrainDeltas::set_offset(TileCoord tile, std::uint32_t sx, std::uint32_t sy, float offset) {
    if (sx >= k_city_tile_samples || sy >= k_city_tile_samples) {
        throw std::invalid_argument("terrain sample (" + std::to_string(sx) + ", " + std::to_string(sy) +
                                    ") is outside the tile");
    }
    if (!std::isfinite(offset)) {
        throw std::invalid_argument("a terrain delta offset is not finite");
    }
    auto it = tiles_.find(tile);
    if (it == tiles_.end()) {
        if (offset == 0.0F) {
            return; // nothing to record, and no layer to make for it
        }
        it = tiles_.emplace(tile, TileDelta{}).first;
    }
    it->second.set(sx, sy, offset);
}

void TerrainDeltas::prune(TileCoord tile) {
    const auto it = tiles_.find(tile);
    if (it != tiles_.end() && it->second.nonzero_count() == 0) {
        tiles_.erase(it);
    }
}

void TerrainDeltas::hash(core::StateHasher& hasher) const {
    hasher.add_pod(static_cast<std::uint64_t>(tiles_.size()));
    for (const auto& [coord, layer] : tiles_) { // a std::map: sorted by TileCoord
        hasher.add_pod(coord.tx);
        hasher.add_pod(coord.ty);
        // Offsets are finite and never -0 (TileDelta::set), so their bytes are their value.
        hasher.add(std::as_bytes(layer.offsets()));
    }
}

std::vector<SavedTileDelta> TerrainDeltas::to_saved() const {
    std::vector<SavedTileDelta> out;
    out.reserve(tiles_.size());
    for (const auto& [coord, layer] : tiles_) {
        out.push_back(
            SavedTileDelta{coord, std::vector<float>(layer.offsets().begin(), layer.offsets().end())});
    }
    return out;
}

TerrainDeltas TerrainDeltas::from_saved(std::vector<SavedTileDelta> saved) {
    TerrainDeltas out;
    for (std::size_t i = 0; i < saved.size(); ++i) {
        if (i > 0 && !(saved[i - 1].coord < saved[i].coord)) {
            throw std::invalid_argument("the saved terrain deltas are not sorted or repeat a tile");
        }
        TileDelta layer(std::move(saved[i].offsets));
        if (layer.nonzero_count() == 0) {
            throw std::invalid_argument("a saved terrain delta layer is empty");
        }
        out.tiles_.emplace(saved[i].coord, std::move(layer));
    }
    return out;
}

bool operator==(const TerrainDeltas& a, const TerrainDeltas& b) {
    if (a.tiles_.size() != b.tiles_.size()) {
        return false;
    }
    for (auto x = a.tiles_.begin(), y = b.tiles_.begin(); x != a.tiles_.end(); ++x, ++y) {
        if (x->first != y->first || !std::equal(x->second.offsets().begin(), x->second.offsets().end(),
                                                y->second.offsets().begin())) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> compress_offsets(std::span<const float> offsets) {
    if (offsets.size() != TileDelta::k_sample_count) {
        throw std::invalid_argument("a terrain delta layer needs " +
                                    std::to_string(TileDelta::k_sample_count) + " offsets");
    }
    std::vector<std::uint8_t> raw(k_raw_bytes);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const auto bits = std::bit_cast<std::uint32_t>(offsets[i]);
        for (std::size_t b = 0; b < 4; ++b) {
            raw[i * 4 + b] = static_cast<std::uint8_t>(bits >> (8U * b));
        }
    }
    std::vector<std::uint8_t> packed(ZSTD_compressBound(raw.size()));
    const std::size_t size =
        ZSTD_compress(packed.data(), packed.size(), raw.data(), raw.size(), k_compression_level);
    if (ZSTD_isError(size) != 0U) {
        throw std::runtime_error(std::string("zstd compression failed: ") + ZSTD_getErrorName(size));
    }
    packed.resize(size);
    return packed;
}

std::vector<float> decompress_offsets(std::span<const std::uint8_t> bytes) {
    const unsigned long long expected = ZSTD_getFrameContentSize(bytes.data(), bytes.size());
    if (expected != k_raw_bytes) {
        throw std::invalid_argument("a terrain delta layer must decompress to exactly " +
                                    std::to_string(k_raw_bytes) + " bytes");
    }
    std::vector<std::uint8_t> raw(k_raw_bytes);
    const std::size_t size = ZSTD_decompress(raw.data(), raw.size(), bytes.data(), bytes.size());
    if (ZSTD_isError(size) != 0U || size != k_raw_bytes) {
        throw std::invalid_argument("a terrain delta layer cannot be decompressed");
    }
    std::vector<float> offsets(TileDelta::k_sample_count);
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        std::uint32_t bits = 0;
        for (std::size_t b = 0; b < 4; ++b) {
            bits |= static_cast<std::uint32_t>(raw[i * 4 + b]) << (8U * b);
        }
        offsets[i] = std::bit_cast<float>(bits);
    }
    return offsets;
}

} // namespace citysim::world
