#pragma once

#include <compare>
#include <cstdint>

namespace citysim::world {

// A tile of the world's 2,048 m tile grid (architecture §15.1): tx counts tiles east and ty counts tiles
// north from the south-west corner of the map. Detail tiles in a map package and city tiles in the simulation
// both use it. Ordered by (ty, tx), the order tiles are listed and processed in, so loops over tiles are
// deterministic.
struct TileCoord {
    std::int32_t tx = 0;
    std::int32_t ty = 0;

    friend constexpr bool operator==(const TileCoord&, const TileCoord&) = default;
    friend constexpr std::strong_ordering operator<=>(const TileCoord& a, const TileCoord& b) {
        if (const auto order = a.ty <=> b.ty; order != 0) {
            return order;
        }
        return a.tx <=> b.tx;
    }
};

} // namespace citysim::world
