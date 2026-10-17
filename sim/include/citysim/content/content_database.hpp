#pragma once

#include "citysim/content/content_issue.hpp"
#include "citysim/content/def_kind.hpp"
#include "citysim/content/era_def.hpp"
#include "citysim/time/time_defines.hpp"

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
// For now one mod root is loaded as-is. Mod discovery, load order, overrides and patches arrive in T0.4.2.1.
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

    // Loads each mod root in order (defs/**/*.json in sorted path order, then defines/time.json), finalizes
    // the registries, resolves references, validates every definition, and reports everything wrong. Problems
    // are listed in a fixed order: files in path order, then per type and id. Can be called once.
    LoadResult load(const std::vector<std::filesystem::path>& mod_roots);

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
    void load_mod_root(const std::filesystem::path& root, std::vector<ContentIssue>& issues);
    void load_defs_file(const std::filesystem::path& path, const std::string& mod_root,
                        const std::string& display_name, std::vector<ContentIssue>& issues);
    void load_time_defines(const std::filesystem::path& path, const std::string& mod_root,
                           const std::string& display_name, std::vector<ContentIssue>& issues);

    std::string type_name_of(std::type_index type) const;

    std::vector<std::unique_ptr<IDefKind>> kinds_; // registration order: the order registries finalize in
    std::map<std::string, IDefKind*> by_name_;
    std::map<std::type_index, IDefKind*> by_type_;
    time::TimeDefines time_defines_;
    bool loaded_ = false;
};

} // namespace citysim::content
