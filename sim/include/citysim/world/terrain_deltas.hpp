#pragma once

#include "citysim/world/frames.hpp"
#include "citysim/world/tile_coord.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace citysim::core {
class StateHasher;
}

namespace citysim::world {

// A rectangle of samples of one city tile, both corners included: 0 <= x0 <= x1 <= 512 and the same for y.
struct SampleRect {
    std::uint32_t x0 = 0;
    std::uint32_t y0 = 0;
    std::uint32_t x1 = 0;
    std::uint32_t y1 = 0;

    static constexpr SampleRect whole_tile() {
        return SampleRect{0, 0, k_city_tile_samples - 1, k_city_tile_samples - 1};
    }
    constexpr bool valid() const {
        return x0 <= x1 && y0 <= y1 && x1 < k_city_tile_samples && y1 < k_city_tile_samples;
    }
    constexpr bool contains(std::uint32_t x, std::uint32_t y) const {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1;
    }

    friend constexpr bool operator==(const SampleRect&, const SampleRect&) = default;
};

// The terrain edits of one city tile (architecture §15.4): a height offset in meters for each of its
// 513 x 513 samples, added to the synthesized base. Only edited tiles have one (TerrainDeltas below).
class TileDelta {
  public:
    static constexpr std::size_t k_sample_count =
        static_cast<std::size_t>(k_city_tile_samples) * static_cast<std::size_t>(k_city_tile_samples);

    TileDelta() : offsets_(k_sample_count, 0.0F) {}
    // Takes ownership of `offsets` (k_sample_count finite values); the dirty rectangle is rebuilt from the
    // non-zero ones. Throws std::invalid_argument for a wrong size or a non-finite value.
    explicit TileDelta(std::vector<float> offsets);

    std::span<const float> offsets() const { return offsets_; }
    float offset(std::uint32_t sx, std::uint32_t sy) const {
        return offsets_[static_cast<std::size_t>(sy) * k_city_tile_samples + sx];
    }
    // How many samples have a non-zero offset. A layer with none is dropped (TerrainDeltas::prune).
    std::size_t nonzero_count() const { return nonzero_; }
    // The box around every sample whose offset has been non-zero since the layer was made or loaded: where a
    // renderer has to look. Not shrunk when an edit sets an offset back to zero. Empty until the first edit.
    const std::optional<SampleRect>& dirty() const { return dirty_; }

    // Sets one offset (finite; -0 is stored as 0 so equal states hash equally). Not range-checked beyond
    // that: TerrainDeltas::set_offset does it.
    void set(std::uint32_t sx, std::uint32_t sy, float offset);

  private:
    std::vector<float> offsets_;
    std::size_t nonzero_ = 0;
    std::optional<SampleRect> dirty_;
};

// A delta layer as a save holds it: every offset, uncompressed (the serializer compresses).
struct SavedTileDelta {
    TileCoord coord;
    std::vector<float> offsets;

    friend bool operator==(const SavedTileDelta&, const SavedTileDelta&) = default;
};

// All the terrain edits of a map, sparse: a layer exists only for a tile that was edited and still has a
// non-zero offset (architecture §15.4). Edits are kept for tiles that are not active and applied again when
// the tile is activated. Hashed and saved (zstd inside the WRLD block, see terrain_serialize.hpp); the base
// heights never are.
//
// Not tied to a clock: changes only when WorldTerrain applies an edit or restores a save.
class TerrainDeltas {
  public:
    bool empty() const { return tiles_.empty(); }
    std::size_t size() const { return tiles_.size(); }
    // The layer of a tile, or nullptr if it has none (the tile was never edited, or its edits cancelled out).
    const TileDelta* find(TileCoord tile) const;
    // Sorted by TileCoord.
    const std::map<TileCoord, TileDelta>& tiles() const { return tiles_; }

    // Sets one sample's offset, making the tile's layer on first use. Throws std::invalid_argument for a
    // sample outside [0, 512] or a non-finite offset.
    void set_offset(TileCoord tile, std::uint32_t sx, std::uint32_t sy, float offset);
    // Drops the layer if it no longer has a non-zero offset.
    void prune(TileCoord tile);
    void clear() { tiles_.clear(); }

    // Hashes the layers in TileCoord order: coordinate, then every offset.
    void hash(core::StateHasher& hasher) const;

    std::vector<SavedTileDelta> to_saved() const;
    // Throws std::invalid_argument for unsorted or repeated tiles, a layer of the wrong size or with a
    // non-finite value, or a layer with no non-zero offset.
    static TerrainDeltas from_saved(std::vector<SavedTileDelta> saved);

    friend bool operator==(const TerrainDeltas& a, const TerrainDeltas& b);

  private:
    std::map<TileCoord, TileDelta> tiles_;
};

// Offsets <-> bytes for saves: the floats' bit patterns, little-endian, as one zstd frame. Throws
// std::invalid_argument for data that is not a frame of exactly one layer.
std::vector<std::uint8_t> compress_offsets(std::span<const float> offsets);
std::vector<float> decompress_offsets(std::span<const std::uint8_t> bytes);

} // namespace citysim::world
