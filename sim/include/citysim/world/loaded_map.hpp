#pragma once

#include "citysim/world/frames.hpp"
#include "citysim/world/map_package.hpp"
#include "citysim/world/raster.hpp"
#include "citysim/world/river_network.hpp"
#include "citysim/world/tile_coord.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace citysim::world {

// An opened map package (architecture §15.2): the verified manifest, every region layer decoded to float, and
// the 32 m detail tiles, read from disk the first time something asks for one. Logically immutable once
// opened, so one instance is shared as std::shared_ptr<const LoadedMap> by everything that reads the map: the
// WorldTerrain of every Simulation of a game session (a load builds a new Simulation but keeps the map),
// synthesis jobs on worker threads, and later the region renderer.
//
// Thread-safe: every member may be called from any thread at the same time. The detail-tile cache is the only
// thing that changes after opening, and it is guarded; a tile, once loaded, is never replaced or dropped.
// Not tied to a clock.
//
// Sampling rules, shared by synthesis and by height queries outside the active city tiles:
//   * Region cells hold the value at their center ((i + 0.5) * cell size); region_value() is bilinear between
//     centers and clamps at the raster's edges, so it answers anywhere, also beyond the map bounds.
//   * Detail tiles hold 65 x 65 samples on their corners and edges (32 m spacing). A point is covered by a
//     detail tile if it lies in the tile's closed square; on an edge or corner between tiles the first
//     present one in the order (tx, ty), (tx - 1, ty), (tx, ty - 1), (tx - 1, ty - 1) of tile_of(point)
//     answers. Shared edge samples are equal, so the choice never changes the height.
class LoadedMap {
  public:
    // Opens and verifies the package in `dir` (world::verify_package: every file's checksum, the package
    // hash), decodes the region layers and checks the parts this build relies on: 2,048 m detail tiles with a
    // whole number of samples, a region "elevation" layer of the manifest's size without no-data cells. If
    // `expected_map_id` is not empty, the manifest's map id must equal it. Throws MapFormatError.
    static std::shared_ptr<const LoadedMap> open(const std::filesystem::path& dir,
                                                 std::string_view expected_map_id = {});

    LoadedMap(const LoadedMap&) = delete;
    LoadedMap& operator=(const LoadedMap&) = delete;
    LoadedMap(LoadedMap&&) = delete;
    LoadedMap& operator=(LoadedMap&&) = delete;
    ~LoadedMap() = default;

    const std::filesystem::path& dir() const { return dir_; }
    const PackageManifest& manifest() const { return manifest_; }
    const std::string& map_id() const { return manifest_.map_id; }
    std::uint64_t package_hash() const { return manifest_.package_hash; }

    double width_m() const { return manifest_.bounds_width_m; }
    double height_m() const { return manifest_.bounds_height_m; }
    // Tiles that touch the map bounds: [0, tiles_x) x [0, tiles_y). The last row and column may stick out
    // past the bounds; heights there come from the region raster's edge.
    std::int32_t tiles_x() const { return tiles_x_; }
    std::int32_t tiles_y() const { return tiles_y_; }
    bool contains_tile(TileCoord tile) const {
        return tile.tx >= 0 && tile.ty >= 0 && tile.tx < tiles_x_ && tile.ty < tiles_y_;
    }

    // A region layer decoded to float (no-data cells are NaN), or nullptr if the package has no such layer.
    const Raster<float>* region_layer(std::string_view name) const;
    const Raster<float>& region_elevation() const { return *elevation_; }
    // Bilinear between region cell centers (see above). NaN only where a neighboring cell is no-data.
    float region_value(const Raster<float>& layer, WorldPos pos) const;
    float region_height(WorldPos pos) const { return region_value(*elevation_, pos); }

    // The river courses from rivers.json (empty if the map has none). Part of the immutable map data.
    const RiverNetwork& rivers() const { return rivers_; }

    // Samples along one side of a detail tile (65 for 32 m spacing) and their spacing in meters.
    std::uint32_t detail_samples() const { return detail_samples_; }
    double detail_spacing_m() const { return manifest_.detail.sample_spacing_m; }
    bool has_detail_tile(TileCoord tile) const;
    // The detail tile's elevation decoded to meters, loaded on first use; nullptr if the package has no such
    // tile. Throws MapFormatError if the file is damaged.
    std::shared_ptr<const Raster<float>> detail_tile(TileCoord tile) const;

    // The tiles whose closed square holds `pos`, in the order that decides which one answers (see above).
    struct Candidates {
        std::array<TileCoord, 4> tiles;
        std::size_t count = 0;
    };
    static Candidates covering_tiles(WorldPos pos);
    // Bilinear in one detail tile, at a point inside (or on the edge of) its closed square.
    float detail_value(const Raster<float>& tile, TileCoord coord, WorldPos pos) const;

    // The 32 m detail DEM where a detail tile covers the point, else the region elevation. The height used
    // outside the active city tiles, and the input of the placeholder synthesis.
    float dem_height(WorldPos pos) const;

  private:
    LoadedMap() = default;

    std::filesystem::path dir_;
    PackageManifest manifest_;
    std::int32_t tiles_x_ = 0;
    std::int32_t tiles_y_ = 0;
    std::uint32_t detail_samples_ = 0;
    std::map<std::string, Raster<float>, std::less<>> region_layers_;
    const Raster<float>* elevation_ = nullptr;
    std::vector<TileCoord> detail_tiles_; // sorted
    RiverNetwork rivers_;

    mutable std::mutex detail_mutex_;
    mutable std::map<TileCoord, std::shared_ptr<const Raster<float>>> detail_cache_;
};

} // namespace citysim::world
