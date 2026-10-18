#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace citysim::world {

// The sea level in meters above the datum of the map's heights. Constant for now; a per-map offset is
// reserved (architecture §15.6).
inline constexpr float k_sea_level_m = 0.0F;

// How far each river reach's water surface is from its base level, in meters (architecture §15.6): positive
// when the river is in flood, negative in a dry year. One value per reach, in RiverNetwork reach order, 0 at
// the start. A World ctx() singleton registered as "world.Water", hashed and saved: it is the only water
// state that is, because everything else (the channels carved into the terrain, which land is flooded)
// follows from it and the river data.
//
// Clock: changes only by commands and by the river regimes (calendar time, T1.3.2.1), on the tick clock.
class WaterState {
  public:
    WaterState() = default;
    // `reach_count` reaches, all at their base level.
    explicit WaterState(std::size_t reach_count) : level_offsets_m_(reach_count, 0.0F) {}

    std::size_t reach_count() const { return level_offsets_m_.size(); }
    std::span<const float> level_offsets() const { return level_offsets_m_; }
    // The offset of a reach; 0 for an index that does not exist.
    float level_offset(std::uint32_t reach) const {
        return reach < level_offsets_m_.size() ? level_offsets_m_[reach] : 0.0F;
    }
    // Sets one offset. Throws std::out_of_range for an unknown reach and std::invalid_argument for a value
    // that is not finite. -0 is stored as 0 so equal states hash equally.
    void set_level_offset(std::uint32_t reach, float offset_m);
    // Starts again with `reach_count` reaches at their base level.
    void reset(std::size_t reach_count) { level_offsets_m_.assign(reach_count, 0.0F); }

    // For loading: replaces the offsets. Throws std::invalid_argument if one is not finite.
    void restore(std::vector<float> offsets);

    friend bool operator==(const WaterState&, const WaterState&) = default;

  private:
    std::vector<float> level_offsets_m_;
};

} // namespace citysim::world
