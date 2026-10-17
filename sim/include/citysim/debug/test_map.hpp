#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace citysim::debug {

// The flat test map of the walking skeleton (architecture §2 "Walking Skeleton"): a grid of square cells,
// each walkable or blocked. Singleton state in the World context, registered so it is hashed and saved.
// Throwaway gameplay, production engineering: it is what proves pathfinding, jobs, snapshots and saves work
// together.
//
// Cell (x, y) has index y * width + x. Ground coordinates: the cell's centre is at
// ((x + 0.5) * cell_size, (y + 0.5) * cell_size) in the simulation's (x, z) plane, in meters, with the world
// origin at the south-west corner (architecture §11).
class TestMap {
  public:
    TestMap() = default;
    // All cells walkable. `width` and `height` of 0 give an empty map.
    TestMap(std::uint32_t width, std::uint32_t height, float cell_size);

    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    float cell_size() const { return cell_size_; }
    std::uint32_t cell_count() const { return width_ * height_; }

    bool in_bounds(std::int64_t x, std::int64_t y) const {
        return x >= 0 && y >= 0 && x < static_cast<std::int64_t>(width_) &&
               y < static_cast<std::int64_t>(height_);
    }
    std::uint32_t index_of(std::uint32_t x, std::uint32_t y) const { return y * width_ + x; }
    std::uint32_t x_of(std::uint32_t index) const { return index % width_; }
    std::uint32_t y_of(std::uint32_t index) const { return index / width_; }

    bool walkable(std::uint32_t index) const { return ((words_[index >> 6U] >> (index & 63U)) & 1U) != 0; }
    // Out-of-bounds cells are blocked.
    bool walkable(std::int64_t x, std::int64_t y) const {
        return in_bounds(x, y) &&
               walkable(index_of(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)));
    }
    void set_walkable(std::uint32_t index, bool value);

    // The cell a ground position falls in, clamped to the map.
    std::uint32_t cell_at(float x, float z) const;
    float center_x(std::uint32_t index) const {
        return (static_cast<float>(x_of(index)) + 0.5F) * cell_size_;
    }
    float center_z(std::uint32_t index) const {
        return (static_cast<float>(y_of(index)) + 0.5F) * cell_size_;
    }

    // The bit-packed cells (1 = walkable), for hashing and saving.
    const std::vector<std::uint64_t>& words() const { return words_; }
    std::vector<std::uint64_t>& words() { return words_; }
    // Used when loading: sets the size and resizes the cells, leaving them all blocked.
    void resize_for_load(std::uint32_t width, std::uint32_t height, float cell_size);

  private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    float cell_size_ = 4.0F;
    std::vector<std::uint64_t> words_;
};

// A map with random rectangular obstacles. The same (seed, width, height) always gives the same map: it uses
// only citysim::rng. Obstacles are 2 to 10 cells on a side, about one per 600 cells (at least one).
TestMap make_test_map(std::uint64_t seed, std::uint32_t width, std::uint32_t height, float cell_size = 4.0F);

// A random walkable cell derived from `hash` (a counter-based rng result), or nullopt if the map has none.
// Retries a bounded number of times with the same hash, so the answer depends only on `hash` and the map.
std::optional<std::uint32_t> pick_walkable_cell(const TestMap& map, std::uint64_t hash);

} // namespace citysim::debug
