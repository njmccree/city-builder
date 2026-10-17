#include "citysim/debug/test_map.hpp"

#include "citysim/core/rng.hpp"

#include <algorithm>
#include <cmath>

namespace citysim::debug {

TestMap::TestMap(std::uint32_t width, std::uint32_t height, float cell_size)
    : width_(width), height_(height), cell_size_(cell_size) {
    words_.assign((static_cast<std::size_t>(width) * height + 63U) / 64U, ~std::uint64_t{0});
}

void TestMap::resize_for_load(std::uint32_t width, std::uint32_t height, float cell_size) {
    width_ = width;
    height_ = height;
    cell_size_ = cell_size;
    words_.assign((static_cast<std::size_t>(width) * height + 63U) / 64U, 0);
}

void TestMap::set_walkable(std::uint32_t index, bool value) {
    const std::uint64_t bit = std::uint64_t{1} << (index & 63U);
    if (value) {
        words_[index >> 6U] |= bit;
    } else {
        words_[index >> 6U] &= ~bit;
    }
}

std::uint32_t TestMap::cell_at(float x, float z) const {
    const auto clamp_cell = [this](float coordinate, std::uint32_t limit) {
        const float cell = std::floor(coordinate / cell_size_);
        return static_cast<std::uint32_t>(std::clamp(cell, 0.0F, static_cast<float>(limit - 1)));
    };
    return index_of(clamp_cell(x, width_), clamp_cell(z, height_));
}

TestMap make_test_map(std::uint64_t seed, std::uint32_t width, std::uint32_t height, float cell_size) {
    TestMap map(width, height, cell_size);
    if (width == 0 || height == 0) {
        return map;
    }
    const std::uint64_t stream = rng::stream("debug.map");
    const std::uint64_t obstacles =
        std::max<std::uint64_t>(1, static_cast<std::uint64_t>(width) * height / 600);
    for (std::uint64_t i = 0; i < obstacles; ++i) {
        // Each number of the obstacle is its own counter-based draw, so nothing depends on draw order.
        const auto draw = [&](std::uint64_t part, std::int64_t lo, std::int64_t hi) {
            return rng::uniform_int(rng::hash64(seed, stream, 0, i, part), lo, hi);
        };
        const std::int64_t w = draw(0, 2, 10);
        const std::int64_t h = draw(1, 2, 10);
        const std::int64_t x0 = draw(2, 0, static_cast<std::int64_t>(width) - 1);
        const std::int64_t y0 = draw(3, 0, static_cast<std::int64_t>(height) - 1);
        for (std::int64_t y = y0; y < std::min<std::int64_t>(y0 + h, height); ++y) {
            for (std::int64_t x = x0; x < std::min<std::int64_t>(x0 + w, width); ++x) {
                map.set_walkable(map.index_of(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)),
                                 false);
            }
        }
    }
    return map;
}

std::optional<std::uint32_t> pick_walkable_cell(const TestMap& map, std::uint64_t hash) {
    const std::uint32_t cells = map.cell_count();
    if (cells == 0) {
        return std::nullopt;
    }
    constexpr std::uint64_t k_attempts = 32;
    for (std::uint64_t attempt = 0; attempt < k_attempts; ++attempt) {
        const std::uint64_t h = rng::hash64(hash, 1, 2, attempt, 3);
        const auto index = static_cast<std::uint32_t>(h % cells);
        if (map.walkable(index)) {
            return index;
        }
    }
    // Dense obstacles: fall back to the first walkable cell after a hashed start, so a free cell is always
    // found.
    const auto start = static_cast<std::uint32_t>(hash % cells);
    for (std::uint32_t offset = 0; offset < cells; ++offset) {
        const std::uint32_t index = (start + offset) % cells;
        if (map.walkable(index)) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace citysim::debug
