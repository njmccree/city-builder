#pragma once

#include "citysim/world/city_tile.hpp"
#include "citysim/world/frames.hpp"
#include "citysim/world/tile_coord.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace citysim::world {

class LoadedMap;

// Render preparation for the city terrain (architecture §15.9): the chunk quadtree of a 4 m city tile, the
// geometric error of every chunk, the vertex data of a chunk, the coarse horizon patches beyond the active
// tiles, and a ray test against the active tiles. Plain C++ with no Godot types, so the GDExtension's
// TerrainRenderer stays a thin layer over it and the rules here have native tests. Everything is a pure
// function of immutable inputs (CityTile, LoadedMap), so it may run on any thread. Not tied to a clock.
//
// Chunks. Every chunk has 64 x 64 quads. A tile (512 x 512 quads) is a quadtree of 4 levels: LOD 0 has
// 8 x 8 chunks of 256 m at 4 m spacing, LOD 1 has 4 x 4 of 512 m at 8 m, LOD 2 has 2 x 2 of 1,024 m at 16 m,
// and LOD 3 is the whole tile at 32 m. A coarser chunk uses every 2^lod-th sample, so its vertices are a
// subset of the full grid. Every chunk carries a skirt (a strip hanging down from its border) that hides the
// cracks where it meets a chunk of another LOD or another tile.
inline constexpr std::uint32_t k_chunk_quads = 64;
inline constexpr std::uint32_t k_chunk_side_vertices = k_chunk_quads + 1;
inline constexpr std::uint32_t k_chunk_lod_count = 4;
inline constexpr std::uint32_t k_chunk_grid_vertices = k_chunk_side_vertices * k_chunk_side_vertices;
inline constexpr std::uint32_t k_chunk_skirt_vertices = 4 * k_chunk_side_vertices;
inline constexpr std::uint32_t k_chunk_vertex_count = k_chunk_grid_vertices + k_chunk_skirt_vertices;
inline constexpr std::uint32_t k_chunk_index_count =
    k_chunk_quads * k_chunk_quads * 6 + 4 * k_chunk_quads * 6;
// Chunks in one tile's quadtree: 64 + 16 + 4 + 1.
inline constexpr std::uint32_t k_tile_chunk_count = 85;

// A chunk of one tile: its LOD and its column and row in that LOD's grid (row 0 at the south).
struct ChunkKey {
    std::uint8_t lod = 0;
    std::uint8_t cx = 0;
    std::uint8_t cy = 0;
    friend constexpr bool operator==(const ChunkKey&, const ChunkKey&) = default;
};

// Chunks along one side of a tile at a LOD (8, 4, 2, 1).
constexpr std::uint32_t chunks_per_side(std::uint32_t lod) {
    return (k_city_tile_samples - 1) / k_chunk_quads >> lod;
}
// Samples between two vertices of a chunk at a LOD (1, 2, 4, 8).
constexpr std::uint32_t chunk_step(std::uint32_t lod) {
    return 1U << lod;
}
// The side of a chunk at a LOD, in meters (256 ... 2,048).
constexpr double chunk_size_m(std::uint32_t lod) {
    return static_cast<double>(k_chunk_quads * chunk_step(lod)) * k_city_sample_spacing_m;
}
// The chunk's place in a tile's list of 85 (coarse levels first: LOD 3 is 0, then LOD 2's 4, ...).
std::uint32_t chunk_index(ChunkKey key);
ChunkKey chunk_key(std::uint32_t index);

// One city tile with its eight neighbors, for the samples just beyond its edges that normals need. A
// neighbor that is not given is replaced by the map's 32 m DEM (LoadedMap::dem_height), or, without a map,
// by the nearest sample of the tile itself. The view only borrows: the tiles and the map must outlive it.
class TileHeights {
  public:
    // `neighbors` is the 3 x 3 block around the tile, row-major from the south-west, [4] being the tile
    // itself (which must not be null).
    TileHeights(const std::array<const CityTile*, 9>& neighbors, const LoadedMap* map);

    TileCoord coord() const { return center_->coord(); }
    const CityTile& tile() const { return *center_; }
    // The height of sample (sx, sy) of the tile, where -512 <= sx, sy <= 1024 reach into the neighbors.
    float at(std::int32_t sx, std::int32_t sy) const;

  private:
    std::array<const CityTile*, 9> tiles_;
    const CityTile* center_;
    const LoadedMap* map_;
};

// What the LOD selection needs of a tile: per chunk the lowest and highest ground (for its bounding box) and
// its geometric error, the largest vertical distance between the full 4 m surface and that chunk's coarser
// triangles. LOD 0 chunks have no error.
struct TileLodInfo {
    std::array<float, k_tile_chunk_count> min_height{};
    std::array<float, k_tile_chunk_count> max_height{};
    std::array<float, k_tile_chunk_count> error_m{};
};
TileLodInfo analyze_tile(const CityTile& tile);

