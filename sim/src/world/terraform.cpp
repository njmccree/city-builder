#include "citysim/world/terraform.hpp"

#include "citysim/profiling/profile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace citysim::world {

namespace {

constexpr double k_cell_m = 4.0;
constexpr double k_cell_area_m2 = k_cell_m * k_cell_m;
constexpr std::int64_t k_tile_cells = 512;
constexpr std::int64_t k_area_cells = k_tile_cells * k_city_area_tiles; // sample intervals along an area

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

// 1 in the middle, 0 at the edge of the brush, with a smooth (zero-slope) start and end.
double falloff(double t) {
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

// 1 up to half the radius, then falling smoothly to 0 at the edge.
double plateau(double t) {
    return t <= 0.5 ? 1.0 : falloff((t - 0.5) * 2.0);
}

bool valid(const Terraform& command) {
    const auto in_range = [](float value, float low, float high) {
        return std::isfinite(value) && value >= low && value <= high;
    };
    return static_cast<std::uint8_t>(command.op) < k_terraform_op_count && std::isfinite(command.center.x) &&
           std::isfinite(command.center.z) &&
           in_range(command.radius_m, k_terraform_min_radius_m, k_terraform_max_radius_m) &&
           in_range(command.strength, 0.0F, 1.0F) &&
           in_range(command.target_height_m, k_terraform_min_target_m, k_terraform_max_target_m);
}

} // namespace

TerraformResult apply_terraform(WorldTerrain& terrain, const CityArea& area, const Terraform& command,
                                const TerrainDefines& defines) {
    CS_PROFILE_ZONE("world.terraform");
    TerraformResult result;
    if (!valid(command)) {
        result.status = TerraformStatus::BadParameters;
        return result;
    }

    const WorldPos origin = area.origin();
    const double cx = origin.x + static_cast<double>(command.center.x);
    const double cz = origin.z + static_cast<double>(command.center.z);
    const double radius = static_cast<double>(command.radius_m);
    const double strength = static_cast<double>(command.strength);
    const double target = static_cast<double>(command.target_height_m);

    // The samples of the area: its south and west edge samples are in, the north and east edge ones are the
    // neighbor's. gx is the sample's index along x on the global 4 m grid (x = gx * 4).
    const auto area_x0 = static_cast<std::int64_t>(std::llround(origin.x / k_cell_m));
    const auto area_z0 = static_cast<std::int64_t>(std::llround(origin.z / k_cell_m));
    const std::int64_t gx_lo =
        std::max(static_cast<std::int64_t>(std::ceil((cx - radius) / k_cell_m)), area_x0);
    const std::int64_t gx_hi =
        std::min(static_cast<std::int64_t>(std::floor((cx + radius) / k_cell_m)), area_x0 + k_area_cells - 1);
    const std::int64_t gz_lo =
        std::max(static_cast<std::int64_t>(std::ceil((cz - radius) / k_cell_m)), area_z0);
    const std::int64_t gz_hi =
        std::min(static_cast<std::int64_t>(std::floor((cz + radius) / k_cell_m)), area_z0 + k_area_cells - 1);
    if (cx + radius < static_cast<double>(area_x0) * k_cell_m ||
        cx - radius > static_cast<double>(area_x0 + k_area_cells - 1) * k_cell_m ||
        cz + radius < static_cast<double>(area_z0) * k_cell_m ||
        cz - radius > static_cast<double>(area_z0 + k_area_cells - 1) * k_cell_m) {
        result.status = TerraformStatus::OutsideCity;
        return result;
    }
    if (gx_lo > gx_hi || gz_lo > gz_hi) {
        return result; // the brush lies between two samples: nothing to change
    }

    // A snapshot of the heights over the box and one sample around it (the smoothing brush reads neighbors).
    const std::int64_t width = gx_hi - gx_lo + 1;
    const std::int64_t height = gz_hi - gz_lo + 1;
    const std::int64_t snap_width = width + 2;
    const auto snap_index = [&](std::int64_t gx, std::int64_t gz) {
        return static_cast<std::size_t>((gz - gz_lo + 1) * snap_width + (gx - gx_lo + 1));
    };
    std::vector<float> snapshot(static_cast<std::size_t>(snap_width * (height + 2)));
    for (std::int64_t gz = gz_lo - 1; gz <= gz_hi + 1; ++gz) {
        for (std::int64_t gx = gx_lo - 1; gx <= gx_hi + 1; ++gx) {
            snapshot[snap_index(gx, gz)] = terrain.height_at(
                WorldPos{static_cast<double>(gx) * k_cell_m, static_cast<double>(gz) * k_cell_m});
        }
    }

    // Rivers stay fixed: channel samples are never changed and the ground beside a channel is never cut below
    // the channel's bed (the base level minus the depth).
    const RiverNetwork& rivers = terrain.map()->rivers();
    const double river_reach = rivers.max_half_width_m() + static_cast<double>(defines.bank_width_m);
    const bool rivers_near =
        !rivers.empty() &&
        rivers.any_near_box(static_cast<double>(gx_lo) * k_cell_m, static_cast<double>(gz_lo) * k_cell_m,
                            static_cast<double>(gx_hi) * k_cell_m, static_cast<double>(gz_hi) * k_cell_m,
                            river_reach);

    std::vector<float> next(static_cast<std::size_t>(width * height));
    std::vector<std::uint8_t> changed(next.size(), 0);
    const auto box_index = [&](std::int64_t gx, std::int64_t gz) {
        return static_cast<std::size_t>((gz - gz_lo) * width + (gx - gx_lo));
    };
    for (std::int64_t gz = gz_lo; gz <= gz_hi; ++gz) {
        for (std::int64_t gx = gx_lo; gx <= gx_hi; ++gx) {
            const double x = static_cast<double>(gx) * k_cell_m;
            const double z = static_cast<double>(gz) * k_cell_m;
            const double d2 = (x - cx) * (x - cx) + (z - cz) * (z - cz);
            if (d2 >= radius * radius) {
                continue;
            }
            const double t = std::sqrt(d2) / radius;
            const double here = static_cast<double>(snapshot[snap_index(gx, gz)]);
            double delta = 0.0;
            switch (command.op) {
            case TerraformOp::Raise:
                delta = strength * static_cast<double>(defines.terraform_step_m) * falloff(t);
                break;
            case TerraformOp::Lower:
                delta = -strength * static_cast<double>(defines.terraform_step_m) * falloff(t);
                break;
            case TerraformOp::Flatten:
                delta = (target - here) * strength * falloff(t);
                break;
            case TerraformOp::Level:
                delta = (target - here) * strength * plateau(t);
                break;
            case TerraformOp::Smooth: {
                const double average = (static_cast<double>(snapshot[snap_index(gx - 1, gz)]) +
                                        static_cast<double>(snapshot[snap_index(gx + 1, gz)]) +
                                        static_cast<double>(snapshot[snap_index(gx, gz - 1)]) +
                                        static_cast<double>(snapshot[snap_index(gx, gz + 1)])) /
                                       4.0;
                delta = (average - here) * strength * falloff(t);
                break;
            }
            }
            double proposed = here + delta;

            bool in_channel = false;
            if (rivers_near) {
                double bed_floor = -1.0e30;
                rivers.for_each_near(WorldPos{x, z}, river_reach, [&](const ChannelSample& channel) {
                    if (channel.distance_m <= channel.half_width_m) {
                        in_channel = true;
                    } else if (static_cast<double>(channel.distance_m) <=
                               static_cast<double>(channel.half_width_m) +
                                   static_cast<double>(defines.bank_width_m)) {
                        bed_floor =
                            std::max(bed_floor, static_cast<double>(channel.level_m - channel.depth_m));
                    }
                });
                if (in_channel) {
                    continue;
                }
                proposed = std::max(proposed, bed_floor);
            }

            // Within [-50, +200] m of the synthesized base: the base is the current height minus the edit
            // already stored for the sample.
            const std::int64_t tx = floor_div(gx, k_tile_cells);
            const std::int64_t tz = floor_div(gz, k_tile_cells);
            const TileCoord tile{static_cast<std::int32_t>(tx), static_cast<std::int32_t>(tz)};
            const TileDelta* layer = terrain.deltas().find(tile);
            const double stored =
                layer != nullptr
                    ? static_cast<double>(layer->offset(static_cast<std::uint32_t>(gx - tx * k_tile_cells),
                                                        static_cast<std::uint32_t>(gz - tz * k_tile_cells)))
                    : 0.0;
            const double base = here - stored;
            proposed = std::clamp(proposed, base + static_cast<double>(k_terraform_min_offset_m),
                                  base + static_cast<double>(k_terraform_max_offset_m));

            const auto value = static_cast<float>(proposed);
            if (value != snapshot[snap_index(gx, gz)]) {
                next[box_index(gx, gz)] = value;
                changed[box_index(gx, gz)] = 1;
                ++result.samples_changed;
                result.volume_m3 += std::abs(static_cast<double>(value) - here) * k_cell_area_m2;
            }
        }
    }
    if (result.samples_changed == 0) {
        return result;
    }

    // Apply tile by tile, south row first. A sample on a tile edge is applied by the tile that owns it (the
    // one to its north-east) and apply_delta copies it to the neighbors, so shared edges stay equal.
    for (std::int64_t tz = floor_div(gz_lo, k_tile_cells); tz <= floor_div(gz_hi, k_tile_cells); ++tz) {
        for (std::int64_t tx = floor_div(gx_lo, k_tile_cells); tx <= floor_div(gx_hi, k_tile_cells); ++tx) {
            const std::int64_t x_first = std::max(gx_lo, tx * k_tile_cells);
            const std::int64_t x_last = std::min(gx_hi, tx * k_tile_cells + k_tile_cells - 1);
            const std::int64_t z_first = std::max(gz_lo, tz * k_tile_cells);
            const std::int64_t z_last = std::min(gz_hi, tz * k_tile_cells + k_tile_cells - 1);
            if (x_first > x_last || z_first > z_last) {
                continue;
            }
            const SampleRect rect{static_cast<std::uint32_t>(x_first - tx * k_tile_cells),
                                  static_cast<std::uint32_t>(z_first - tz * k_tile_cells),
                                  static_cast<std::uint32_t>(x_last - tx * k_tile_cells),
                                  static_cast<std::uint32_t>(z_last - tz * k_tile_cells)};
            terrain.apply_delta(TileCoord{static_cast<std::int32_t>(tx), static_cast<std::int32_t>(tz)}, rect,
                                [&](std::uint32_t sx, std::uint32_t sy, float current) {
                                    const std::int64_t gx = tx * k_tile_cells + static_cast<std::int64_t>(sx);
                                    const std::int64_t gz = tz * k_tile_cells + static_cast<std::int64_t>(sy);
                                    const std::size_t i = box_index(gx, gz);
                                    return changed[i] != 0 ? next[i] : current;
                                });
        }
    }
    return result;
}

} // namespace citysim::world
