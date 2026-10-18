#include "mapgen/synthetic_map.hpp"

#include "citysim/core/rng.hpp"
#include "citysim/world/raster.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace citysim::mapgen {

namespace {

using nlohmann::json;
using world::PackageManifest;
using world::Raster;
using world::TileCoord;

constexpr double k_tile_m = 2048.0;
constexpr double k_spacing_m = 32.0;
constexpr std::uint32_t k_tile_samples = 65; // 2,048 m / 32 m + 1: edge samples are shared with the neighbor
constexpr double k_region_cell_m = 1000.0;
constexpr float k_elevation_scale = 0.25F;         // region elevation, i16: 0.25 m steps
constexpr float k_detail_elevation_scale = 0.125F; // detail elevation, i16: 0.125 m steps
constexpr float k_unit_scale = 1.0F / 255.0F;      // u8 layers that mean 0..1

// A made-up place on the Nile's latitude, only so the map definition has plausible geographic bounds.
constexpr double k_center_lon = 31.0;
constexpr double k_center_lat = 28.0;
constexpr double k_meters_per_degree_lat = 111320.0;
constexpr double k_cos_center_lat = 0.8829; // cos(28 degrees), a constant so no libm call decides the output

double clamp01(double v) {
    return std::clamp(v, 0.0, 1.0);
}
double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}
double smoothstep(double edge0, double edge1, double x) {
    const double t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}
// Rounds to 1/scale (scale 10: tenths). Dividing by a whole scale gives the double nearest the decimal, so
// the JSON text is short.
double round_to(double v, double scale) {
    return std::round(v * scale) / scale;
}

// Smooth value noise on an integer lattice, in [0, 1). The lattice values are counter-based hashes, so a
// value depends only on (seed, name, lattice point).
class Noise {
  public:
    Noise(std::uint64_t seed, std::string_view name) : seed_(seed), stream_(rng::stream(name)) {}

    double value(double x, double z) const {
        const double fx = std::floor(x);
        const double fz = std::floor(z);
        const double tx = x - fx;
        const double tz = z - fz;
        const auto ix = static_cast<std::int64_t>(fx);
        const auto iz = static_cast<std::int64_t>(fz);
        const double sx = tx * tx * (3.0 - 2.0 * tx);
        const double sz = tz * tz * (3.0 - 2.0 * tz);
        return lerp(lerp(lattice(ix, iz), lattice(ix + 1, iz), sx),
                    lerp(lattice(ix, iz + 1), lattice(ix + 1, iz + 1), sx), sz);
    }

    // Octaves of value noise, each at twice the frequency and half the weight; the result is in [0, 1).
    double fbm(double x, double z, int octaves) const {
        double sum = 0.0;
        double weight = 0.0;
        double amplitude = 1.0;
        double frequency = 1.0;
        for (int i = 0; i < octaves; ++i) {
            sum += amplitude * value(x * frequency + 17.0 * i, z * frequency - 31.0 * i);
            weight += amplitude;
            amplitude *= 0.5;
            frequency *= 2.0;
        }
        return sum / weight;
    }

  private:
    double lattice(std::int64_t ix, std::int64_t iz) const {
        return rng::uniform_double01(
            rng::hash64(seed_, stream_, 0, static_cast<std::uint64_t>(ix), static_cast<std::uint64_t>(iz)));
    }

    std::uint64_t seed_;
    std::uint64_t stream_;
};

struct Vertex {
    double x = 0.0;
    double z = 0.0;
    double width_m = 0.0;
    double depth_m = 0.0;
    double water_level_m = 0.0;
};

struct Reach {
    std::string id;
    int order = 1;
    double discharge_m3s = 0.0;
    std::vector<std::string> downstream;
    std::vector<Vertex> vertices;
    std::vector<std::pair<double, double>> floodplain; // one closed ring (x, z), the first point not repeated
};

// Which detail tiles the map has: a tile-aligned rectangle over the valley and delta.
struct DetailArea {
    std::int32_t tx_min = 0;
    std::int32_t tx_max = 0; // inclusive
    std::int32_t ty_min = 0;
    std::int32_t ty_max = 0; // inclusive
};

