#include "citysim/world/terrain_synth.hpp"

#include "citysim/profiling/profile.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace citysim::world {

std::shared_ptr<const CityTile> TerrainSynth::make_tile(const LoadedMap& map, TileCoord tile,
                                                        std::uint64_t map_seed) {
    CS_PROFILE_ZONE("world.synthesize_tile");
    (void)map_seed; // the placeholder has no micro-detail yet (T1.1.2.2)
    if (!map.contains_tile(tile)) {
        throw std::out_of_range("tile (" + std::to_string(tile.tx) + ", " + std::to_string(tile.ty) +
                                ") lies outside the map");
    }

    // Every sample of this tile lies in its closed square, so the detail tiles that can cover one are this
    // tile and its eight neighbors. Fetch them once instead of going through the shared cache per sample;
    // the result is the same as LoadedMap::dem_height() at each sample.
    std::array<std::shared_ptr<const Raster<float>>, 9> near;
    const auto slot = [&](TileCoord coord) {
        return (coord.ty - tile.ty + 1) * 3 + (coord.tx - tile.tx + 1);
    };
    for (std::int32_t dy = -1; dy <= 1; ++dy) {
        for (std::int32_t dx = -1; dx <= 1; ++dx) {
            const TileCoord coord{tile.tx + dx, tile.ty + dy};
            near[static_cast<std::size_t>(slot(coord))] = map.detail_tile(coord);
        }
    }

    const WorldPos origin = tile_origin(tile);
    std::vector<float> heights(CityTile::k_sample_count);
    for (std::uint32_t sy = 0; sy < k_city_tile_samples; ++sy) {
        for (std::uint32_t sx = 0; sx < k_city_tile_samples; ++sx) {
            // Exact in double: a multiple of 2,048 plus a multiple of 4.
            const WorldPos pos{origin.x + static_cast<double>(sx) * k_city_sample_spacing_m,
                               origin.z + static_cast<double>(sy) * k_city_sample_spacing_m};
            const LoadedMap::Candidates candidates = LoadedMap::covering_tiles(pos);
            float height = 0.0F;
            bool found = false;
            for (std::size_t c = 0; c < candidates.count && !found; ++c) {
                const TileCoord coord = candidates.tiles[c];
                const auto& detail = near[static_cast<std::size_t>(slot(coord))];
                if (detail) {
                    height = map.detail_value(*detail, coord, pos);
                    found = true;
                }
            }
            heights[static_cast<std::size_t>(sy) * k_city_tile_samples + sx] =
                found ? height : map.region_height(pos);
        }
    }
    return std::make_shared<const CityTile>(tile, 1, std::move(heights));
}

} // namespace citysim::world
