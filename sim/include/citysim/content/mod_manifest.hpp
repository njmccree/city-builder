#pragma once

#include "citysim/content/parse_context.hpp"

#include <optional>
#include <string>
#include <vector>

namespace citysim::content {

// What mod.json declares about a mod (architecture §8).
struct ModManifest {
    std::string id;                      // lowercase a-z, 0-9, '_' and '.'; no ':'
    std::string name_key;                // localization key for the display name
    std::string version;                 // semantic version, e.g. "1.2.0" or "0.1.0-beta"
    std::vector<std::string> depends_on; // must be enabled, and load before this mod
    std::vector<std::string> load_after; // optional ordering: load after these if they are enabled
    bool enabled_by_default = true;      // false for mods that ship in the repo but must be switched on
};

// True for ids made of lowercase a-z, digits, '_' and '.', non-empty. A mod id is the part before ':' in the
// ids of the definitions it adds.
bool is_valid_mod_id(std::string_view id);

// True for "MAJOR.MINOR.PATCH" with optional "-prerelease" and "+build" parts.
bool is_valid_semver(std::string_view version);

// Reads a manifest from the parsed mod.json. Reports every problem on `context` and returns nullopt if any.
std::optional<ModManifest> parse_mod_manifest(const nlohmann::json& json, ParseContext& context);

} // namespace citysim::content
