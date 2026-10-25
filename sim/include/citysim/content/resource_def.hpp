#pragma once

#include "citysim/content/content_id.hpp"
#include "citysim/content/def_registry.hpp"
#include "citysim/content/parse_context.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace citysim::content {

enum class ResourceKind : std::uint8_t { Soil, Stone, Ore, Fuel, Vegetation };

// How a resource comes back after it was used (architecture §15.8): never, with the silt of the river's
// flood, or by regrowing.
enum class ResourceRenewal : std::uint8_t { None, Silt, Regrowth };

// A resource of the land that the player can look at and use up (architecture §15.8): soil, stone, ore,
// fuel, vegetation. Its amount at every 4 m sample of a city is a resource field (world::ResourceFields),
// sampled from the map package's region layer named `layer`.
//
// In a defs file:
//
//   { "type": "resource", "id": "base:resource.ore_copper",
//     "name_key": "resource.ore_copper.name", "desc_key": "resource.ore_copper.desc",
//     "kind": "ore",                              // soil | stone | ore | fuel | vegetation
//     "layer": "ore_copper",                      // optional: the package layer; default = the id's name
//                                                 // without a leading "resource." ("ore_copper")
//     "ramp": ["#2e1f14", "#b87333", "#f2c58a"],  // overlay colors from little to much, 2 to 8 stops
//     "#rrggbb" "renewable": "none",                        // none | silt | regrowth (optional, default
//     none) "renewal_per_year": 0.0 }                   // optional, 0..1: the share of what was used that
//     comes back
//                                                 // each year; read by the systems that renew (silt: the
//                                                 flood)
//
// Two resources may name the same layer (a "fertile soil" resource uses the "fertility" layer); they then
// share one field. Every *_key needs text in "en" like any definition.
struct ResourceDef {
    ContentId id;
    std::string name_key;
    std::string desc_key;
    ResourceKind kind = ResourceKind::Stone;
    std::string layer;
    std::vector<std::array<std::uint8_t, 3>> ramp; // RGB
    ResourceRenewal renewable = ResourceRenewal::None;
    double renewal_per_year = 0.0;
};

ResourceDef parse_resource_def(const nlohmann::json& json, ParseContext& context);
// A layer name of lower-case letters, digits and '_', at least two ramp stops, a rate within 0..1.
void validate_resource_def(const ResourceDef& resource, const DefRegistry<ResourceDef>& all,
                           ParseContext& context);

const char* resource_kind_name(ResourceKind kind);
const char* resource_renewal_name(ResourceRenewal renewal);

} // namespace citysim::content
