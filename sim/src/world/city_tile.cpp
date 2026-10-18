#include "citysim/world/city_tile.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace citysim::world {

CityTile::CityTile(TileCoord coord, std::uint64_t version, std::vector<float> heights)
    : coord_(coord), version_(version), heights_(std::move(heights)) {
    if (heights_.size() != k_sample_count) {
        throw std::invalid_argument("a city tile needs " + std::to_string(k_sample_count) + " heights, not " +
                                    std::to_string(heights_.size()));
    }
}

float CityTile::height_at(double local_x, double local_z) const {
    constexpr double k_last = static_cast<double>(k_city_tile_samples - 1);
    const double u = std::clamp(local_x / k_city_sample_spacing_m, 0.0, k_last);
    const double v = std::clamp(local_z / k_city_sample_spacing_m, 0.0, k_last);
    // The cell whose lower corner is at or below the point; the last sample row and column belong to the cell
    // before them, so a point on the far edge reads the edge samples exactly.
    const auto i = static_cast<std::uint32_t>(std::min(std::floor(u), k_last - 1.0));
    const auto j = static_cast<std::uint32_t>(std::min(std::floor(v), k_last - 1.0));
    const auto fu = static_cast<float>(u - static_cast<double>(i));
    const auto fv = static_cast<float>(v - static_cast<double>(j));
    // a * (1 - f) + b * f rather than a + (b - a) * f: at f = 1 it gives b exactly, so a point on a shared
    // edge reads the same height from both tiles.
    const float south = sample(i, j) * (1.0F - fu) + sample(i + 1, j) * fu;
    const float north = sample(i, j + 1) * (1.0F - fu) + sample(i + 1, j + 1) * fu;
    return south * (1.0F - fv) + north * fv;
}

} // namespace citysim::world