class Terrain {
  public:
    explicit Terrain(const SyntheticSpec& spec)
        : size_(spec.size_km * 1000.0), axis_noise_(spec.seed, "mapgen.axis"),
          coast_noise_(spec.seed, "mapgen.coast"), plateau_noise_(spec.seed, "mapgen.plateau"),
          micro_noise_(spec.seed, "mapgen.micro"), branch_noise_(spec.seed, "mapgen.branch") {}

    double size() const { return size_; }
    double split_z() const { return 0.62 * size_; }

    // The x of the valley axis (the main channel, and the middle delta branch) at northing z.
    double axis_x(double z) const {
        return 0.5 * size_ + 0.1 * size_ * (axis_noise_.fbm(z / (0.3 * size_), 0.5, 2) - 0.5);
    }

    double valley_half_width(double z) const {
        return size_ * (0.03 + 0.12 * smoothstep(0.50 * size_, 0.82 * size_, z));
    }

    // Distance from the axis in valley half-widths: below 1 is the valley floor or the delta.
    double valley_distance(double x, double z) const {
        return std::abs(x - axis_x(z)) / valley_half_width(z);
    }

    // The northing of the coast at easting x: a wobbly line with the delta bulging out around the axis.
    double coast_z(double x) const {
        const double wobble = 0.025 * size_ * (2.0 * coast_noise_.fbm(x / (0.12 * size_), 0.5, 3) - 1.0);
        const double bulge = 0.04 * size_ * (1.0 - smoothstep(0.0, 0.18 * size_, std::abs(x - 0.5 * size_)));
        return 0.82 * size_ + wobble + bulge;
    }

    // The valley floor, which falls from 40 m in the south to 2 m at the delta, without noise.
    double floor_m(double z) const {
        const double t = clamp01(z / (0.82 * size_));
        return 2.0 + 38.0 * (1.0 - t) * (1.0 - t);
    }

    // Land height above the valley floor on the plateaus, 60-120 m, fading out toward the sea.
    double plateau_m(double x, double z) const {
        const double height = 60.0 + 60.0 * plateau_noise_.fbm(x / (0.15 * size_), z / (0.15 * size_), 3);
        return height * (1.0 - smoothstep(0.55 * size_, 0.80 * size_, z));
    }

    // The surface in meters above sea level at world position (x, z). Used for the region cells and, at a
    // finer step, the detail tiles, so the two agree.
    double elevation(double x, double z) const {
        const double wall = smoothstep(1.0, 1.5, valley_distance(x, z));
        const double micro = (micro_noise_.fbm(x / 600.0, z / 600.0, 3) - 0.5) * (2.0 + 10.0 * wall);
        const double land = floor_m(z) + wall * plateau_m(x, z) + micro;
        const double sea = smoothstep(0.0, 1.0, std::min((z - coast_z(x)) / (0.08 * size_), 1.0));
        return land * (1.0 - sea) - 40.0 * sea;
    }

    // 1 on the valley floor and delta, 0 on the desert.
    double valley_weight(double x, double z) const {
        return 1.0 - smoothstep(0.9, 1.3, valley_distance(x, z));
    }

    // Extra wobble for the delta branches, in meters.
    double branch_wobble(int branch, double z) const {
        return 0.01 * size_ * (2.0 * branch_noise_.fbm(z / (0.1 * size_), 3.0 * branch + 0.5, 2) - 1.0);
    }

    // The tile rectangle over the valley and delta that has detail tiles.
    DetailArea detail_area() const {
        const auto tiles_in = [](double meters) {
            return static_cast<std::int32_t>(std::floor(meters / k_tile_m));
        };
        DetailArea area;
        area.tx_min = tiles_in(0.35 * size_);
        area.tx_max = std::min(tiles_in(0.65 * size_), tiles_in(size_) - 1);
        area.ty_min = tiles_in(0.40 * size_);
        area.ty_max = std::min(tiles_in(0.90 * size_), tiles_in(size_) - 1);
        return area;
    }

  private:
    double size_;
    Noise axis_noise_;
    Noise coast_noise_;
    Noise plateau_noise_;
    Noise micro_noise_;
    Noise branch_noise_;
};

