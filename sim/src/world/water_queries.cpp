#include "citysim/world/water_queries.hpp"

#include <algorithm>
#include <cmath>

namespace citysim::world {

std::optional<float> water_level_at(const WorldTerrain& terrain, const WaterState& water, WorldPos pos) {
    const LoadedMap& map = *terrain.map();
    const RiverNetwork& rivers = map.rivers();
    std::optional<float> level;
    float nearest = 0.0F;
    if (!rivers.empty()) {
        rivers.for_each_near(pos, rivers.max_half_width_m(), [&](const ChannelSample& channel) {
            if (channel.distance_m > channel.half_width_m) {
                return;
            }
            // Strictly nearer wins; at equal distance the first visited, which depends only on the data.
            if (!level || channel.distance_m < nearest) {
                nearest = channel.distance_m;
                level = channel.level_m + water.level_offset(channel.reach);
            }
        });
    }
    if (level) {
        return level;
    }
    if (terrain.height_at(pos) <= k_sea_level_m) {
        return k_sea_level_m;
    }
    return std::nullopt;
}

float water_depth(const WorldTerrain& terrain, const WaterState& water, WorldPos pos) {
    const std::optional<float> level = water_level_at(terrain, water, pos);
    if (!level) {
        return 0.0F;
    }
    return std::max(0.0F, *level - terrain.height_at(pos));
}

bool is_water(const WorldTerrain& terrain, const WaterState& water, WorldPos pos) {
    return water_depth(terrain, water, pos) > 0.0F;
}

} // namespace citysim::world
