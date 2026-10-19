#include "citysim/world/terrain_mesh.hpp"

#include "citysim/world/loaded_map.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace citysim::world {

namespace {

constexpr std::int32_t k_last_sample = static_cast<std::int32_t>(k_city_tile_samples) - 1; // 512
constexpr float k_sample_m = static_cast<float>(k_city_sample_spacing_m);

// Where each LOD's chunks start in a tile's list: LOD 3 first (coarse to fine).
constexpr std::array<std::uint32_t, k_chunk_lod_count> k_level_offset = {21, 5, 1, 0};

// The surface a chunk at `lod` draws, at full-resolution sample (sx, sy): the coarse grid's triangles, split
// along the diagonal from the south-east to the north-west corner of each quad, as chunk_indices() does.
float coarse_height(const CityTile& tile, std::uint32_t lod, std::int32_t sx, std::int32_t sy) {
    const std::int32_t step = static_cast<std::int32_t>(chunk_step(lod));
    const std::int32_t x0 = std::min((sx / step) * step, k_last_sample - step);
    const std::int32_t y0 = std::min((sy / step) * step, k_last_sample - step);
    const float fx = static_cast<float>(sx - x0) / static_cast<float>(step);
    const float fy = static_cast<float>(sy - y0) / static_cast<float>(step);
    const auto at = [&](std::int32_t x, std::int32_t y) {
        return tile.sample(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    };
    const float a = at(x0, y0);
    const float b = at(x0 + step, y0);
    const float c = at(x0, y0 + step);
    const float d = at(x0 + step, y0 + step);
    if (fx + fy <= 1.0F) {
        return a + (b - a) * fx + (c - a) * fy;
    }
    return d + (c - d) * (1.0F - fx) + (b - d) * (1.0F - fy);
}

void normalize3(float& x, float& y, float& z) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length > 0.0F) {
        x /= length;
        y /= length;
        z /= length;
    }
}

// The border of a (quads + 1)^2 grid walked once around, counter-clockwise seen from above with z north
// (south edge west to east, east edge south to north, north edge east to west, west edge north to south), as
// grid indices. Each edge lists its two corners, so every skirt quad hangs from one edge segment.
std::vector<std::uint32_t> border_walk(std::uint32_t quads) {
    const std::uint32_t side = quads + 1;
    std::vector<std::uint32_t> walk;
    walk.reserve(4 * static_cast<std::size_t>(side));
    for (std::uint32_t i = 0; i < side; ++i) {
        walk.push_back(i); // south
    }
    for (std::uint32_t j = 0; j < side; ++j) {
        walk.push_back(j * side + quads); // east
    }
    for (std::uint32_t i = 0; i < side; ++i) {
        walk.push_back(quads * side + (quads - i)); // north
    }
    for (std::uint32_t j = 0; j < side; ++j) {
        walk.push_back((quads - j) * side); // west
    }
    return walk;
}

// Copies the border vertices into the skirt part of the arrays, `depth` lower.
void write_skirt(std::uint32_t quads, float depth, std::span<float> positions, std::span<float> normals,
                 std::span<float> colors) {
    const std::uint32_t side = quads + 1;
    const std::size_t grid = static_cast<std::size_t>(side) * side;
    const std::vector<std::uint32_t> walk = border_walk(quads);
    for (std::size_t k = 0; k < walk.size(); ++k) {
        const std::size_t from = walk[k];
        const std::size_t to = grid + k;
        for (std::size_t c = 0; c < 3; ++c) {
            positions[to * 3 + c] = positions[from * 3 + c];
            normals[to * 3 + c] = normals[from * 3 + c];
        }
        positions[to * 3 + 1] -= depth;
        for (std::size_t c = 0; c < 4; ++c) {
            colors[to * 4 + c] = colors[from * 4 + c];
        }
    }
}

void check_sizes(std::size_t vertices, std::span<float> positions, std::span<float> normals,
                 std::span<float> colors) {
    if (positions.size() < vertices * 3 || normals.size() < vertices * 3 || colors.size() < vertices * 4) {
        throw std::invalid_argument("terrain mesh: the output arrays are too small");
    }
}

float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

} // namespace

std::uint32_t chunk_index(ChunkKey key) {
    return k_level_offset[key.lod] + static_cast<std::uint32_t>(key.cy) * chunks_per_side(key.lod) + key.cx;
}

