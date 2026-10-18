#include "citysim/world/loaded_map.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace citysim::world {

namespace {

template <class T> Raster<float> decode_to_float(const std::filesystem::path& path) {
    const Raster<T> stored = read_csr<T>(path);
    Raster<float> out(stored.width(), stored.height());
    for (std::uint32_t y = 0; y < stored.height(); ++y) {
        for (std::uint32_t x = 0; x < stored.width(); ++x) {
            out.at(x, y) = stored.value(x, y);
        }
    }
    return out;
}

// Any `.csr` file as decoded values in meters (or whatever unit the layer has); no-data cells become NaN.
Raster<float> read_as_float(const std::filesystem::path& path) {
    switch (read_csr_header(path).type) {
    case RasterType::U8:
        return decode_to_float<std::uint8_t>(path);
    case RasterType::U16:
        return decode_to_float<std::uint16_t>(path);
    case RasterType::I16:
        return decode_to_float<std::int16_t>(path);
    case RasterType::F32:
        return decode_to_float<float>(path);
    }
    throw MapFormatError(path.generic_string(), "unknown element type");
}

bool has_nan(const Raster<float>& raster) {
    return std::any_of(raster.data().begin(), raster.data().end(), [](float v) { return std::isnan(v); });
}

float lerp(float a, float b, float f) {
    return a * (1.0F - f) + b * f; // exact at f = 0 and f = 1, see CityTile::height_at
}

// Bilinear in a grid whose sample (i, j) sits at grid coordinates (i, j); u and v are clamped to the grid.
float bilinear(const Raster<float>& grid, double u, double v) {
    const auto last_x = static_cast<double>(grid.width() - 1);
    const auto last_y = static_cast<double>(grid.height() - 1);
    u = std::clamp(u, 0.0, last_x);
    v = std::clamp(v, 0.0, last_y);
    const auto i = static_cast<std::uint32_t>(std::min(std::floor(u), std::max(last_x - 1.0, 0.0)));
    const auto j = static_cast<std::uint32_t>(std::min(std::floor(v), std::max(last_y - 1.0, 0.0)));
    const std::uint32_t i1 = std::min(i + 1, grid.width() - 1);
    const std::uint32_t j1 = std::min(j + 1, grid.height() - 1);
    const auto fu = static_cast<float>(u - static_cast<double>(i));
    const auto fv = static_cast<float>(v - static_cast<double>(j));
    return lerp(lerp(grid.at(i, j), grid.at(i1, j), fu), lerp(grid.at(i, j1), grid.at(i1, j1), fu), fv);
}

std::string tile_text(TileCoord tile) {
    return "(" + std::to_string(tile.tx) + ", " + std::to_string(tile.ty) + ")";
}

} // namespace

std::shared_ptr<const LoadedMap> LoadedMap::open(const std::filesystem::path& dir,
                                                 std::string_view expected_map_id) {
    std::shared_ptr<LoadedMap> map(new LoadedMap());
    map->dir_ = dir;
    map->manifest_ = verify_package(dir);
    const PackageManifest& manifest = map->manifest_;
    const std::string where = (dir / k_package_file_name).generic_string();

    if (!expected_map_id.empty() && manifest.map_id != expected_map_id) {
        throw MapFormatError(where, "the package is for map '" + manifest.map_id + "', not '" +
                                        std::string(expected_map_id) + "'");
    }
    if (manifest.detail.tile_size_m != k_tile_size_m) {
        throw MapFormatError(where, "/detail/tile_size_m: this build needs 2048 m tiles");
    }
    const double intervals = k_tile_size_m / manifest.detail.sample_spacing_m;
    if (!(intervals >= 1.0) || intervals != std::floor(intervals) || intervals > 4096.0) {
        throw MapFormatError(where, "/detail/sample_spacing_m: must divide 2048 m into whole steps");
    }
    map->detail_samples_ = static_cast<std::uint32_t>(intervals) + 1;
    if (!(manifest.bounds_width_m > 0.0) || !(manifest.bounds_height_m > 0.0) ||
        manifest.bounds_width_m > k_tile_size_m * (1 << 20) ||
        manifest.bounds_height_m > k_tile_size_m * (1 << 20)) {
        throw MapFormatError(where, "/bounds_m: the map must be larger than 0 m and smaller than 2^20 tiles");
    }
    map->tiles_x_ = static_cast<std::int32_t>(std::ceil(manifest.bounds_width_m / k_tile_size_m));
    map->tiles_y_ = static_cast<std::int32_t>(std::ceil(manifest.bounds_height_m / k_tile_size_m));

    for (const auto& [name, file] : manifest.region.layers) {
        const std::filesystem::path path = dir / file;
        Raster<float> layer = read_as_float(path);
        if (layer.width() != manifest.region.width || layer.height() != manifest.region.height) {
            throw MapFormatError(path.generic_string(), "is " + std::to_string(layer.width()) + " x " +
                                                            std::to_string(layer.height()) +
                                                            " cells, but the manifest says " +
                                                            std::to_string(manifest.region.width) + " x " +
                                                            std::to_string(manifest.region.height));
        }
        map->region_layers_.emplace(name, std::move(layer));
    }
    const auto elevation = map->region_layers_.find("elevation");
    if (elevation == map->region_layers_.end()) {
        throw MapFormatError(where, "/region/layers: the package has no \"elevation\" layer");
    }
    if (has_nan(elevation->second)) {
        throw MapFormatError((dir / manifest.region.layers.at("elevation")).generic_string(),
                             "the elevation layer has no-data cells");
    }
    map->elevation_ = &elevation->second;

    {
        const std::filesystem::path path = dir / manifest.rivers_file;
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw MapFormatError(path.generic_string(), "cannot open the file");
        }
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(in);
        } catch (const nlohmann::json::parse_error& error) {
            throw MapFormatError(path.generic_string(), std::string("not valid JSON: ") + error.what());
        }
        map->rivers_ = RiverNetwork::parse(json, path.generic_string());
    }

    map->detail_tiles_ = manifest.detail.tiles;
    std::sort(map->detail_tiles_.begin(), map->detail_tiles_.end());
    return map;
}

