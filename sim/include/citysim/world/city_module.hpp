#pragma once

#include "citysim/core/simulation.hpp"
#include "citysim/world/city_defines.hpp"
#include "citysim/world/city_registry.hpp"
#include "citysim/world/world_terrain.hpp"

#include <cstdint>
#include <string_view>

namespace citysim::world {

// Wires cities into a simulation that already has the WorldTerrain singleton (world::install calls this):
// registers "world.Cities", "world.ActiveCity" and "world.CityDefines" for hashing and saving, emplaces them
// (no cities, no active city) and adds the FoundCity and SetActiveCity handlers. Throws std::invalid_argument
// for unusable defines (a land fraction outside 0..1, no cities allowed).
void install_cities(core::Simulation& simulation, const CityDefines& defines);
void validate(const CityDefines& defines);

// The tiles that are active terrain while a city is the active one: its 4 x 4 plus `ring` tiles around, kept
// to the tiles on the map, sorted by TileCoord.
std::vector<TileCoord> city_active_tiles(const LoadedMap& map, const CityArea& area, std::uint32_t ring);

// Why FoundCity would fail for this area right now, as a localization key, or an empty view if it would
// succeed. The same checks the command makes, in the same order, without changing anything: a UI can show the
// reason while the player picks a site. `name_bytes` is the length of the name to be used.
std::string_view found_city_problem(const WorldTerrain& terrain, const CityRegistry& registry,
                                    const CityDefines& defines, TileCoord origin, std::size_t name_bytes);

inline constexpr const char* k_error_limit = "city.found.error.limit";
inline constexpr const char* k_error_name = "city.found.error.name";
inline constexpr const char* k_error_out_of_bounds = "city.found.error.out_of_bounds";
inline constexpr const char* k_error_no_detail = "city.found.error.no_detail";
inline constexpr const char* k_error_overlap = "city.found.error.overlap";
inline constexpr const char* k_error_not_land = "city.found.error.not_land";
inline constexpr const char* k_error_unknown_city = "city.active.error.unknown";

} // namespace citysim::world
