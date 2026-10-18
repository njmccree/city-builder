#pragma once

#include "citysim/world/map_format_error.hpp"
#include "citysim/world/tile_coord.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace citysim::world {

// A map package is the generated data of one map (architecture §15.2): region rasters, detail tiles, rivers
// and lakes, and `package.json`, which lists every file with its checksum. This header is the parsed form of
// `package.json` and the functions that read and verify a package. Packages are written by the offline
// pipeline (`tools/terrain`) and, for tests, by `citysim_mapgen --synthetic`.
//
// package.json (every field is required; unknown fields are ignored):
//
//   {
//     "format": 1,
//     "map_id": "base:map.ancient_near_east",
//     "pipeline_version": "1.0.0",
//     "sources": [
//       { "name": "Copernicus DEM GLO-30", "version": "2021_1", "license": "Copernicus DEM licence" }
//     ],
//     "projection": { "type": "laea", "lat_0": 30.0, "lon_0": 38.0 },
//     "bounds_m": { "width": 3000000.0, "height": 2000000.0 },
//     "region": {
//       "cell_size_m": 1000.0, "width": 3000, "height": 2000,
//       "layers": { "elevation": "region/elevation.csr", "land_mask": "region/land_mask.csr" }
//     },
//     "detail": { "tile_size_m": 2048.0, "sample_spacing_m": 32.0, "tiles": [[10, 20], [11, 20]] },
//     "rivers": "rivers.json",
//     "lakes": "lakes.json",
//     "files": {
//       "detail/10_20.csr": "0123456789abcdef",
//       "region/elevation.csr": "89abcdef01234567"
//     },
//     "package_hash": "fedcba9876543210"
//   }
//
// File paths are relative to the package folder, use '/', and never contain "..". Detail tile (tx, ty) is
// `detail/<tx>_<ty>.csr`. Every checksum is the XXH3-64 of the file's bytes as 16 lower-case hex digits.
// `package_hash` is XXH3-64 of the text "<path>=<checksum>\n" for each file in sorted path order (see
// compute_package_hash): it identifies the data, whatever the folder is called, and saves store it.

struct PackageSource {
    std::string name;
    std::string version;
    std::string license;

    friend bool operator==(const PackageSource&, const PackageSource&) = default;
};

struct PackageProjection {
    std::string type; // "laea": Lambert azimuthal equal-area, the only one supported
    double lat_0 = 0.0;
    double lon_0 = 0.0;

    friend bool operator==(const PackageProjection&, const PackageProjection&) = default;
};

struct PackageRegion {
    double cell_size_m = 0.0;
    std::uint32_t width = 0;                   // cells
    std::uint32_t height = 0;                  // cells
    std::map<std::string, std::string> layers; // layer name -> relative file

    friend bool operator==(const PackageRegion&, const PackageRegion&) = default;
};

struct PackageDetail {
    double tile_size_m = 0.0;
    double sample_spacing_m = 0.0;
    std::vector<TileCoord> tiles; // in the order the file lists them (the pipeline writes them sorted)

    friend bool operator==(const PackageDetail&, const PackageDetail&) = default;
};

struct PackageManifest {
    std::uint32_t format = 1;
    std::string map_id;
    std::string pipeline_version;
    std::vector<PackageSource> sources;
    PackageProjection projection;
    double bounds_width_m = 0.0;
    double bounds_height_m = 0.0;
    PackageRegion region;
    PackageDetail detail;
    std::string rivers_file;
    std::string lakes_file;
    std::map<std::string, std::uint64_t> files; // relative path -> XXH3-64 of the file
    std::uint64_t package_hash = 0;

    friend bool operator==(const PackageManifest&, const PackageManifest&) = default;
};

inline constexpr const char* k_package_file_name = "package.json";
inline constexpr std::uint32_t k_package_format_version = 1;

// 16 lower-case hex digits, and back. parse_hex_hash returns false for anything else.
std::string hash_to_hex(std::uint64_t hash);
bool parse_hex_hash(std::string_view text, std::uint64_t& out);

// XXH3-64 of a file's bytes, read in pieces. Throws MapFormatError if the file cannot be read.
std::uint64_t hash_file(const std::filesystem::path& path);

// The package hash of a file list (see above).
std::uint64_t compute_package_hash(const std::map<std::string, std::uint64_t>& files);

// `detail/<tx>_<ty>.csr`, relative to the package folder.
std::string detail_tile_file(TileCoord tile);

// Reads the manifest out of parsed JSON. `source` names the data in errors, which carry a JSON pointer:
// "package.json: /region/width: expected a positive whole number". Checks the fields and their ranges, not
// the files on disk.
PackageManifest parse_manifest(const nlohmann::json& json, const std::string& source);
// Reads `<dir>/package.json`.
PackageManifest read_manifest(const std::filesystem::path& dir);
// The manifest as JSON (keys in a fixed order, so the text is deterministic) and as the text written to
// package.json (two-space indent, trailing newline).
nlohmann::json manifest_to_json(const PackageManifest& manifest);
std::string manifest_to_text(const PackageManifest& manifest);

// Reads the manifest of the package in `dir` and checks it against the disk: every layer, tile, rivers and
// lakes file is listed, every listed file exists and has its checksum, there are no unlisted detail tiles,
// and `package_hash` matches the list. Throws MapFormatError naming the first few problems. Returns the
// manifest.
PackageManifest verify_package(const std::filesystem::path& dir);

} // namespace citysim::world