const Raster<float>* LoadedMap::region_layer(std::string_view name) const {
    const auto it = region_layers_.find(name);
    return it == region_layers_.end() ? nullptr : &it->second;
}

float LoadedMap::region_value(const Raster<float>& layer, WorldPos pos) const {
    if (!std::isfinite(pos.x) || !std::isfinite(pos.z)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const double cell = manifest_.region.cell_size_m;
    return bilinear(layer, pos.x / cell - 0.5, pos.z / cell - 0.5);
}

bool LoadedMap::has_detail_tile(TileCoord tile) const {
    return std::binary_search(detail_tiles_.begin(), detail_tiles_.end(), tile);
}

std::shared_ptr<const Raster<float>> LoadedMap::detail_tile(TileCoord tile) const {
    if (!has_detail_tile(tile)) {
        return nullptr;
    }
    {
        const std::lock_guard lock(detail_mutex_);
        const auto it = detail_cache_.find(tile);
        if (it != detail_cache_.end()) {
            return it->second;
        }
    }
    // Read outside the lock so threads that need different tiles do not wait for each other. Two threads may
    // read the same tile at once; the first to finish wins, and both copies are identical anyway.
    const std::filesystem::path path = dir_ / detail_tile_file(tile);
    auto loaded = std::make_shared<Raster<float>>(read_as_float(path));
    if (loaded->width() != detail_samples_ || loaded->height() != detail_samples_) {
        throw MapFormatError(path.generic_string(), "detail tile " + tile_text(tile) + " is not " +
                                                        std::to_string(detail_samples_) + " x " +
                                                        std::to_string(detail_samples_) + " samples");
    }
    if (has_nan(*loaded)) {
        throw MapFormatError(path.generic_string(),
                             "detail tile " + tile_text(tile) + " has no-data samples");
    }
    const std::lock_guard lock(detail_mutex_);
    return detail_cache_.emplace(tile, std::move(loaded)).first->second;
}

LoadedMap::Candidates LoadedMap::covering_tiles(WorldPos pos) {
    Candidates out;
    const TileCoord tile = tile_of(pos);
    const WorldPos origin = tile_origin(tile);
    const bool on_x_edge = pos.x == origin.x;
    const bool on_z_edge = pos.z == origin.z;
    out.tiles[out.count++] = tile;
    if (on_x_edge) {
        out.tiles[out.count++] = TileCoord{tile.tx - 1, tile.ty};
    }
    if (on_z_edge) {
        out.tiles[out.count++] = TileCoord{tile.tx, tile.ty - 1};
    }
    if (on_x_edge && on_z_edge) {
        out.tiles[out.count++] = TileCoord{tile.tx - 1, tile.ty - 1};
    }
    return out;
}

float LoadedMap::detail_value(const Raster<float>& tile, TileCoord coord, WorldPos pos) const {
    const WorldPos origin = tile_origin(coord);
    const double spacing = manifest_.detail.sample_spacing_m;
    return bilinear(tile, (pos.x - origin.x) / spacing, (pos.z - origin.z) / spacing);
}

float LoadedMap::dem_height(WorldPos pos) const {
    if (!std::isfinite(pos.x) || !std::isfinite(pos.z)) {
        return std::numeric_limits<float>::quiet_NaN();
    }
    const Candidates candidates = covering_tiles(pos);
    for (std::size_t i = 0; i < candidates.count; ++i) {
        if (const auto tile = detail_tile(candidates.tiles[i])) {
            return detail_value(*tile, candidates.tiles[i], pos);
        }
    }
    return region_height(pos);
}

} // namespace citysim::world
