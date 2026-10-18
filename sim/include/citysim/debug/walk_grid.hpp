#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace citysim::core {
class World;
}

namespace citysim::debug {

// Where the walking skeleton's agents may stand: a grid of square cells, each walkable or blocked, in the
// agents' ground plane (x, z) in meters, with the origin at the south-west corner of the grid (the city-local
// frame when the grid belongs to a city, architecture §15.3). The planner, the pathfinder and the movement
// system use only this interface, so they work on the flat test map (TestMap) and on real terrain
// (TerrainWalkGrid) alike. The cells are bit-packed in the base class and every accessor is inline, so the
// pathfinder's inner loop pays no virtual call.
//
// Cell (x, y) has index y * width + x; its centre is at ((x + 0.5) * cell_size, (y + 0.5) * cell_size).
// `version()` counts changes of the cells: the planner re-checks the paths of its agents when it changes.
class WalkGrid {
  public:
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    float cell_size() const { return cell_size_; }
    std::uint32_t cell_count() const { return width_ * height_; }
    // Counts the changes of the cells since the grid was made; not saved. Starts at 1 so that 0 can mean
    // "never looked".
    std::uint64_t version() const { return version_; }

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
    // Changes one cell and counts a change in version() if the cell was different.
    void set_walkable(std::uint32_t index, bool value);

    // The cell a ground position falls in, clamped to the grid.
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
    // For code that wrote through words(): counts one change.
    void touch() { ++version_; }

  protected:
    WalkGrid() = default;
    // `width` x `height` cells, all walkable (or all blocked).
    WalkGrid(std::uint32_t width, std::uint32_t height, float cell_size, bool walkable);
    ~WalkGrid() = default; // not meant to be deleted through a WalkGrid

    // Sets the size and resizes the cells, all walkable or all blocked.
    void reset(std::uint32_t width, std::uint32_t height, float cell_size, bool walkable);

  private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    float cell_size_ = 4.0F;
    std::uint64_t version_ = 1;
    std::vector<std::uint64_t> words_;
};

// The grid of the world, whichever kind it is: the TerrainWalkGrid if the world has one, else the TestMap,
// else null.
const WalkGrid* find_walk_grid(const core::World& world);

// A random walkable cell derived from `hash` (a counter-based rng result), or nullopt if the grid has none.
// Retries a bounded number of times with the same hash, so the answer depends only on `hash` and the grid.
std::optional<std::uint32_t> pick_walkable_cell(const WalkGrid& grid, std::uint64_t hash);

class TerrainWalkGrid;
// Like pick_walkable_cell, but only cells in the same region as `from` (TerrainWalkGrid::region_of) can be
// chosen, so the way there exists. The grid's regions must be current (update_regions). Nullopt if `from` is
// blocked. Depends only on `hash`, `from` and the grid.
std::optional<std::uint32_t> pick_walkable_cell_in_region(const TerrainWalkGrid& grid, std::uint32_t from,
                                                          std::uint64_t hash);

} // namespace citysim::debug
