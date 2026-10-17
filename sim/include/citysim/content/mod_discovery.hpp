#pragma once

#include "citysim/content/content_issue.hpp"
#include "citysim/content/mod_manifest.hpp"

#include <filesystem>
#include <vector>

namespace citysim::content {

// A mod found on disk.
struct ModInfo {
    ModManifest manifest;
    std::filesystem::path root;
    bool has_manifest_file = true; // false for a folder accepted without mod.json (id = folder name)
};

struct DiscoveryResult {
    std::vector<ModInfo> mods; // in the order found: roots in the order given, mods by folder name
    std::vector<ContentIssue> issues;
};

// Finds mods under `roots`. For each root:
//   * if the root itself holds mod.json (or defs/ or defines/), it is a mod;
//   * otherwise every immediate subfolder that does is a mod, in folder-name order.
// A folder with defs/ or defines/ but no mod.json is accepted with the folder name as its id and default
// settings (handy for tests and quick experiments; shipped mods should have a manifest). Two mods with the
// same id are an error. A mod.json that cannot be read or parsed is an error and that folder is skipped.
DiscoveryResult discover_mods(const std::vector<std::filesystem::path>& roots);

// Builds the ModInfo for a single mod folder, as discover_mods does for each one it finds. Returns nullopt,
// after reporting on `issues`, if the manifest is broken.
std::optional<ModInfo> read_mod_folder(const std::filesystem::path& folder,
                                       std::vector<ContentIssue>& issues);

} // namespace citysim::content