// Which LOD 0 chunks (8 x 8, row-major from the south-west) differ between two versions of a tile, counting a
// change within one sample of a chunk's border as a change of that chunk (its normals move). Every chunk
// above a changed LOD 0 chunk has changed too: chunk_changed() answers for any LOD.
struct TileChanges {
    std::array<bool, 64> lod0{};
    bool any() const;
    bool chunk_changed(ChunkKey key) const;
};
TileChanges compare_tiles(const CityTile& before, const CityTile& after);

// The vertex data of one chunk, in meters relative to the tile's south-west corner (x east, y up, z north).
// Normals are unit length, from central differences over the chunk's own vertex spacing (so a coarse chunk is
// lit like the surface it draws). Colors are the placeholder palette by height and slope (RGBA, 0..1).
struct ChunkVertices {
    std::vector<float> positions; // 3 per vertex
    std::vector<float> normals;   // 3 per vertex
    std::vector<float> colors;    // 4 per vertex
    float min_y = 0.0F;
    float max_y = 0.0F;
};
// Writes k_chunk_vertex_count vertices into the spans (3, 3 and 4 floats each). The first 65 x 65 are the
// grid (row-major from the south-west), then the skirt: the south, east, north and west edges, each 65
// vertices `skirt_depth_m` below the edge vertex they hang from. Returns the lowest and highest grid height.
std::pair<float, float> build_chunk_vertices(const TileHeights& heights, ChunkKey key, float skirt_depth_m,
                                             std::span<float> positions, std::span<float> normals,
                                             std::span<float> colors);
ChunkVertices build_chunk_vertices(const TileHeights& heights, ChunkKey key, float skirt_depth_m);
// The triangle list every chunk shares (k_chunk_index_count indices), wound clockwise seen from above, the
// winding Godot treats as the front face.
const std::vector<std::int32_t>& chunk_indices();

// The placeholder terrain color (RGBA, 0..1) for a height in meters and a slope (rise over run).
std::array<float, 4> terrain_color(float height_m, float slope);

// Choosing the chunks to draw: a chunk is split into its four children while its geometric error, seen from
// the camera, covers more than `max_error_px` pixels, or while the camera is closer than
// `split_distance_factor` times its size; never below `min_lod`. `pixels_per_radian` is the viewport height
// divided by 2 tan(fov / 2). The camera is in the tile's own frame (meters from its south-west corner).
struct LodCamera {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double pixels_per_radian = 700.0;
    double max_error_px = 2.0;
    double split_distance_factor = 1.5;
    std::uint32_t min_lod = 0;
};
// The chunks to draw for one tile, covering it exactly once. `ready(index)` says whether a chunk's mesh
// exists; where a split is wanted but a child is not ready, the parent is drawn instead (if it is ready), and
// the missing chunks are listed in `wanted` so they get built. If nothing covers part of the tile, that part
// is not drawn (the root is always wanted). Chunks come out as indices (chunk_index).
struct LodSelection {
    std::vector<std::uint32_t> draw;
    std::vector<std::uint32_t> wanted;
};
void select_chunks(const TileLodInfo& info, const LodCamera& camera,
                   const std::function<bool(std::uint32_t)>& ready, LodSelection& out);

// A coarse square of ground beyond the active tiles (the horizon): `quads` x `quads` quads of `step_m`
// starting at the world point `origin`, with heights from LoadedMap::dem_height (the 32 m DEM inside the
// detail regions, the region raster elsewhere), plus a skirt `skirt_depth_m` deep. Positions are relative to
// `origin` (x east, y up, z north). Same vertex order and colors as a chunk.
ChunkVertices build_horizon_patch(const LoadedMap& map, WorldPos origin, std::uint32_t quads, double step_m,
                                  float skirt_depth_m);
// The triangle list of such a patch (clockwise from above), for `quads` per side.
std::vector<std::int32_t> patch_indices(std::uint32_t quads);

// The first point where the ray origin + t * direction (t > 0, at most `max_distance`) meets the ground given
// by `height(x, z)`, which returns nothing where there is no ground to hit (outside the active tiles).
// Marches in steps of `step_m`, then refines by bisection to a few millimeters. Returns {x, y, z} or nothing.
std::optional<std::array<double, 3>>
raycast_heightfield(const std::array<double, 3>& origin, const std::array<double, 3>& direction,
                    double max_distance, double step_m,
                    const std::function<std::optional<float>(double x, double z)>& height);

} // namespace citysim::world