ChunkKey chunk_key(std::uint32_t index) {
    for (std::uint32_t lod = k_chunk_lod_count; lod-- > 0;) {
        const std::uint32_t n = chunks_per_side(lod);
        if (index < k_level_offset[lod] + n * n) {
            const std::uint32_t local = index - k_level_offset[lod];
            return ChunkKey{static_cast<std::uint8_t>(lod), static_cast<std::uint8_t>(local % n),
                            static_cast<std::uint8_t>(local / n)};
        }
    }
    throw std::out_of_range("chunk_key: index out of range");
}

TileHeights::TileHeights(const std::array<const CityTile*, 9>& neighbors, const LoadedMap* map)
    : tiles_(neighbors), center_(neighbors[4]), map_(map) {
    if (center_ == nullptr) {
        throw std::invalid_argument("TileHeights: the center tile is missing");
    }
}

float TileHeights::at(std::int32_t sx, std::int32_t sy) const {
    if (sx >= 0 && sy >= 0 && sx <= k_last_sample && sy <= k_last_sample) {
        return center_->sample(static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sy));
    }
    const std::int32_t nx = sx < 0 ? 0 : (sx > k_last_sample ? 2 : 1);
    const std::int32_t ny = sy < 0 ? 0 : (sy > k_last_sample ? 2 : 1);
    const CityTile* tile = tiles_[static_cast<std::size_t>(ny * 3 + nx)];
    if (tile != nullptr) {
        const std::int32_t lx = std::clamp(sx - (nx - 1) * k_last_sample, 0, k_last_sample);
        const std::int32_t ly = std::clamp(sy - (ny - 1) * k_last_sample, 0, k_last_sample);
        return tile->sample(static_cast<std::uint32_t>(lx), static_cast<std::uint32_t>(ly));
    }
    if (map_ != nullptr) {
        const WorldPos origin = tile_origin(center_->coord());
        const float height = map_->dem_height(
            WorldPos{origin.x + sx * k_city_sample_spacing_m, origin.z + sy * k_city_sample_spacing_m});
        if (std::isfinite(height)) {
            return height;
        }
    }
    return center_->sample(static_cast<std::uint32_t>(std::clamp(sx, 0, k_last_sample)),
                           static_cast<std::uint32_t>(std::clamp(sy, 0, k_last_sample)));
}

TileLodInfo analyze_tile(const CityTile& tile) {
    TileLodInfo info;
    // Lowest and highest ground: LOD 0 chunks from their samples (edges included), coarser ones from their
    // children.
    const std::uint32_t n0 = chunks_per_side(0);
    for (std::uint32_t cy = 0; cy < n0; ++cy) {
        for (std::uint32_t cx = 0; cx < n0; ++cx) {
            float low = std::numeric_limits<float>::max();
            float high = std::numeric_limits<float>::lowest();
            for (std::uint32_t sy = cy * k_chunk_quads; sy <= (cy + 1) * k_chunk_quads; ++sy) {
                for (std::uint32_t sx = cx * k_chunk_quads; sx <= (cx + 1) * k_chunk_quads; ++sx) {
                    const float h = tile.sample(sx, sy);
                    low = std::min(low, h);
                    high = std::max(high, h);
                }
            }
            const std::uint32_t index =
                chunk_index(ChunkKey{0, static_cast<std::uint8_t>(cx), static_cast<std::uint8_t>(cy)});
            info.min_height[index] = low;
            info.max_height[index] = high;
        }
    }
    for (std::uint32_t lod = 1; lod < k_chunk_lod_count; ++lod) {
        const std::uint32_t n = chunks_per_side(lod);
        for (std::uint32_t cy = 0; cy < n; ++cy) {
            for (std::uint32_t cx = 0; cx < n; ++cx) {
                const std::uint32_t index =
                    chunk_index(ChunkKey{static_cast<std::uint8_t>(lod), static_cast<std::uint8_t>(cx),
                                         static_cast<std::uint8_t>(cy)});
                float low = std::numeric_limits<float>::max();
                float high = std::numeric_limits<float>::lowest();
                for (std::uint32_t d = 0; d < 4; ++d) {
                    const std::uint32_t child = chunk_index(ChunkKey{
                        static_cast<std::uint8_t>(lod - 1), static_cast<std::uint8_t>(cx * 2 + (d & 1U)),
                        static_cast<std::uint8_t>(cy * 2 + (d >> 1U))});
                    low = std::min(low, info.min_height[child]);
                    high = std::max(high, info.max_height[child]);
                }
                info.min_height[index] = low;
                info.max_height[index] = high;

                // Geometric error: every full-resolution sample in the chunk against its coarse triangles.
                const std::uint32_t span = k_chunk_quads * chunk_step(lod);
                float error = 0.0F;
                for (std::uint32_t sy = cy * span; sy <= (cy + 1) * span; ++sy) {
                    for (std::uint32_t sx = cx * span; sx <= (cx + 1) * span; ++sx) {
                        const float diff = std::abs(tile.sample(sx, sy) -
                                                    coarse_height(tile, lod, static_cast<std::int32_t>(sx),
                                                                  static_cast<std::int32_t>(sy)));
                        error = std::max(error, diff);
                    }
                }
                info.error_m[index] = error;
            }
        }
    }
    return info;
}

