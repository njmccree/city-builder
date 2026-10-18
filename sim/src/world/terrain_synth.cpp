#include "citysim/world/terrain_synth.hpp"

#include "citysim/core/rng.hpp"
#include "citysim/profiling/profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace citysim::world {

namespace {

constexpr std::uint32_t k_samples = k_city_tile_samples;
// The slope that sets the roughness needs the heights one sample beyond each edge, so the base surface is
// computed for sample indices -1 .. 513 (a function of world position, so tiles still agree at their edges).
constexpr std::uint32_t k_base_side = k_samples + 2;
constexpr double k_inv_two_sample_spacing = 1.0 / (2.0 * k_city_sample_spacing_m);

// Micro-detail octaves: lattice spacings in meters. All divide 2,048, so lattice points fall on tile borders
// and a tile's lattice is closed by its neighbors' (see lattice_value).
constexpr std::array<double, 5> k_octave_spacing_m{128.0, 64.0, 32.0, 16.0, 8.0};
constexpr std::array<double, 5> k_octave_weight{1.0, 0.5, 0.25, 0.125, 0.0625};
constexpr double k_octave_weight_sum = 1.0 + 0.5 + 0.25 + 0.125 + 0.0625;

// Detail amplitude in meters: at most 0.3 m on flat, fertile ground and up to 3 m on rough, barren ground.
constexpr double k_min_amplitude_m = 0.3;
constexpr double k_max_amplitude_m = 3.0;

double clamp01(double v) {
    return std::clamp(v, 0.0, 1.0);
}
double smoothstep(double edge0, double edge1, double x) {
    const double t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

// Catmull-Rom weights for the four nodes around a point at fraction u (0..1) of the middle cell. At u = 0 the
// weights are exactly (0, 1, 0, 0), so the surface passes through the nodes.
std::array<double, 4> catmull_rom(double u) {
    const double u2 = u * u;
    const double u3 = u2 * u;
    return {-0.5 * u3 + u2 - 0.5 * u, 1.5 * u3 - 2.5 * u2 + 1.0, -1.5 * u3 + 2.0 * u2 + 0.5 * u,
            0.5 * u3 - 0.5 * u2};
}

// For every sample column (or row) of the base surface: the first of the four nodes it blends, and their
// weights. `first` is an index into the tile's node grid, whose index 0 is node -2.
struct AxisWeights {
    std::vector<int> first;
    std::vector<std::array<double, 4>> weight;
};

AxisWeights make_axis_weights(double spacing_m) {
    AxisWeights axis;
    axis.first.resize(k_base_side);
    axis.weight.resize(k_base_side);
    for (std::uint32_t i = 0; i < k_base_side; ++i) {
        const double local_m = (static_cast<double>(i) - 1.0) * k_city_sample_spacing_m;
        const double cell = std::floor(local_m / spacing_m);
        axis.first[i] = static_cast<int>(cell) - 1 + 2; // nodes cell-1 .. cell+2, node -2 at index 0
        axis.weight[i] = catmull_rom((local_m - cell * spacing_m) / spacing_m);
    }
    return axis;
}

// The value (0..1) of one lattice point of one octave. A point on a tile's east or north border belongs to
// the neighbor, which is where the tile's own hash input comes from: that is what makes both tiles read the
// same number. `a` and `b` are lattice indices inside the closed tile, 0 .. points - 1, where the last is the
// border.
double lattice_value(std::uint64_t map_seed, std::uint64_t stream, TileCoord tile, std::size_t octave,
                     std::uint32_t a, std::uint32_t b, std::uint32_t cells) {
    TileCoord owner = tile;
    if (a == cells) {
        a = 0;
        ++owner.tx;
    }
    if (b == cells) {
        b = 0;
        ++owner.ty;
    }
    const std::uint64_t n =
        (static_cast<std::uint64_t>(octave) << 40U) | (static_cast<std::uint64_t>(b) << 20U) | a;
    return rng::uniform_double01(
        rng::hash64(map_seed, stream, static_cast<std::uint64_t>(static_cast<std::int64_t>(owner.tx)),
                    static_cast<std::uint64_t>(static_cast<std::int64_t>(owner.ty)), n));
}

// One octave's lattice over a tile and how to read it at a sample column or row.
struct Octave {
    std::uint32_t cells = 0;   // cells along a side; the lattice has cells + 1 points
    std::vector<float> values; // (cells + 1)^2, row b, column a
    std::vector<int> cell;     // per sample index 0..512: the lattice cell it lies in
    std::vector<double> t;     // and its smoothed position in that cell
};

// The height of the ground after the river channels near `pos` are carved into it. `segments` are the
// segments that can reach this point (RiverNetwork::segments_near_box), by index.
float carve_channels(const RiverNetwork& rivers, std::span<const std::uint32_t> segments, WorldPos pos,
                     const TerrainDefines& defines, float ground) {
    const double edge_fraction = static_cast<double>(defines.channel_edge_depth_fraction);
    const double bank = static_cast<double>(defines.bank_width_m);
    float result = ground;
    for (const std::uint32_t index : segments) {
        const ChannelSample channel = rivers.measure_segment(index, pos);
        const double half = static_cast<double>(channel.half_width_m);
        const double distance = static_cast<double>(channel.distance_m);
        if (distance >= half + bank) {
            continue;
        }
        const double level = static_cast<double>(channel.level_m);
        const double depth = static_cast<double>(channel.depth_m);
        double bed = 0.0;
        if (distance <= half) {
            const double across = distance / half;
            bed = level - depth * (1.0 - (1.0 - edge_fraction) * across * across);
        } else {
            const double edge_bed = level - depth * edge_fraction;
            const double t = (distance - half) / bank;
            bed = edge_bed + (static_cast<double>(ground) - edge_bed) * (t * t * (3.0 - 2.0 * t));
        }
        result = std::min(result, static_cast<float>(bed));
    }
    return result;
}

} // namespace

std::shared_ptr<const CityTile> TerrainSynth::make_tile(const LoadedMap& map, TileCoord tile,
                                                        std::uint64_t map_seed,
                                                        const TerrainDefines& defines) {
    CS_PROFILE_ZONE("world.synthesize_tile");
    if (!map.contains_tile(tile)) {
        throw std::out_of_range("tile (" + std::to_string(tile.tx) + ", " + std::to_string(tile.ty) +
                                ") lies outside the map");
    }
    const double spacing_m = map.detail_spacing_m();
    const int nodes_per_side = static_cast<int>(std::lround(k_tile_size_m / spacing_m)); // 64 for 32 m
    if (!(spacing_m > 0.0) ||
        std::abs(static_cast<double>(nodes_per_side) * spacing_m - k_tile_size_m) > 1e-9) {
        throw std::logic_error("the detail spacing must divide the 2,048 m tile");
    }

    // 1. The 32 m DEM nodes around this tile: indices -2 .. nodes_per_side + 2 on both axes (the base
    // surface's
    //    4 x 4 Catmull-Rom neighborhoods reach that far). Every node is a function of its world position, so
    //    the tiles that share a node read the same number. The detail tiles that can hold one are this tile
    //    and its eight neighbors: fetch them once instead of going through the shared cache per node.
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
    const int grid = nodes_per_side + 5;
    std::vector<float> nodes(static_cast<std::size_t>(grid) * static_cast<std::size_t>(grid));
    for (int nj = 0; nj < grid; ++nj) {
        for (int ni = 0; ni < grid; ++ni) {
            const WorldPos pos{origin.x + static_cast<double>(ni - 2) * spacing_m,
                               origin.z + static_cast<double>(nj - 2) * spacing_m};
            const LoadedMap::Candidates candidates = LoadedMap::covering_tiles(pos);
            float height = 0.0F;
            bool found = false;
            for (std::size_t c = 0; c < candidates.count && !found; ++c) {
                const TileCoord coord = candidates.tiles[c];
                if (std::abs(coord.tx - tile.tx) > 1 || std::abs(coord.ty - tile.ty) > 1) {
                    continue;
                }
                const auto& detail = near[static_cast<std::size_t>(slot(coord))];
                if (detail) {
                    height = map.detail_value(*detail, coord, pos);
                    found = true;
                }
            }
            nodes[static_cast<std::size_t>(nj) * static_cast<std::size_t>(grid) +
                  static_cast<std::size_t>(ni)] = found ? height : map.region_height(pos);
        }
    }

    // 2. The base surface: bicubic (Catmull-Rom) through the nodes, at the 4 m samples -1 .. 513. First along
    //    x for every node row, then along y.
    const AxisWeights axis = make_axis_weights(spacing_m);
    std::vector<double> along_x(static_cast<std::size_t>(grid) * k_base_side);
    for (int nj = 0; nj < grid; ++nj) {
        for (std::uint32_t i = 0; i < k_base_side; ++i) {
            const float* row = &nodes[static_cast<std::size_t>(nj) * static_cast<std::size_t>(grid)];
            const int first = axis.first[i];
            const auto& w = axis.weight[i];
            along_x[static_cast<std::size_t>(nj) * k_base_side + i] =
                w[0] * static_cast<double>(row[first]) + w[1] * static_cast<double>(row[first + 1]) +
                w[2] * static_cast<double>(row[first + 2]) + w[3] * static_cast<double>(row[first + 3]);
        }
    }
    std::vector<float> base(static_cast<std::size_t>(k_base_side) * k_base_side);
    for (std::uint32_t j = 0; j < k_base_side; ++j) {
        const int first = axis.first[j];
        const auto& w = axis.weight[j];
        for (std::uint32_t i = 0; i < k_base_side; ++i) {
            const auto at = [&](int node_row) {
                return along_x[static_cast<std::size_t>(first + node_row) * k_base_side + i];
            };
            base[static_cast<std::size_t>(j) * k_base_side + i] =
                static_cast<float>(w[0] * at(0) + w[1] * at(1) + w[2] * at(2) + w[3] * at(3));
        }
    }

    // 3. Micro-detail: value noise at five scales, seeded by the map seed and the tile that owns each lattice
    //    point (never the game seed). Lattice spacings divide the tile, so the lattice of a tile ends where
    //    its neighbor's begins and the noise is a smooth function of world position.
    const std::uint64_t stream = rng::stream("terrain.detail");
    std::array<Octave, k_octave_spacing_m.size()> octaves;
    for (std::size_t o = 0; o < octaves.size(); ++o) {
        Octave& octave = octaves[o];
        octave.cells = static_cast<std::uint32_t>(std::lround(k_tile_size_m / k_octave_spacing_m[o]));
        const std::uint32_t points = octave.cells + 1;
        octave.values.resize(static_cast<std::size_t>(points) * points);
        for (std::uint32_t b = 0; b < points; ++b) {
            for (std::uint32_t a = 0; a < points; ++a) {
                octave.values[static_cast<std::size_t>(b) * points + a] =
                    static_cast<float>(lattice_value(map_seed, stream, tile, o, a, b, octave.cells));
            }
        }
        octave.cell.resize(k_samples);
        octave.t.resize(k_samples);
        for (std::uint32_t s = 0; s < k_samples; ++s) {
            const double position = static_cast<double>(s) * k_city_sample_spacing_m / k_octave_spacing_m[o];
            const int cell = std::min(static_cast<int>(position), static_cast<int>(octave.cells) - 1);
            const double t = position - static_cast<double>(cell);
            octave.cell[s] = cell;
            octave.t[s] = t * t * (3.0 - 2.0 * t);
        }
    }

    // 4. Heights: base plus detail. The detail's amplitude depends on the ground: the steeper and the less
    //    fertile, the rougher (0.3 m up to 3 m); water (at or below sea level, fading in over the first meter
    //    above it) gets none. Slope and fertility are functions of world position, so tiles still agree.
    const Raster<float>* fertility = map.region_layer("fertility");
    // River channels (version 2). The segments that can reach this tile are picked once, and per block of 16
    // x 16 samples again, so most samples (and every sample of a tile with no river) never look at the river
    // data.
    const RiverNetwork& rivers = map.rivers();
    const double carve_radius = rivers.max_half_width_m() + static_cast<double>(defines.bank_width_m);
    constexpr std::uint32_t k_block = 16;
    constexpr std::uint32_t k_blocks = (k_samples + k_block - 1) / k_block;
    std::vector<std::vector<std::uint32_t>> block_segments;
    if (!rivers.empty()) {
        const std::vector<RiverNetwork::SegmentBox> near_tile = rivers.segments_near_box(
            origin.x, origin.z, origin.x + k_tile_size_m, origin.z + k_tile_size_m, carve_radius);
        if (!near_tile.empty()) {
            block_segments.resize(static_cast<std::size_t>(k_blocks) * k_blocks);
            const double block_m = static_cast<double>(k_block) * k_city_sample_spacing_m;
            for (std::uint32_t by = 0; by < k_blocks; ++by) {
                for (std::uint32_t bx = 0; bx < k_blocks; ++bx) {
                    const double x0 = origin.x + static_cast<double>(bx) * block_m;
                    const double z0 = origin.z + static_cast<double>(by) * block_m;
                    for (const RiverNetwork::SegmentBox& box : near_tile) {
                        if (box.max_x >= x0 - carve_radius && box.min_x <= x0 + block_m + carve_radius &&
                            box.max_z >= z0 - carve_radius && box.min_z <= z0 + block_m + carve_radius) {
                            block_segments[static_cast<std::size_t>(by) * k_blocks + bx].push_back(box.index);
                        }
                    }
                }
            }
        }
    }
    std::vector<float> heights(CityTile::k_sample_count);
    for (std::uint32_t sy = 0; sy < k_samples; ++sy) {
        for (std::uint32_t sx = 0; sx < k_samples; ++sx) {
            const std::size_t centre = static_cast<std::size_t>(sy + 1) * k_base_side + (sx + 1);
            const double h = static_cast<double>(base[centre]);
            const double dx =
                (static_cast<double>(base[centre + 1]) - static_cast<double>(base[centre - 1])) *
                k_inv_two_sample_spacing;
            const double dz = (static_cast<double>(base[centre + k_base_side]) -
                               static_cast<double>(base[centre - k_base_side])) *
                              k_inv_two_sample_spacing;
            const double slope = std::sqrt(dx * dx + dz * dz);
            double fertile = 0.5;
            if (fertility != nullptr) {
                const WorldPos pos{origin.x + static_cast<double>(sx) * k_city_sample_spacing_m,
                                   origin.z + static_cast<double>(sy) * k_city_sample_spacing_m};
                const float value = map.region_value(*fertility, pos);
                fertile = value == value ? clamp01(static_cast<double>(value)) : 0.5; // NaN: no data
            }
            const double roughness = clamp01(0.7 * (1.0 - fertile) + 0.6 * smoothstep(0.02, 0.25, slope));
            const double amplitude =
                (k_min_amplitude_m + (k_max_amplitude_m - k_min_amplitude_m) * roughness) *
                smoothstep(0.0, 1.0, h);

            double noise = 0.0;
            for (std::size_t o = 0; o < octaves.size(); ++o) {
                const Octave& octave = octaves[o];
                const std::size_t points = octave.cells + 1;
                const std::size_t a = static_cast<std::size_t>(octave.cell[sx]);
                const std::size_t b = static_cast<std::size_t>(octave.cell[sy]);
                const double v00 = static_cast<double>(octave.values[b * points + a]);
                const double v10 = static_cast<double>(octave.values[b * points + a + 1]);
                const double v01 = static_cast<double>(octave.values[(b + 1) * points + a]);
                const double v11 = static_cast<double>(octave.values[(b + 1) * points + a + 1]);
                const double tx = octave.t[sx];
                const double ty = octave.t[sy];
                const double bottom = v00 + (v10 - v00) * tx;
                const double top = v01 + (v11 - v01) * tx;
                noise += k_octave_weight[o] * (bottom + (top - bottom) * ty);
            }
            noise = 2.0 * (noise / k_octave_weight_sum) - 1.0; // -1 .. 1

            float height = static_cast<float>(h + amplitude * noise);
            if (!block_segments.empty()) {
                const std::vector<std::uint32_t>& near_block =
                    block_segments[static_cast<std::size_t>(sy / k_block) * k_blocks + sx / k_block];
                if (!near_block.empty()) {
                    const WorldPos pos{origin.x + static_cast<double>(sx) * k_city_sample_spacing_m,
                                       origin.z + static_cast<double>(sy) * k_city_sample_spacing_m};
                    height = carve_channels(rivers, near_block, pos, defines, height);
                }
            }
            heights[static_cast<std::size_t>(sy) * k_samples + sx] = height;
        }
    }
    return std::make_shared<const CityTile>(tile, 1, std::move(heights));
}

} // namespace citysim::world