// The land beside a reach that may flood: a strip `distance_m` to each side of the centerline, as a ring
// (the left side from upstream to downstream, then the right side back). The normal at a vertex follows the
// average direction of the two segments that meet there.
std::vector<std::pair<double, double>> floodplain_ring(const std::vector<Vertex>& vertices,
                                                       double distance_m) {
    std::vector<std::pair<double, double>> left;
    std::vector<std::pair<double, double>> right;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const Vertex& before = vertices[i == 0 ? 0 : i - 1];
        const Vertex& after = vertices[i + 1 < vertices.size() ? i + 1 : i];
        double dx = after.x - before.x;
        double dz = after.z - before.z;
        const double length = std::sqrt(dx * dx + dz * dz);
        if (length > 0.0) {
            dx /= length;
            dz /= length;
        }
        left.emplace_back(round_to(vertices[i].x - dz * distance_m, 10.0),
                          round_to(vertices[i].z + dx * distance_m, 10.0));
        right.emplace_back(round_to(vertices[i].x + dz * distance_m, 10.0),
                           round_to(vertices[i].z - dx * distance_m, 10.0));
    }
    left.insert(left.end(), right.rbegin(), right.rend());
    return left;
}

// How far from a river's centerline its floodplain reaches, in meters.
constexpr double k_synthetic_floodplain_m = 1200.0;

// The river: one main channel from the south edge to the split, then three branches (west, center, east) that
// reach the sea. Vertices are about one region cell apart.
std::vector<Reach> make_rivers(const Terrain& terrain) {
    const double size = terrain.size();
    const double step = std::clamp(size / 96.0, 250.0, 2000.0);
    const double split_z = terrain.split_z();
    const auto level_at = [&](double z) { return round_to(terrain.floor_m(z) - 1.5, 10.0); };

    std::vector<Reach> reaches;
    Reach main;
    main.id = "main";
    main.order = 3;
    main.discharge_m3s = 3000.0;
    main.downstream = {"west", "center", "east"};
    const auto main_count = static_cast<std::size_t>(std::ceil(split_z / step));
    for (std::size_t i = 0; i <= main_count; ++i) {
        const double z = split_z * static_cast<double>(i) / static_cast<double>(main_count);
        const double u = z / split_z;
        main.vertices.push_back(Vertex{round_to(terrain.axis_x(z), 10.0), round_to(z, 10.0),
                                       round_to(lerp(200.0, 600.0, u), 1.0),
                                       round_to(lerp(6.0, 8.0, u), 10.0), level_at(z)});
    }
    reaches.push_back(std::move(main));

    struct Branch {
        const char* id;
        double offset_m; // where it ends, east of the axis at the mouth
        double end_width_m;
    };
    const Branch branches[] = {
        {"west", -0.08 * size, 80.0}, {"center", 0.0, 140.0}, {"east", 0.08 * size, 80.0}};
    int index = 0;
    for (const Branch& branch : branches) {
        Reach reach;
        reach.id = branch.id;
        reach.order = 2;
        reach.discharge_m3s = 1000.0;
        const double mouth_x = terrain.axis_x(split_z) + branch.offset_m;
        const double end_z =
            terrain.coast_z(mouth_x) + 0.01 * size; // just past the shore, where the land ends
        const auto count = static_cast<std::size_t>(std::ceil((end_z - split_z) / step));
        for (std::size_t i = 0; i <= count; ++i) {
            const double u = static_cast<double>(i) / static_cast<double>(count);
            const double z = lerp(split_z, end_z, u);
            // The middle branch is the axis itself; the others fan out from it and wobble a little.
            const double fan = branch.offset_m * smoothstep(0.0, 1.0, u);
            const double wobble =
                (branch.offset_m == 0.0 ? 0.0 : terrain.branch_wobble(index, z)) * (4.0 * u * (1.0 - u));
            reach.vertices.push_back(Vertex{round_to(terrain.axis_x(z) + fan + wobble, 10.0),
                                            round_to(z, 10.0),
                                            round_to(lerp(200.0, branch.end_width_m, u), 1.0),
                                            round_to(lerp(4.0, 2.0, u), 10.0), level_at(z)});
        }
        reaches.push_back(std::move(reach));
        ++index;
    }
    for (Reach& reach : reaches) {
        reach.floodplain = floodplain_ring(reach.vertices, k_synthetic_floodplain_m);
    }
    return reaches;
}

