#include "citysim/debug/walk_grid.hpp"

#include "citysim/core/rng.hpp"
#include "citysim/core/world.hpp"
#include "citysim/debug/terrain_walk_grid.hpp"
#include "citysim/debug/test_map.hpp"

#include <algorithm>
#include <cmath>

namespace citysim::debug {

WalkGrid::WalkGrid(std::uint32_t width, std::uint32_t height, float cell_size, bool walkable) {
    reset(width, height, cell_size, walkable);
}

void WalkGrid::reset(std::uint32_t width, std::uint32_t height, float cell_size, bool walkable) {
    width_ = width;
    height_ = height;
    cell_size_ = cell_size;
    words_.assign((static_cast<std::size_t>(width) * height + 63U) / 64U, walkable ? ~std::uint64_t{0} : 0);
    ++version_;
}

void WalkGrid::set_walkable(std::uint32_t index, bool value) {
    const std::uint64_t bit = std::uint64_t{1} << (index & 63U);
    std::uint64_t& word = words_[index >> 6U];
    const bool was = (word & bit) != 0;
    if (was == value) {
        return;
    }
    word ^= bit;
    ++version_;
}

std::uint32_t WalkGrid::cell_at(float x, float z) const {
    const auto clamp_cell = [this](float coordinate, std::uint32_t limit) {
        const float cell = std::floor(coordinate / cell_size_);
        return static_cast<std::uint32_t>(std::clamp(cell, 0.0F, static_cast<float>(limit - 1)));
    };
    return index_of(clamp_cell(x, width_), clamp_cell(z, height_));
}

const WalkGrid* find_walk_grid(const core::World& world) {
    if (const auto* terrain = world.ctx().find<TerrainWalkGrid>(); terrain != nullptr) {
        return terrain;
    }
    return world.ctx().find<TestMap>();
}

std::optional<std::uint32_t> pick_walkable_cell(const WalkGrid& grid, std::uint64_t hash) {
    const std::uint32_t cells = grid.cell_count();
    if (cells == 0) {
        return std::nullopt;
    }
    constexpr std::uint64_t k_attempts = 32;
    for (std::uint64_t attempt = 0; attempt < k_attempts; ++attempt) {
        const std::uint64_t h = rng::hash64(hash, 1, 2, attempt, 3);
        const auto index = static_cast<std::uint32_t>(h % cells);
        if (grid.walkable(index)) {
            return index;
        }
    }
    // Dense obstacles: fall back to the first walkable cell after a hashed start, so a free cell is always
    // found.
    const auto start = static_cast<std::uint32_t>(hash % cells);
    for (std::uint32_t offset = 0; offset < cells; ++offset) {
        const std::uint32_t index = (start + offset) % cells;
        if (grid.walkable(index)) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t> pick_walkable_cell_in_region(const TerrainWalkGrid& grid, std::uint32_t from,
                                                          std::uint64_t hash) {
    const std::uint32_t region = grid.region_of(from);
    const std::uint32_t cells = grid.cell_count();
    if (region == 0 || cells == 0) {
        return std::nullopt;
    }
    constexpr std::uint64_t k_attempts = 32;
    for (std::uint64_t attempt = 0; attempt < k_attempts; ++attempt) {
        const std::uint64_t h = rng::hash64(hash, 1, 2, attempt, 3);
        const auto index = static_cast<std::uint32_t>(h % cells);
        if (grid.region_of(index) == region) {
            return index;
        }
    }
    // A small region: the first cell of it after a hashed start (it contains at least `from`).
    const auto start = static_cast<std::uint32_t>(hash % cells);
    for (std::uint32_t offset = 0; offset < cells; ++offset) {
        const std::uint32_t index = (start + offset) % cells;
        if (grid.region_of(index) == region) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace citysim::debug