bool TileChanges::any() const {
    return std::any_of(lod0.begin(), lod0.end(), [](bool changed) { return changed; });
}

bool TileChanges::chunk_changed(ChunkKey key) const {
    const std::uint32_t scale = chunk_step(key.lod);
    for (std::uint32_t dy = 0; dy < scale; ++dy) {
        for (std::uint32_t dx = 0; dx < scale; ++dx) {
            const std::uint32_t cx = key.cx * scale + dx;
            const std::uint32_t cy = key.cy * scale + dy;
            if (lod0[cy * chunks_per_side(0) + cx]) {
                return true;
            }
        }
    }
    return false;
}

TileChanges compare_tiles(const CityTile& before, const CityTile& after) {
    TileChanges changes;
    const std::span<const float> a = before.heights();
    const std::span<const float> b = after.heights();
    const std::int32_t n0 = static_cast<std::int32_t>(chunks_per_side(0));
    const std::int32_t quads = static_cast<std::int32_t>(k_chunk_quads);
    for (std::int32_t sy = 0; sy <= k_last_sample; ++sy) {
        for (std::int32_t sx = 0; sx <= k_last_sample; ++sx) {
            const std::size_t i =
                static_cast<std::size_t>(sy) * k_city_tile_samples + static_cast<std::size_t>(sx);
            if (a[i] == b[i]) {
                continue;
            }
            // Every chunk whose samples, or the ring of samples just outside it, include this one.
            // Chunk c covers samples [64c, 64c + 64] and needs [64c - 1, 64c + 65] for its normals.
            const std::int32_t cx0 = std::clamp((sx - 2) / quads, 0, n0 - 1);
            const std::int32_t cx1 = std::clamp((sx + 1) / quads, 0, n0 - 1);
            const std::int32_t cy0 = std::clamp((sy - 2) / quads, 0, n0 - 1);
            const std::int32_t cy1 = std::clamp((sy + 1) / quads, 0, n0 - 1);
            for (std::int32_t cy = cy0; cy <= cy1; ++cy) {
                for (std::int32_t cx = cx0; cx <= cx1; ++cx) {
                    changes.lod0[static_cast<std::size_t>(cy * n0 + cx)] = true;
                }
            }
        }
    }
    return changes;
}

float relief_shade(float slope_x, float slope_z) {
    // Light from the north-west (x west, z north) at 45 degrees, as the pipeline's hillshade previews.
    constexpr float k_light_x = -0.5F;
    constexpr float k_light_y = 0.70710678F;
    constexpr float k_light_z = 0.5F;
    float nx = -slope_x * k_relief_exaggeration;
    float ny = 1.0F;
    float nz = -slope_z * k_relief_exaggeration;
    normalize3(nx, ny, nz);
    const float lit = (nx * k_light_x + ny * k_light_y + nz * k_light_z) / k_light_y;
    return std::clamp(lit, 0.45F, 1.35F);
}