json rivers_json(const std::vector<Reach>& reaches) {
    json list = json::array();
    for (const Reach& reach : reaches) {
        json vertices = json::array();
        for (const Vertex& v : reach.vertices) {
            vertices.push_back(json{{"x", v.x},
                                    {"z", v.z},
                                    {"width_m", v.width_m},
                                    {"depth_m", v.depth_m},
                                    {"water_level_m", v.water_level_m}});
        }
        json ring = json::array();
        for (const auto& [x, z] : reach.floodplain) {
            ring.push_back(json::array({x, z}));
        }
        list.push_back(json{{"id", reach.id},
                            {"order", reach.order},
                            {"discharge_m3s", reach.discharge_m3s},
                            {"downstream", reach.downstream},
                            {"vertices", vertices},
                            {"floodplain", json::array({ring})}});
    }
    return json{
        {"format", 1},
        {"rivers",
         json::array({json{{"id", "synthetic_nile"}, {"name", "Synthetic Nile"}, {"reaches", list}}})}};
}

// Distance in meters from (x, z) to the nearest river centerline.
double distance_to_rivers(const std::vector<Reach>& reaches, double x, double z) {
    double best = 1e18;
    for (const Reach& reach : reaches) {
        for (std::size_t i = 0; i + 1 < reach.vertices.size(); ++i) {
            const Vertex& a = reach.vertices[i];
            const Vertex& b = reach.vertices[i + 1];
            const double dx = b.x - a.x;
            const double dz = b.z - a.z;
            const double length_sq = dx * dx + dz * dz;
            const double t = length_sq > 0.0 ? clamp01(((x - a.x) * dx + (z - a.z) * dz) / length_sq) : 0.0;
            const double px = a.x + t * dx - x;
            const double pz = a.z + t * dz - z;
            best = std::min(best, px * px + pz * pz);
        }
    }
    return std::sqrt(best);
}

struct Blob {
    double x = 0.0;
    double z = 0.0;
    double radius_m = 0.0;
};

// A few deposit blobs on the plateaus (away from the valley, on land), chosen with counter-based hashes.
std::vector<Blob> make_blobs(const Terrain& terrain, std::uint64_t seed, std::string_view name, int count) {
    const double size = terrain.size();
    const std::uint64_t stream = rng::stream(name);
    std::vector<Blob> blobs;
    for (int i = 0; i < count; ++i) {
        for (std::uint64_t attempt = 0; attempt < 64; ++attempt) {
            const auto key = static_cast<std::uint64_t>(i);
            const double x =
                lerp(0.05, 0.95, rng::uniform_double01(rng::hash64(seed, stream, 0, key, attempt * 3)));
            const double z =
                lerp(0.05, 0.6, rng::uniform_double01(rng::hash64(seed, stream, 0, key, attempt * 3 + 1)));
            if (terrain.valley_distance(x * size, z * size) < 1.8) {
                continue;
            }
            const double radius =
                size *
                lerp(0.03, 0.06, rng::uniform_double01(rng::hash64(seed, stream, 0, key, attempt * 3 + 2)));
            blobs.push_back(Blob{x * size, z * size, radius});
            break;
        }
    }
    return blobs;
}

std::uint8_t to_u8(double unit) {
    return static_cast<std::uint8_t>(std::lround(clamp01(unit) * 255.0));
}

std::int16_t to_i16(double meters, double step) {
    return static_cast<std::int16_t>(std::clamp(std::lround(meters / step), -32768L, 32767L));
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out) {
        throw world::MapFormatError(path.generic_string(), "cannot write the file");
    }
}

struct GeoDeg {
    double lon = 0.0;
    double lat = 0.0;
};

// The lon/lat of a world position, with the map center at (k_center_lon, k_center_lat). Flat-earth, which is
// plenty for the bounds of a made-up map.
GeoDeg to_geo(double size, double x, double z) {
    return GeoDeg{k_center_lon + (x - 0.5 * size) / (k_meters_per_degree_lat * k_cos_center_lat),
                  k_center_lat + (z - 0.5 * size) / k_meters_per_degree_lat};
}

json geo_point(const GeoDeg& g) {
    return json::array({round_to(g.lon, 10000.0), round_to(g.lat, 10000.0)});
}

// Adds the file's checksum to the manifest once it is written.
void record(PackageManifest& manifest, const std::filesystem::path& dir, const std::string& relative) {
    manifest.files[relative] = world::hash_file(dir / relative);
}

} // namespace

