#pragma once

#include "citysim/content/content_issue.hpp"
#include "citysim/content/def_kind.hpp"
#include "citysim/content/era_def.hpp"
#include "citysim/content/mod_discovery.hpp"
#include "citysim/content/mod_load_order.hpp"
#include "citysim/time/time_defines.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <vector>

namespace citysim::content {

struct LoadResult {
    std::vector<ContentIssue> errors;
    std::vector<ContentIssue> warnings;
    bool ok() const { return errors.empty(); }
};

// Owns every definition registry and loads content into them (architecture §8). Content loads once, before
// the simulation starts; afterwards the database is read-only. Not tied to a clock.
//
// Mods layer on each other (architecture §8). They load in dependency order, `base` first. A definition whose
// id already exists replaces it when it comes from a later mod, and a definition with "patch": true is
// applied to the earlier one as a JSON Merge Patch (RFC 7386), before anything is parsed. Two definitions
// with the same id in one mod are an error.
struct ModLoadRequest {
    std::vector<std::filesystem::path> roots; // folders to search for mods (see discover_mods)
    LoadOrderOptions order;                   // which mods are enabled; default: those enabled by default
};

class ContentDatabase {
  public:
    // Registers the built-in definition types (era).
    ContentDatabase();

    // Registers a definition type: files may then contain {"type": <name>, ...}. `resolve` fills in
    // references once every file is loaded; `validate` checks each definition after that. Both are optional.
    // Throws std::logic_error if the name or the C++ type is already registered, or after content has been
    // loaded.
    template <class T>
    void register_def_type(std::string name, ParseFn<T> parse, ResolveFn<T> resolve = nullptr,
                           ValidateFn<T> validate = nullptr) {
        if (loaded_) {
            throw std::logic_error("register_def_type after content was loaded");
        }
        if (by_name_.contains(name) || by_type_.contains(std::type_index(typeid(T)))) {
            throw std::logic_error("definition type already registered: " + name);
        }
        auto kind =
            std::make_unique<DefKind<T>>(name, std::move(parse), std::move(resolve), std::move(validate));
        by_type_.emplace(std::type_index(typeid(T)), kind.get());
        by_name_.emplace(std::move(name), kind.get());
        kinds_.push_back(std::move(kind));
    }

    // Finds mods under the request's roots, decides which load and in what order, and loads them. Reports
    // everything wrong, in a fixed order: discovery and load-order problems, then problems found while
    // reading files (mods in load order, files in path order), then problems in the parsed definitions (per
    // type, by id). Can be called once, by this or by load().
    LoadResult load_mods(const ModLoadRequest& request);

    // Loads each folder as a mod, in the order given, with no dependency handling: a lower-level entry point
    // for tests and tools. A folder without mod.json is named after its folder. Can be called once.
    LoadResult load(const std::vector<std::filesystem::path>& mod_roots);

    // The mods that were loaded, in load order.
    const std::vector<ModManifest>& mods() const { return mods_; }

    // A 64-bit XXH3 fingerprint of what was loaded: the ordered mod ids and versions plus the relative path
    // and text of every content file of each (mod.json, defs/, defines/, loc/). Line endings are normalized,
    // so a checkout with CRLF hashes the same as one with LF. Saves store it to warn about mismatched
    // content.
    std::uint64_t content_hash() const { return content_hash_; }

    // Where a definition came from: the last file that wrote or patched it, and every mod that touched it.
    template <class T> const DefSource& origin(DefHandle<T> handle) const {
        const DefSource* source = kind_for<T>().source_of(registry<T>().id(handle));
        if (source == nullptr) {
            throw std::logic_error("definition has no recorded origin");
        }
        return *source;
    }

    template <class T> const DefRegistry<T>& registry() const {
        const auto it = by_type_.find(std::type_index(typeid(T)));
        if (it == by_type_.end()) {
            throw std::logic_error("definition type not registered");
        }
        return static_cast<const DefKind<T>*>(it->second)->registry();
    }

    // From defines/time.json, or the built-in defaults when the file is absent.
    const time::TimeDefines& time_defines() const { return time_defines_; }

  private:
    // One definition as read from files, layered but not parsed yet.
    struct RawDef {
        nlohmann::json json;
        DefSource source;
    };
    using RawDefs = std::map<std::pair<std::string, std::string>, RawDef>; // keyed by (type, id)

    LoadResult load_ordered(const std::vector<ModInfo>& mods, std::vector<ContentIssue> issues);
    void collect_mod_defs(const ModInfo& mod, RawDefs& raw, std::vector<ContentIssue>& issues);
    void collect_defs_file(const ModInfo& mod, const std::filesystem::path& path,
                           const std::string& display_name, RawDefs& raw, std::vector<ContentIssue>& issues);
    void load_time_defines(const std::filesystem::path& path, const std::string& mod_root,
                           const std::string& display_name, std::vector<ContentIssue>& issues);
    void finish(std::vector<ContentIssue>& issues);

    std::string type_name_of(std::type_index type) const;

    template <class T> const DefKind<T>& kind_for() const {
        const auto it = by_type_.find(std::type_index(typeid(T)));
        if (it == by_type_.end()) {
            throw std::logic_error("definition type not registered");
        }
        return *static_cast<const DefKind<T>*>(it->second);
    }

    std::vector<std::unique_ptr<IDefKind>> kinds_; // registration order: the order registries finalize in
    std::map<std::string, IDefKind*> by_name_;
    std::map<std::type_index, IDefKind*> by_type_;
    time::TimeDefines time_defines_;
    std::vector<ModManifest> mods_;
    std::uint64_t content_hash_ = 0;
    bool loaded_ = false;
};

} // namespace citysim::content