std::array<float, 4> terrain_color(float height_m, float slope_x, float slope_z) {
    const float slope = std::sqrt(slope_x * slope_x + slope_z * slope_z);
    // Placeholder palette until the splat shader (T1.6.1.2): mud below sea level, green low ground, olive and
    // then sand higher up, rock on steep slopes.
    const std::array<float, 3> mud = {0.36F, 0.33F, 0.26F};
    const std::array<float, 3> green = {0.30F, 0.45F, 0.22F};
    const std::array<float, 3> olive = {0.52F, 0.52F, 0.32F};
    const std::array<float, 3> sand = {0.80F, 0.70F, 0.50F};
    const std::array<float, 3> rock = {0.50F, 0.47F, 0.44F};
    std::array<float, 3> color = mud;
    const auto mix = [&](const std::array<float, 3>& to, float t) {
        for (std::size_t c = 0; c < 3; ++c) {
            color[c] += (to[c] - color[c]) * t;
        }
    };
    mix(green, smoothstep(-1.0F, 0.5F, height_m));
    mix(olive, smoothstep(30.0F, 70.0F, height_m));
    mix(sand, smoothstep(60.0F, 120.0F, height_m));
    mix(rock, smoothstep(0.25F, 0.6F, slope));
    const float shade = relief_shade(slope_x, slope_z);
    return {std::min(color[0] * shade, 1.0F), std::min(color[1] * shade, 1.0F),
            std::min(color[2] * shade, 1.0F), 1.0F};
}

std::pair<float, float> build_chunk_vertices(const TileHeights& heights, ChunkKey key, float skirt_depth_m,
                                             std::span<float> positions, std::span<float> normals,
                                             std::span<float> colors) {
    check_sizes(k_chunk_vertex_count, positions, normals, colors);
    const std::int32_t step = static_cast<std::int32_t>(chunk_step(key.lod));
    const std::int32_t sx0 = key.cx * static_cast<std::int32_t>(k_chunk_quads) * step;
    const std::int32_t sy0 = key.cy * static_cast<std::int32_t>(k_chunk_quads) * step;
    const float run = 2.0F * static_cast<float>(step) * k_sample_m;
    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();
    for (std::uint32_t j = 0; j < k_chunk_side_vertices; ++j) {
        for (std::uint32_t i = 0; i < k_chunk_side_vertices; ++i) {
            const std::int32_t sx = sx0 + static_cast<std::int32_t>(i) * step;
            const std::int32_t sy = sy0 + static_cast<std::int32_t>(j) * step;
            const float h = heights.at(sx, sy);
            const float slope_x = (heights.at(sx + step, sy) - heights.at(sx - step, sy)) / run;
            const float slope_z = (heights.at(sx, sy + step) - heights.at(sx, sy - step)) / run;
            const std::size_t v = static_cast<std::size_t>(j) * k_chunk_side_vertices + i;
            positions[v * 3 + 0] = static_cast<float>(sx) * k_sample_m;
            positions[v * 3 + 1] = h;
            positions[v * 3 + 2] = static_cast<float>(sy) * k_sample_m;
            float nx = -slope_x;
            float ny = 1.0F;
            float nz = -slope_z;
            normalize3(nx, ny, nz);
            normals[v * 3 + 0] = nx;
            normals[v * 3 + 1] = ny;
            normals[v * 3 + 2] = nz;
            const std::array<float, 4> color = terrain_color(h, slope_x, slope_z);
            std::copy(color.begin(), color.end(), colors.begin() + static_cast<std::ptrdiff_t>(v * 4));
            low = std::min(low, h);
            high = std::max(high, h);
        }
    }
    write_skirt(k_chunk_quads, skirt_depth_m, positions, normals, colors);
    return {low, high};
}

ChunkVertices build_chunk_vertices(const TileHeights& heights, ChunkKey key, float skirt_depth_m) {
    ChunkVertices out;
    out.positions.resize(static_cast<std::size_t>(k_chunk_vertex_count) * 3);
    out.normals.resize(static_cast<std::size_t>(k_chunk_vertex_count) * 3);
    out.colors.resize(static_cast<std::size_t>(k_chunk_vertex_count) * 4);
    const auto [low, high] =
        build_chunk_vertices(heights, key, skirt_depth_m, out.positions, out.normals, out.colors);
    out.min_y = low;
    out.max_y = high;
    return out;
}