std::string check_spec(const SyntheticSpec& spec) {
    if (!(spec.size_km >= k_min_size_km && spec.size_km <= k_max_size_km)) {
        return "size-km must be between " + std::to_string(static_cast<int>(k_min_size_km)) + " and " +
               std::to_string(static_cast<int>(k_max_size_km));
    }
    if (std::floor(spec.size_km) != spec.size_km) {
        return "size-km must be a whole number (the region cells are 1 km)";
    }
    return {};
}

std::string synthetic_map_def_text(const SyntheticSpec& spec) {
    const Terrain terrain(spec);
    const double size = terrain.size();
    const DetailArea area = terrain.detail_area();
    const double x0 = area.tx_min * k_tile_m;
    const double x1 = (area.tx_max + 1) * k_tile_m;
    const double z0 = area.ty_min * k_tile_m;
    const double z1 = (area.ty_max + 1) * k_tile_m;
    const GeoDeg south_west = to_geo(size, 0.0, 0.0);
    const GeoDeg north_east = to_geo(size, size, size);
    const json def{
        {"type", "map"},
        {"id", k_synthetic_map_id},
        {"name_key", "map.synthetic.name"},
        {"desc_key", "map.synthetic.desc"},
        {"package", k_synthetic_package_folder},
        {"bounds", json{{"min_lon", round_to(south_west.lon, 10000.0)},
                        {"min_lat", round_to(south_west.lat, 10000.0)},
                        {"max_lon", round_to(north_east.lon, 10000.0)},
                        {"max_lat", round_to(north_east.lat, 10000.0)}}},
        {"region_cell_size_m", k_region_cell_m},
        {"map_seed", spec.seed},
        {"detail_regions",
         json::array({json::array({geo_point(to_geo(size, x0, z0)), geo_point(to_geo(size, x1, z0)),
                                   geo_point(to_geo(size, x1, z1)), geo_point(to_geo(size, x0, z1)),
                                   geo_point(to_geo(size, x0, z0))})})},
        {"river_regimes", json::array()}};
    return json{{"defs", json::array({def})}}.dump(2) + "\n";
}

