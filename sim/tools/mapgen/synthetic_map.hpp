#pragma once

#include "citysim/world/map_package.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace citysim::mapgen {

// What `citysim_mapgen --synthetic` makes: a small, fictional but plausible map package that needs no real
// data, so tests, CI and cloud sessions can use the whole terrain stack (architecture §15.2). Real packages
// come from the PC pipeline (`tools/terrain`).
//
// The map is size_km x size_km (1,000 m region cells), in the world frame (meters from the south-west corner,
// x east, z north):
//   - a river valley running south to north, 3 km wide in the south and widening into a delta of about 29 km
//     near the coast, with desert plateaus 60-120 m above the valley floor on both sides;
//   - a coast along the north edge: the sea (below 0 m) starts at about 82-88% of the way north;
//   - one river with a main channel that splits into three delta branches that reach the sea;
//   - detail tiles over the valley and delta (the "detail region": the middle 30% of the width, from 40% to
//   90% of the way north).
//
// Everything comes from `citysim::rng` (counter-based hashes, so nothing depends on call order) and from
// plain arithmetic, so one seed always gives the same files. Clock: none, this is an offline tool.
struct SyntheticSpec {
    std::uint64_t seed = 1;
    double size_km = 96.0;
};

inline constexpr double k_min_size_km = 16.0;
inline constexpr double k_max_size_km = 1000.0;

// The text of the problem with a spec, or an empty string if it is fine.
std::string check_spec(const SyntheticSpec& spec);

// Writes a complete package into `out_dir` (created if needed) and returns its manifest. Also writes
// `map_def.json` next to `package.json`: a map definition ("package": "synthetic") for it. That file is not
// part of the package, so it is not listed or hashed; a test content root puts the package in
// `maps/synthetic/` and the definition in `defs/`. Throws std::invalid_argument for a bad spec and
// world::MapFormatError if a file cannot be written.
world::PackageManifest write_synthetic_package(const SyntheticSpec& spec,
                                               const std::filesystem::path& out_dir);

// Writes a whole mod (`k_synthetic_mod_id`, depending on "base") holding the synthetic map into
// `content_root`: <root>/test/mod.json, defs/maps.json (the definition), loc/en/maps.json (its text) and
// maps/synthetic/ (the package). A game started with `content_root` among its content roots, the mod enabled
// and map id `k_synthetic_map_id` plays on it; that is how the editor tries the terrain before a real package
// exists (`citysim_mapgen --synthetic --mod game/content_dev`). Throws as write_synthetic_package does.
world::PackageManifest write_synthetic_mod(const SyntheticSpec& spec,
                                           const std::filesystem::path& content_root);

// The text of that map definition file.
std::string synthetic_map_def_text(const SyntheticSpec& spec);

// The id of the generated map definition, and the folder name its package goes in under `maps/`.
inline constexpr const char* k_synthetic_map_id = "test:map.synthetic";
inline constexpr const char* k_synthetic_package_folder = "synthetic";
// The mod write_synthetic_mod() makes.
inline constexpr const char* k_synthetic_mod_id = "test";

} // namespace citysim::mapgen