std::vector<std::int32_t> patch_indices(std::uint32_t quads) {
    const std::uint32_t side = quads + 1;
    std::vector<std::int32_t> indices;
    indices.reserve(static_cast<std::size_t>(quads) * quads * 6 + 4 * static_cast<std::size_t>(quads) * 6);
    // Each quad as two triangles split from its south-east to its north-west corner, clockwise seen from
    // above (x east, z north, y up): (a, b, c) and (b, d, c) with a south-west, b south-east, c north-west, d
    // north-east.
    for (std::uint32_t j = 0; j < quads; ++j) {
        for (std::uint32_t i = 0; i < quads; ++i) {
            const auto a = static_cast<std::int32_t>(j * side + i);
            const std::int32_t b = a + 1;
            const auto c = static_cast<std::int32_t>(a + static_cast<std::int32_t>(side));
            const std::int32_t d = c + 1;
            indices.insert(indices.end(), {a, b, c, b, d, c});
        }
    }
    // The skirt: one quad below each border segment, facing outwards. The border walk goes counter-clockwise,
    // so outwards is to the right of the walk.
    const auto grid = static_cast<std::int32_t>(side * side);
    const std::vector<std::uint32_t> walk = border_walk(quads);
    for (std::uint32_t edge = 0; edge < 4; ++edge) {
        for (std::uint32_t k = 0; k < quads; ++k) {
            const std::uint32_t at = edge * side + k;
            const auto top0 = static_cast<std::int32_t>(walk[at]);
            const auto top1 = static_cast<std::int32_t>(walk[at + 1]);
            const auto low0 = grid + static_cast<std::int32_t>(at);
            const std::int32_t low1 = low0 + 1;
            indices.insert(indices.end(), {top0, low0, top1, top1, low0, low1});
        }
    }
    return indices;
}

const std::vector<std::int32_t>& chunk_indices() {
    static const std::vector<std::int32_t> indices = patch_indices(k_chunk_quads);
    return indices;
}

ChunkVertices build_horizon_patch(const LoadedMap& map, WorldPos origin, std::uint32_t quads, double step_m,
                                  float skirt_depth_m) {
    const std::uint32_t side = quads + 1;
    const std::size_t count = static_cast<std::size_t>(side) * side + 4 * static_cast<std::size_t>(side);
    ChunkVertices out;
    out.positions.resize(count * 3);
    out.normals.resize(count * 3);
    out.colors.resize(count * 4);
    // Heights on a grid one step wider on every side, for the normals at the border.
    const std::uint32_t wide = side + 2;
    std::vector<float> grid(static_cast<std::size_t>(wide) * wide);
    for (std::uint32_t j = 0; j < wide; ++j) {
        for (std::uint32_t i = 0; i < wide; ++i) {
            const float h = map.dem_height(WorldPos{origin.x + (static_cast<double>(i) - 1.0) * step_m,
                                                    origin.z + (static_cast<double>(j) - 1.0) * step_m});
            grid[static_cast<std::size_t>(j) * wide + i] = std::isfinite(h) ? h : 0.0F;
        }
    }
    const float run = static_cast<float>(2.0 * step_m);
    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();
    for (std::uint32_t j = 0; j < side; ++j) {
        for (std::uint32_t i = 0; i < side; ++i) {
            const auto g = [&](std::uint32_t x, std::uint32_t y) {
                return grid[static_cast<std::size_t>(y) * wide + x];
            };
            const float h = g(i + 1, j + 1);
            const float slope_x = (g(i + 2, j + 1) - g(i, j + 1)) / run;
            const float slope_z = (g(i + 1, j + 2) - g(i + 1, j)) / run;
            const std::size_t v = static_cast<std::size_t>(j) * side + i;
            out.positions[v * 3 + 0] = static_cast<float>(static_cast<double>(i) * step_m);
            out.positions[v * 3 + 1] = h;
            out.positions[v * 3 + 2] = static_cast<float>(static_cast<double>(j) * step_m);
            float nx = -slope_x;
            float ny = 1.0F;
            float nz = -slope_z;
            normalize3(nx, ny, nz);
            out.normals[v * 3 + 0] = nx;
            out.normals[v * 3 + 1] = ny;
            out.normals[v * 3 + 2] = nz;
            const std::array<float, 4> color = terrain_color(h, slope_x, slope_z);
            std::copy(color.begin(), color.end(), out.colors.begin() + static_cast<std::ptrdiff_t>(v * 4));
            low = std::min(low, h);
            high = std::max(high, h);
        }
    }
    write_skirt(quads, skirt_depth_m, out.positions, out.normals, out.colors);
    out.min_y = low;
    out.max_y = high;
    return out;
}