PackageManifest write_synthetic_package(const SyntheticSpec& spec, const std::filesystem::path& out_dir) {
    if (const std::string problem = check_spec(spec); !problem.empty()) {
        throw std::invalid_argument(problem);
    }
    const Terrain terrain(spec);
    const double size = terrain.size();
    const auto cells = static_cast<std::uint32_t>(spec.size_km); // 1 km cells
    const std::vector<Reach> rivers = make_rivers(terrain);

    PackageManifest manifest;
    manifest.format = world::k_package_format_version;
    manifest.map_id = k_synthetic_map_id;
    manifest.pipeline_version = "synthetic-1";
    manifest.sources = {
        world::PackageSource{"citysim_mapgen --synthetic", "1", "generated, no external data"}};
    manifest.projection = world::PackageProjection{"laea", k_center_lat, k_center_lon};
    manifest.bounds_width_m = size;
    manifest.bounds_height_m = size;
    manifest.region.cell_size_m = k_region_cell_m;
    manifest.region.width = cells;
    manifest.region.height = cells;
    manifest.detail.tile_size_m = k_tile_m;
    manifest.detail.sample_spacing_m = k_spacing_m;
    manifest.rivers_file = "rivers.json";
    manifest.lakes_file = "lakes.json";

    // Region layers, sampled at the cell centers.
    Raster<std::int16_t> elevation(cells, cells, k_elevation_scale);
    Raster<std::uint8_t> land_mask(cells, cells);
    Raster<std::uint8_t> fertility(cells, cells, k_unit_scale);
    Raster<std::uint8_t> vegetation(cells, cells, k_unit_scale);
    Raster<std::uint8_t> wetness(cells, cells, k_unit_scale);
    Raster<std::uint8_t> stone(cells, cells, k_unit_scale);
    Raster<std::uint8_t> copper(cells, cells, k_unit_scale);
    const std::vector<Blob> stone_blobs = make_blobs(terrain, spec.seed, "mapgen.deposit.stone", 3);
    const std::vector<Blob> copper_blobs = make_blobs(terrain, spec.seed, "mapgen.deposit.copper", 2);
    const Noise vegetation_noise(spec.seed, "mapgen.vegetation");
    const auto deposit_at = [](const std::vector<Blob>& blobs, double x, double z) {
        double amount = 0.0;
        for (const Blob& blob : blobs) {
            const double dx = x - blob.x;
            const double dz = z - blob.z;
            const double d = std::sqrt(dx * dx + dz * dz) / blob.radius_m;
            amount = std::max(amount, 1.0 - smoothstep(0.4, 1.0, d));
        }
        return amount;
    };
    for (std::uint32_t row = 0; row < cells; ++row) {
        for (std::uint32_t col = 0; col < cells; ++col) {
            const double x = (static_cast<double>(col) + 0.5) * k_region_cell_m;
            const double z = (static_cast<double>(row) + 0.5) * k_region_cell_m;
            const std::int16_t stored =
                to_i16(terrain.elevation(x, z), static_cast<double>(k_elevation_scale));
            // Land is decided on the stored (rounded) elevation, so the land mask always agrees with the
            // raster.
            const bool land = stored > 0;
            const double river_m = distance_to_rivers(rivers, x, z);
            const double near_river = 1.0 / (1.0 + (river_m / 3000.0) * (river_m / 3000.0));
            const double fertile =
                land ? clamp01((0.08 + 0.92 * terrain.valley_weight(x, z)) * (0.75 + 0.25 * near_river))
                     : 0.0;
            elevation.at(col, row) = stored;
            land_mask.at(col, row) = land ? 1 : 0;
            fertility.at(col, row) = to_u8(fertile);
            vegetation.at(col, row) =
                to_u8(land ? 0.9 * fertile + 0.1 * vegetation_noise.fbm(x / 5000.0, z / 5000.0, 2) : 0.0);
            wetness.at(col, row) = to_u8(land ? 1.0 / (1.0 + (river_m / 1500.0) * (river_m / 1500.0)) : 1.0);
            stone.at(col, row) = to_u8(land ? deposit_at(stone_blobs, x, z) : 0.0);
            copper.at(col, row) = to_u8(land ? deposit_at(copper_blobs, x, z) : 0.0);
        }
    }
    const auto write_layer = [&](const std::string& name, const auto& raster) {
        const std::string relative = "region/" + name + ".csr";
        world::write_csr(out_dir / relative, raster);
        manifest.region.layers[name] = relative;
        record(manifest, out_dir, relative);
    };
    write_layer("elevation", elevation);
    write_layer("land_mask", land_mask);
    write_layer("fertility", fertility);
    write_layer("vegetation", vegetation);
    write_layer("wetness", wetness);
    write_layer("stone_limestone", stone);
    write_layer("ore_copper", copper);

    // Detail tiles: the same surface at 32 m spacing, 65 x 65 samples, edge samples shared with the neighbor.
    const DetailArea area = terrain.detail_area();
    for (std::int32_t ty = area.ty_min; ty <= area.ty_max; ++ty) {
        for (std::int32_t tx = area.tx_min; tx <= area.tx_max; ++tx) {
            Raster<std::int16_t> tile(k_tile_samples, k_tile_samples, k_detail_elevation_scale);
            for (std::uint32_t row = 0; row < k_tile_samples; ++row) {
                for (std::uint32_t col = 0; col < k_tile_samples; ++col) {
                    const double x = tx * k_tile_m + static_cast<double>(col) * k_spacing_m;
                    const double z = ty * k_tile_m + static_cast<double>(row) * k_spacing_m;
                    tile.at(col, row) =
                        to_i16(terrain.elevation(x, z), static_cast<double>(k_detail_elevation_scale));
                }
            }
            const TileCoord coord{tx, ty};
            const std::string relative = world::detail_tile_file(coord);
            world::write_csr(out_dir / relative, tile);
            manifest.detail.tiles.push_back(coord);
            record(manifest, out_dir, relative);
        }
    }

    write_text(out_dir / "rivers.json", rivers_json(rivers).dump(2) + "\n");
    write_text(out_dir / "lakes.json", json{{"format", 1}, {"lakes", json::array()}}.dump(2) + "\n");
    record(manifest, out_dir, "rivers.json");
    record(manifest, out_dir, "lakes.json");

    manifest.package_hash = world::compute_package_hash(manifest.files);
    write_text(out_dir / world::k_package_file_name, world::manifest_to_text(manifest));
    write_text(out_dir / "map_def.json", synthetic_map_def_text(spec));
    return manifest;
}

} // namespace citysim::mapgen
