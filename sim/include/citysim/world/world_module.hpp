#pragma once

#include "citysim/content/content_database.hpp"
#include "citysim/content/map_def.hpp"
#include "citysim/core/simulation.hpp"
#include "citysim/world/city_defines.hpp"
#include "citysim/world/loaded_map.hpp"
#include "citysim/world/world_terrain.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace citysim::world {

struct WorldConfig {
    // The MapDef of the map to play on, e.g. "base:map.ancient_near_east".
    std::string map_id;
    // The map already opened with open_map(), or null to open it here. A game session opens it once and
    // passes it to every Simulation it builds (a load builds a new one), so the package is read only once.
    std::shared_ptr<const LoadedMap> map;
    // Founding and activating cities (the demesne limit, the land needed, the ring of tiles). Content fills
    // it from defines/world.json; these are the built-in defaults.
    CityDefines cities{};
};

// The MapDef with this id. Throws std::invalid_argument if the content has none.
const content::MapDef& find_map_def(const content::ContentDatabase& content, std::string_view map_id);

// Opens the package of a map by its MapDef id: `<mod folder>/maps/<package>` (content::map_package_dir),
// which must hold a valid package for that map id. Throws std::invalid_argument for an unknown map id and
// MapFormatError for a missing or damaged package.
std::shared_ptr<const LoadedMap> open_map(const content::ContentDatabase& content, std::string_view map_id);

// Installs the world module (architecture §15) into a simulation: registers the WorldTerrain singleton as
// "world.Terrain" (hashed and saved: map id, package hash, generator version, active tiles) and emplaces it
// with the map and no active tiles, and adds an after-load hook that restores the terrain from a save
// (restore_terrain). Call it on every simulation that may load a save made by one that called it.
//
// `content` must outlive the simulation. Throws std::invalid_argument for an empty or unknown map id or a
// `config.map` of another map, MapFormatError if the package cannot be opened.
void install(core::Simulation& simulation, const content::ContentDatabase& content,
             const WorldConfig& config);

// Brings `terrain` to the saved state: switches to the saved map if it is another one (opened through
// `content`; an unknown map fails the load with save::SaveFormatError), activates the saved tiles again, and
// returns warnings, never errors, when the terrain may differ from what was saved: a different package hash
// (the package was rebuilt) or generator version (this build synthesizes differently), and saved tiles that
// no longer lie on the map (skipped).
std::vector<std::string> restore_terrain(WorldTerrain& terrain, const SavedTerrain& saved,
                                         const content::ContentDatabase& content, core::JobSystem& jobs);

} // namespace citysim::world
