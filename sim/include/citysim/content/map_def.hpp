#pragma once

#include "citysim/content/content_id.hpp"
#include "citysim/content/def_registry.hpp"
#include "citysim/content/parse_context.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace citysim::content {

struct DefSource;

// A longitude and latitude in degrees (WGS 84).
struct GeoPoint {
    double lon = 0.0;
    double lat = 0.0;

    friend bool operator==(const GeoPoint&, const GeoPoint&) = default;
};

// A map (architecture §8, §15.2): the definition the rest of the game refers to. The data itself is a
// generated "map package" in `<mod folder>/maps/<package>/`, which is not committed (the offline pipeline
// builds it), so this definition says which package belongs to it and what the map covers.
//
// In a defs file:
//
//   { "type": "map", "id": "base:map.ancient_near_east",
//     "name_key": "map.ancient_near_east.name", "desc_key": "map.ancient_near_east.desc",
//     "package": "ancient_near_east",
//     "bounds": { "min_lon": 25.0, "min_lat": 20.0, "max_lon": 55.0, "max_lat": 40.0 },
//     "region_cell_size_m": 1000,            // optional, default 1000
//     "map_seed": 1234567,                   // drives the terrain's micro-detail, so a map is the same in
//                                            // every game; not the game seed
//     "detail_regions": [                    // polygons in lon/lat, where 4 m city terrain can be made
//       [[29.5, 29.0], [32.5, 29.0], [32.5, 31.8], [29.5, 31.8], [29.5, 29.0]]
//     ],
//     "river_regimes": ["base:river.nile"] } // plain strings until RiverRegimeDef exists (T1.3.2.1)
//
// A polygon is a closed ring in the GeoJSON sense: its last point repeats its first, and it has at least
// three different corners (so at least four points). Every `*_key` field needs text in "en", like any
// definition.
struct MapDef {
    ContentId id;
    std::string name_key;
    std::string desc_key;
    std::string package; // the folder name under the mod's maps/: letters, digits, '_' and '-'
    double min_lon = 0.0;
    double min_lat = 0.0;
    double max_lon = 0.0;
    double max_lat = 0.0;
    double region_cell_size_m = 1000.0;
    std::uint64_t map_seed = 0;
    std::vector<std::vector<GeoPoint>> detail_regions;
    // Ids of river regimes (annual water-level curves); kept as text until T1.3.2.1 adds the definition type.
    std::vector<std::string> river_regimes;
};

MapDef parse_map_def(const nlohmann::json& json, ParseContext& context);
// Bounds inside the globe and not empty, a positive cell size, a safe package folder name, closed polygons
// with at least three corners that lie inside the bounds' longitude and latitude range.
void validate_map_def(const MapDef& map, const DefRegistry<MapDef>& all, ParseContext& context);

// Where the package of `map` lives: `<the mod folder the definition came from>/maps/<package>`.
std::filesystem::path map_package_dir(const DefSource& source, const MapDef& map);

} // namespace citysim::content
