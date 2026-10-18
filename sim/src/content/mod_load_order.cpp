#include "citysim/content/mod_load_order.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace citysim::content {

bool LoadOrderResult::ok() const {
    return std::none_of(issues.begin(), issues.end(),
                        [](const ContentIssue& i) { return i.severity == Severity::Error; });
}

namespace {

constexpr const char* k_base_id = "base";

ContentIssue issue_for(const ModInfo& mod, std::string pointer, std::string message) {
    return ContentIssue{Severity::Error, mod.has_manifest_file ? "mod.json" : "", std::move(pointer),
                        std::move(message), mod.root.generic_string()};
}

} // namespace

LoadOrderResult resolve_load_order(const std::vector<ModInfo>& discovered, const LoadOrderOptions& options) {
    LoadOrderResult result;

    std::map<std::string, const ModInfo*> by_id; // ordered by id
    for (const ModInfo& mod : discovered) {
        by_id.emplace(mod.manifest.id, &mod);
    }

    // Which mods are enabled.
    std::set<std::string> enabled;
    if (options.enable_all) {
        for (const auto& entry : by_id) {
            enabled.insert(entry.first);
        }
    } else if (options.enabled) {
        for (const std::string& id : *options.enabled) {
            if (by_id.contains(id)) {
                enabled.insert(id);
            } else {
                result.issues.push_back(
                    ContentIssue{Severity::Error, "", "", "enabled mod '" + id + "' was not found", ""});
            }
        }
    } else {
        for (const auto& [id, mod] : by_id) {
            if (mod->manifest.enabled_by_default) {
                enabled.insert(id);
            }
        }
    }
    if (options.require_base && !enabled.contains(k_base_id)) {
        result.issues.push_back(ContentIssue{
            Severity::Error, "", "", "the base game (mod 'base') is required but is not enabled", ""});
    }

    // Edges "first -> second" between enabled mods, as a set so a repeated declaration counts once.
    std::set<std::pair<std::string, std::string>> edges;
    for (const std::string& id : enabled) {
        const ModInfo& mod = *by_id.at(id);
        const ModManifest& manifest = mod.manifest;

        for (std::size_t i = 0; i < manifest.depends_on.size(); ++i) {
            const std::string& dep = manifest.depends_on[i];
            const std::string pointer = "/depends_on/" + std::to_string(i);
            if (dep == id) {
                result.issues.push_back(issue_for(mod, pointer, "a mod cannot depend on itself"));
            } else if (enabled.contains(dep)) {
                edges.emplace(dep, id);
            } else if (by_id.contains(dep)) {
                result.issues.push_back(issue_for(mod, pointer,
                                                  "mod '" + id + "' depends on '" + dep +
                                                      "', which is installed but not enabled"));
            } else {
                result.issues.push_back(issue_for(
                    mod, pointer, "mod '" + id + "' depends on '" + dep + "', which was not found"));
            }
        }
        for (const std::string& after : manifest.load_after) {
            if (after != id && enabled.contains(after)) { // optional ordering: ignored when absent
                edges.emplace(after, id);
            }
        }
        if (enabled.contains(k_base_id) && id != k_base_id) {
            edges.emplace(k_base_id, id);
        }
    }

    // Kahn's algorithm. `ready` is a sorted set, so the smallest id among the available mods always goes
    // next.
    std::map<std::string, std::size_t> unmet;
    std::map<std::string, std::vector<std::string>> successors;
    for (const std::string& id : enabled) {
        unmet[id] = 0;
    }
    for (const auto& [first, second] : edges) {
        ++unmet[second];
        successors[first].push_back(second);
    }
    std::set<std::string> ready;
    for (const auto& [id, count] : unmet) {
        if (count == 0) {
            ready.insert(id);
        }
    }
    std::vector<std::string> order;
    while (!ready.empty()) {
        const std::string next = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(next);
        for (const std::string& following : successors[next]) {
            if (--unmet[following] == 0) {
                ready.insert(following);
            }
        }
    }

    if (order.size() != enabled.size()) {
        // Everything left over is in a cycle or waits on one. Every leftover mod has a leftover predecessor,
        // so walking back from the smallest id (always taking the smallest predecessor) must come round to a
        // node it has seen.
        std::set<std::string> leftover;
        for (const std::string& id : enabled) {
            if (std::find(order.begin(), order.end(), id) == order.end()) {
                leftover.insert(id);
            }
        }
        std::vector<std::string> walk{*leftover.begin()};
        for (;;) {
            std::string predecessor;
            for (const auto& [first, second] :
                 edges) { // sorted, so the first match is the smallest predecessor
                if (second == walk.back() && leftover.contains(first)) {
                    predecessor = first;
                    break;
                }
            }
            const auto seen = std::find(walk.begin(), walk.end(), predecessor);
            if (seen != walk.end()) {
                // The walk goes from a mod to the one it must come after, so the arrows read "needs".
                std::vector<std::string> cycle(seen, walk.end());
                std::string text = "dependency cycle: ";
                for (const std::string& id : cycle) {
                    text += id + " -> ";
                }
                text += cycle.front() + " (each mod loads after the next)";
                if (std::find(cycle.begin(), cycle.end(), k_base_id) != cycle.end()) {
                    text += " (the base mod always loads first)";
                }
                result.issues.push_back(issue_for(*by_id.at(cycle.front()), "", text));
                break;
            }
            walk.push_back(predecessor);
        }
    }

    if (result.ok()) {
        for (const std::string& id : order) {
            result.ordered.push_back(*by_id.at(id));
        }
    }
    return result;
}

} // namespace citysim::content
