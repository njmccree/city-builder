#pragma once

#include "citysim/world/frames.hpp"
#include "citysim/world/tile_coord.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace citysim::world {

// One synthesized 4 m terrain tile (architecture §15.1, §15.4): 513 x 513 heights in meters, row-major with
// row 0 at the south edge, sample (sx, sy) at tile_origin(coord) + (sx * 4, sy * 4). Edge samples are shared
// with the neighbor and always equal to its matching edge.
//
// Immutable once made, and shared as std::shared_ptr<const CityTile> between the simulation and snapshots, so
// the renderer can tell a changed tile by its pointer (the SnapshotGrid pattern, §7.3). A change (a terrain
// edit, T1.1.2.3) makes a new CityTile with version + 1; a freshly synthesized tile has version 1. Never
// saved: the base heights are a function of the map package and the generator version. Not tied to a clock.
class CityTile {
  public:
    static constexpr std::size_t k_sample_count =
        static_cast<std::size_t>(k_city_tile_samples) * static_cast<std::size_t>(k_city_tile_samples);

    // Throws std::invalid_argument unless `heights` holds k_sample_count values.
    CityTile(TileCoord coord, std::uint64_t version, std::vector<float> heights);

    TileCoord coord() const { return coord_; }
    std::uint64_t version() const { return version_; }
    std::span<const float> heights() const { return heights_; }

    // The sample (sx, sy), both in [0, 512].
    float sample(std::uint32_t sx, std::uint32_t sy) const {
        return heights_[static_cast<std::size_t>(sy) * k_city_tile_samples + sx];
    }

    // Bilinear between samples, at a point given in meters from the tile's south-west corner. Coordinates are
    // clamped to the tile, [0, 2048].
    float height_at(double local_x, double local_z) const;

  private:
    TileCoord coord_;
    std::uint64_t version_ = 1;
    std::vector<float> heights_;
};

} // namespace citysim::world