void select_chunks(const TileLodInfo& info, const LodCamera& camera,
                   const std::function<bool(std::uint32_t)>& ready, LodSelection& out) {
    out.draw.clear();
    out.wanted.clear();
    // Returns true if the chunk's square is fully covered by what it added to `draw`.
    const std::function<bool(ChunkKey, std::vector<std::uint32_t>&)> visit =
        [&](ChunkKey key, std::vector<std::uint32_t>& draw) -> bool {
        const std::uint32_t index = chunk_index(key);
        const double size = chunk_size_m(key.lod);
        const double x0 = key.cx * size;
        const double z0 = key.cy * size;
        const double dx = std::max({x0 - camera.x, 0.0, camera.x - (x0 + size)});
        const double dz = std::max({z0 - camera.z, 0.0, camera.z - (z0 + size)});
        const double dy = std::max({static_cast<double>(info.min_height[index]) - camera.y, 0.0,
                                    camera.y - static_cast<double>(info.max_height[index])});
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double error_px =
            static_cast<double>(info.error_m[index]) * camera.pixels_per_radian / std::max(distance, 1.0);
        const bool split = key.lod > camera.min_lod &&
                           (distance < camera.split_distance_factor * size || error_px > camera.max_error_px);
        if (split) {
            std::vector<std::uint32_t> children;
            bool covered = true;
            for (std::uint32_t d = 0; d < 4; ++d) {
                const ChunkKey child{static_cast<std::uint8_t>(key.lod - 1),
                                     static_cast<std::uint8_t>(key.cx * 2 + (d & 1U)),
                                     static_cast<std::uint8_t>(key.cy * 2 + (d >> 1U))};
                covered = visit(child, children) && covered;
            }
            if (covered) {
                draw.insert(draw.end(), children.begin(), children.end());
                return true;
            }
            // Not every child can be drawn yet: draw this chunk instead until they can.
            if (ready(index)) {
                draw.push_back(index);
                return true;
            }
            out.wanted.push_back(index);
            return false;
        }
        if (ready(index)) {
            draw.push_back(index);
            return true;
        }
        out.wanted.push_back(index);
        return false;
    };
    const ChunkKey root{static_cast<std::uint8_t>(k_chunk_lod_count - 1), 0, 0};
    visit(root, out.draw);
    // The root is what stands in for everything else while it loads, so it is always wanted first.
    if (!ready(chunk_index(root)) &&
        std::find(out.wanted.begin(), out.wanted.end(), chunk_index(root)) == out.wanted.end()) {
        out.wanted.push_back(chunk_index(root));
    }
}

std::optional<std::array<double, 3>>
raycast_heightfield(const std::array<double, 3>& origin, const std::array<double, 3>& direction,
                    double max_distance, double step_m,
                    const std::function<std::optional<float>(double x, double z)>& height) {
    const double length =
        std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
    if (!(length > 0.0) || !(step_m > 0.0) || !(max_distance > 0.0)) {
        return std::nullopt;
    }
    const std::array<double, 3> d = {direction[0] / length, direction[1] / length, direction[2] / length};
    const auto point = [&](double t) {
        return std::array<double, 3>{origin[0] + d[0] * t, origin[1] + d[1] * t, origin[2] + d[2] * t};
    };
    // Above (> 0) or below the ground at distance t; nothing where there is no ground.
    const auto above = [&](double t) -> std::optional<double> {
        const std::array<double, 3> p = point(t);
        const std::optional<float> h = height(p[0], p[2]);
        if (!h) {
            return std::nullopt;
        }
        return p[1] - static_cast<double>(*h);
    };
    std::optional<double> last_t;
    for (double t = 0.0; t <= max_distance; t += step_m) {
        const std::optional<double> gap = above(t);
        if (!gap) {
            last_t.reset();
            continue;
        }
        if (*gap <= 0.0) {
            if (!last_t) {
                // Already below the ground where the ground starts (the ray came in under the edge of the
                // active tiles): that edge is what it hits.
                std::array<double, 3> hit = point(t);
                hit[1] -= *gap; // onto the ground
                return hit;
            }
            double lo = *last_t;
            double hi = t;
            for (int i = 0; i < 40 && hi - lo > 1e-3; ++i) {
                const double mid = 0.5 * (lo + hi);
                const std::optional<double> g = above(mid);
                if (g && *g > 0.0) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            std::array<double, 3> hit = point(hi);
            if (const std::optional<float> h = height(hit[0], hit[2])) {
                hit[1] = static_cast<double>(*h);
            }
            return hit;
        }
        last_t = t;
    }
    return std::nullopt;
}

} // namespace citysim::world
