#include "citysim/content/map_def.hpp"

#include "citysim/content/content_database.hpp"

#include <cmath>

namespace citysim::content {

namespace {

bool is_safe_folder_name(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

} // namespace

MapDef parse_map_def(const nlohmann::json& json, ParseContext& context) {
    MapDef map;
    map.id = context.def_id();
    context.warn_unknown_keys(json, {"type", "id", "patch", "name_key", "desc_key", "package", "bounds",
                                     "region_cell_size_m", "map_seed", "detail_regions", "river_regimes"});
    map.name_key = context.read_string(json, "name_key").value_or("");
    map.desc_key = context.read_string(json, "desc_key").value_or("");
    map.package = context.read_string(json, "package").value_or("");
    map.region_cell_size_m = context.read_number(json, "region_cell_size_m", false).value_or(1000.0);
    map.map_seed = context.read_u64(json, "map_seed").value_or(0);

    // bounds: { min_lon, min_lat, max_lon, max_lat }
    if (const auto it = json.find("bounds"); it == json.end()) {
        context.error("missing required field 'bounds'");
    } else if (!it->is_object()) {
        context.error(context.child_pointer("bounds"), "expected an object");
    } else {
        const std::string base = context.pointer();
        // The reads below report at the pointer of the object they are given, so move there for them.
        context.set_pointer(context.child_pointer("bounds"));
        map.min_lon = context.read_number(*it, "min_lon").value_or(0.0);
        map.min_lat = context.read_number(*it, "min_lat").value_or(0.0);
        map.max_lon = context.read_number(*it, "max_lon").value_or(0.0);
        map.max_lat = context.read_number(*it, "max_lat").value_or(0.0);
        context.set_pointer(base);
    }

    // detail_regions: [ [ [lon, lat], ... ], ... ]
    if (const auto it = json.find("detail_regions"); it == json.end()) {
        context.error("missing required field 'detail_regions'");
    } else if (!it->is_array()) {
        context.error(context.child_pointer("detail_regions"), "expected an array of polygons");
    } else {
        for (std::size_t p = 0; p < it->size(); ++p) {
            const std::string polygon_pointer = context.child_pointer("detail_regions") + "/" + std::to_string(p);
            const nlohmann::json& ring = (*it)[p];
            if (!ring.is_array()) {
                context.error(polygon_pointer, "expected an array of [lon, lat] points");
                continue;
            }
            std::vector<GeoPoint> points;
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const std::string point_pointer = polygon_pointer + "/" + std::to_string(i);
                const nlohmann::json& point = ring[i];
                if (!point.is_array() || point.size() != 2 || !point[0].is_number() || !point[1].is_number()) {
                    context.error(point_pointer, "expected a point as [lon, lat]");
                    continue;
                }
                points.push_back(GeoPoint{point[0].get<double>(), point[1].get<double>()});
            }
            map.detail_regions.push_back(std::move(points));
        }
    }

    // river_regimes: [ "id", ... ]
    if (const auto it = json.find("river_regimes"); it != json.end()) {
        if (!it->is_array()) {
            context.error(context.child_pointer("river_regimes"), "expected an array of ids");
        } else {
            for (std::size_t i = 0; i < it->size(); ++i) {
                const std::string pointer = context.child_pointer("river_regimes") + "/" + std::to_string(i);
                if (!(*it)[i].is_string() || (*it)[i].get<std::string>().empty()) {
                    context.error(pointer, "expected an id");
                    continue;
                }
                map.river_regimes.push_back((*it)[i].get<std::string>());
            }
        }
    }
    return map;
}

void validate_map_def(const MapDef& map, const DefRegistry<MapDef>&, ParseContext& context) {
    if (!is_safe_folder_name(map.package)) {
        context.error(context.child_pointer("package"),
                      "must be a folder name made of letters, digits, '_' and '-' ('" + map.package + "')");
    }
    if (!(map.region_cell_size_m > 0.0) || !std::isfinite(map.region_cell_size_m)) {
        context.error(context.child_pointer("region_cell_size_m"), "must be greater than 0");
    }

    const std::string bounds = context.child_pointer("bounds");
    bool bounds_ok = true;
    if (!(map.min_lon >= -180.0 && map.max_lon <= 180.0 && map.min_lon < map.max_lon)) {
        context.error(bounds, "longitudes must satisfy -180 <= min_lon < max_lon <= 180");
        bounds_ok = false;
    }
    if (!(map.min_lat >= -90.0 && map.max_lat <= 90.0 && map.min_lat < map.max_lat)) {
        context.error(bounds, "latitudes must satisfy -90 <= min_lat < max_lat <= 90");
        bounds_ok = false;
    }

    for (std::size_t p = 0; p < map.detail_regions.size(); ++p) {
        const std::vector<GeoPoint>& ring = map.detail_regions[p];
        const std::string pointer = context.child_pointer("detail_regions") + "/" + std::to_string(p);
        if (ring.size() < 4) {
            context.error(pointer, "a polygon needs at least three corners and a closing point (4 points)");
            continue;
        }
        if (!(ring.front() == ring.back())) {
            context.error(pointer, "the polygon is not closed: its last point must repeat its first");
        }
        if (bounds_ok) {
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const GeoPoint& point = ring[i];
                if (point.lon < map.min_lon || point.lon > map.max_lon || point.lat < map.min_lat ||
                    point.lat > map.max_lat) {
                    context.error(pointer + "/" + std::to_string(i), "the point lies outside the map bounds");
                }
            }
        }
    }
}

std::filesystem::path map_package_dir(const DefSource& source, const MapDef& map) {
    return std::filesystem::path(source.mod_root) / "maps" / map.package;
}

} // namespace citysim::content
