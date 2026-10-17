#pragma once

#include "citysim/content/content_issue.hpp"
#include "citysim/content/mod_discovery.hpp"

#include <optional>
#include <string>
#include <vector>

namespace citysim::content {

struct LoadOrderOptions {
    // Ids of the mods to load. nullopt = every discovered mod whose manifest has enabled_by_default (the
    // default).
    std::optional<std::vector<std::string>> enabled;
    // Load every discovered mod, including those that are off by default. For tools that check content
    // (the validator). Takes precedence over `enabled`.
    bool enable_all = false;
    // The base game is the mod "base" and always loads first when present. Set this to make its absence an
    // error.
    bool require_base = false;
};

struct LoadOrderResult {
    std::vector<ModInfo> ordered; // empty if there were errors
    std::vector<ContentIssue> issues;
    bool ok() const;
};

// Decides which mods load and in what order (architecture §8).
//   * Every mod comes after the mods in its depends_on; a dependency that is missing or not enabled is an
//   error.
//   * Every mod comes after the enabled mods in its load_after; ones that are not enabled are ignored.
//   * `base` comes before every other mod.
//   * Among mods that could go next, the smallest id goes first, so the order is the same on every machine.
//   * A cycle is an error that names it, e.g. "dependency cycle: a -> b -> c -> a (each mod loads after the
//   next)".
LoadOrderResult resolve_load_order(const std::vector<ModInfo>& discovered,
                                   const LoadOrderOptions& options = {});

} // namespace citysim::content
