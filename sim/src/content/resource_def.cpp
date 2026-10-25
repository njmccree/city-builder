#include "citysim/content/resource_def.hpp"

#include <cmath>

namespace citysim::content {

namespace {

constexpr std::size_t k_min_stops = 2;
constexpr std::size_t k_max_stops = 8;
constexpr std::string_view k_id_prefix = "resource.";

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool parse_color(const std::string& text, std::array<std::uint8_t, 3>& out) {
    if (text.size() != 7 || text[0] != '#') {
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        const int high = hex_digit(text[1 + 2 * i]);
        const int low = hex_digit(text[2 + 2 * i]);
        if (high < 0 || low < 0) {
            return false;
        }
        out[i] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return true;
}

// The name part of an id: "base:resource.ore_copper" -> "ore_copper" ("resource." dropped if there).
std::string default_layer(const ContentId& id) {
    const std::string& text = id.str();
    const std::size_t colon = text.find(':');
    std::string name = colon == std::string::npos ? text : text.substr(colon + 1);
    if (name.compare(0, k_id_prefix.size(), k_id_prefix) == 0) {
        name.erase(0, k_id_prefix.size());
    }
    return name;
}

} // namespace

const char* resource_kind_name(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::Soil:
        return "soil";
    case ResourceKind::Stone:
        return "stone";
    case ResourceKind::Ore:
        return "ore";
    case ResourceKind::Fuel:
        return "fuel";
    case ResourceKind::Vegetation:
        return "vegetation";
    }
    return "stone";
}

const char* resource_renewal_name(ResourceRenewal renewal) {
    switch (renewal) {
    case ResourceRenewal::None:
        return "none";
    case ResourceRenewal::Silt:
        return "silt";
    case ResourceRenewal::Regrowth:
        return "regrowth";
    }
    return "none";
}

ResourceDef parse_resource_def(const nlohmann::json& json, ParseContext& context) {
    ResourceDef resource;
    resource.id = context.def_id();
    context.warn_unknown_keys(json, {"type", "id", "patch", "name_key", "desc_key", "kind", "layer", "ramp",
                                     "renewable", "renewal_per_year"});
    resource.name_key = context.read_string(json, "name_key").value_or("");
    resource.desc_key = context.read_string(json, "desc_key").value_or("");

    if (const auto kind = context.read_string(json, "kind")) {
        if (*kind == "soil") {
            resource.kind = ResourceKind::Soil;
        } else if (*kind == "stone") {
            resource.kind = ResourceKind::Stone;
        } else if (*kind == "ore") {
            resource.kind = ResourceKind::Ore;
        } else if (*kind == "fuel") {
            resource.kind = ResourceKind::Fuel;
        } else if (*kind == "vegetation") {
            resource.kind = ResourceKind::Vegetation;
        } else {
            context.error(context.child_pointer("kind"),
                          "must be soil, stone, ore, fuel or vegetation, not '" + *kind + "'");
        }
    }

    resource.layer = context.read_string(json, "layer", false).value_or(default_layer(resource.id));

    if (const auto it = json.find("ramp"); it == json.end()) {
        context.error("missing required field 'ramp'");
    } else if (!it->is_array()) {
        context.error(context.child_pointer("ramp"), "expected an array of \"#rrggbb\" colors");
    } else {
        for (std::size_t i = 0; i < it->size(); ++i) {
            const std::string pointer = context.child_pointer("ramp") + "/" + std::to_string(i);
            std::array<std::uint8_t, 3> color{};
            if (!(*it)[i].is_string() || !parse_color((*it)[i].get<std::string>(), color)) {
                context.error(pointer, "expected a color like \"#b87333\"");
                continue;
            }
            resource.ramp.push_back(color);
        }
    }

    if (const auto renewable = context.read_string(json, "renewable", false)) {
        if (*renewable == "none") {
            resource.renewable = ResourceRenewal::None;
        } else if (*renewable == "silt") {
            resource.renewable = ResourceRenewal::Silt;
        } else if (*renewable == "regrowth") {
            resource.renewable = ResourceRenewal::Regrowth;
        } else {
            context.error(context.child_pointer("renewable"),
                          "must be none, silt or regrowth, not '" + *renewable + "'");
        }
    }
    resource.renewal_per_year = context.read_number(json, "renewal_per_year", false).value_or(0.0);
    return resource;
}

void validate_resource_def(const ResourceDef& resource, const DefRegistry<ResourceDef>&,
                           ParseContext& context) {
    bool layer_ok = !resource.layer.empty();
    for (const char c : resource.layer) {
        layer_ok = layer_ok && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_');
    }
    if (!layer_ok) {
        context.error(context.child_pointer("layer"),
                      "must be lower-case letters, digits and '_' ('" + resource.layer + "')");
    }
    if (resource.ramp.size() < k_min_stops || resource.ramp.size() > k_max_stops) {
        context.error(context.child_pointer("ramp"),
                      "needs 2 to 8 colors, has " + std::to_string(resource.ramp.size()));
    }
    if (!(resource.renewal_per_year >= 0.0 && resource.renewal_per_year <= 1.0) ||
        !std::isfinite(resource.renewal_per_year)) {
        context.error(context.child_pointer("renewal_per_year"), "must be between 0 and 1");
    }
}

} // namespace citysim::content
