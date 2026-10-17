#include "citysim/content/content_database.hpp"

#include "citysim/content/json_merge_patch.hpp"
#include "citysim/core/state_hasher.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>

namespace citysim::content {

namespace {

namespace fs = std::filesystem;

// Reads a whole file; strips a UTF-8 byte-order mark, which Windows editors like to add.
bool read_text_file(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

// Forward slashes on every platform, so messages and sort order are the same everywhere.
std::string display_path(const fs::path& path, const fs::path& root) {
    return fs::relative(path, root).generic_string();
}

// Reads and parses a JSON file; on failure reports why (with the parser's own message, which includes the
// byte offset) and returns nullopt.
std::optional<nlohmann::json> read_json_file(const fs::path& path, ParseContext& context) {
    std::string text;
    if (!read_text_file(path, text)) {
        context.error("", "cannot read file");
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception& error) {
        context.error("", std::string("invalid JSON: ") + error.what());
        return std::nullopt;
    }
}

// Every .json file under `dir`, sorted by path relative to `root`.
std::vector<fs::path> json_files_under(const fs::path& dir, const fs::path& root) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
    }
    std::sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) {
        return display_path(a, root) < display_path(b, root);
    });
    return files;
}

// Adds a mod's identity and every content file to the hash: mod.json, then the .json files of defs/, defines/
// and loc/, each as relative path and text. Text is hashed with CRLF turned into LF, so a Windows checkout
// with autocrlf gives the same hash as a Linux one.
void hash_mod(core::StateHasher& hasher, const ModInfo& mod) {
    hasher.add(mod.manifest.id);
    hasher.add(mod.manifest.version);

    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_regular_file(mod.root / "mod.json", ec)) {
        files.push_back(mod.root / "mod.json");
    }
    for (const char* folder : {"defs", "defines", "loc"}) {
        for (fs::path& file : json_files_under(mod.root / folder, mod.root)) {
            files.push_back(std::move(file));
        }
    }
    std::sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) {
        return display_path(a, mod.root) < display_path(b, mod.root);
    });

    hasher.add_pod(static_cast<std::uint64_t>(files.size()));
    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        std::string text = buffer.str();
        std::string normalized;
        normalized.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                continue; // drop the CR of a CRLF
            }
            normalized += text[i];
        }
        hasher.add(display_path(file, mod.root));
        hasher.add(normalized);
    }
}

void add_if_new(std::vector<std::string>& mods, const std::string& id) {
    if (mods.empty() || mods.back() != id) {
        mods.push_back(id);
    }
}

} // namespace

ContentDatabase::ContentDatabase() {
    register_def_type<EraDef>("era", parse_era_def, resolve_era_def, validate_era_def);
}

std::string ContentDatabase::type_name_of(std::type_index type) const {
    const auto it = by_type_.find(type);
    return it == by_type_.end() ? std::string("definition") : std::string(it->second->type_name());
}

LoadResult ContentDatabase::load_mods(const ModLoadRequest& request) {
    if (loaded_) {
        throw std::logic_error("ContentDatabase can only load once");
    }
    DiscoveryResult found = discover_mods(request.roots);
    LoadOrderResult order = resolve_load_order(found.mods, request.order);

    std::vector<ContentIssue> issues = std::move(found.issues);
    for (ContentIssue& issue : order.issues) {
        issues.push_back(std::move(issue));
    }
    return load_ordered(order.ordered, std::move(issues));
}

LoadResult ContentDatabase::load(const std::vector<fs::path>& mod_roots) {
    if (loaded_) {
        throw std::logic_error("ContentDatabase can only load once");
    }
    std::vector<ContentIssue> issues;
    std::vector<ModInfo> mods;
    for (const fs::path& root : mod_roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            issues.push_back(
                ContentIssue{Severity::Error, root.generic_string(), "", "mod root is not a directory", ""});
            continue;
        }
        if (auto info = read_mod_folder(root, issues)) {
            mods.push_back(std::move(*info));
        }
    }
    return load_ordered(mods, std::move(issues));
}

LoadResult ContentDatabase::load_ordered(const std::vector<ModInfo>& mods, std::vector<ContentIssue> issues) {
    loaded_ = true;

    const bool earlier_errors = std::any_of(
        issues.begin(), issues.end(), [](const ContentIssue& i) { return i.severity == Severity::Error; });
    if (!earlier_errors) {
        // Phase 1: read every file and layer the raw definitions, mod by mod in load order.
        RawDefs raw;
        core::StateHasher hasher;
        hasher.add_pod(static_cast<std::uint64_t>(mods.size()));
        for (const ModInfo& mod : mods) {
            mods_.push_back(mod.manifest);
            collect_mod_defs(mod, raw, issues);
            hash_mod(hasher, mod);
        }
        content_hash_ = hasher.digest();

        // Phase 2: parse what is left after layering. Ordered by (type, id), so the result and any messages
        // never depend on which mod or file a definition came from.
        ParseContext context(issues);
        for (auto& [key, def] : raw) {
            const auto kind = by_name_.find(key.first);
            context.set_file(def.source.file);
            context.set_mod_root(def.source.mod_root);
            context.set_layering(def.source.mod_id, def.source.touched_by);
            context.set_pointer(def.source.pointer);
            const ContentId id(key.second); // validated when collected
            context.set_def_id(id);
            kind->second->parse_and_add(def.json, id, context);
        }
    }
    finish(issues);

    LoadResult result;
    for (ContentIssue& issue : issues) {
        (issue.severity == Severity::Error ? result.errors : result.warnings).push_back(std::move(issue));
    }
    return result;
}

void ContentDatabase::finish(std::vector<ContentIssue>& issues) {
    for (const auto& kind : kinds_) {
        kind->finalize();
    }

    // References can only be resolved once every registry is sorted; validation needs them resolved.
    const RefLookup lookup{
        [this](std::type_index type, const ContentId& id) -> std::optional<std::uint32_t> {
            const auto it = by_type_.find(type);
            return it == by_type_.end() ? std::nullopt : it->second->find_index(id);
        },
        [this](std::type_index type) { return type_name_of(type); },
    };
    ParseContext pass_context(issues);
    pass_context.set_ref_lookup(&lookup);
    for (const auto& kind : kinds_) {
        kind->resolve_all(pass_context);
    }
    pass_context.set_ref_lookup(nullptr);
    for (const auto& kind : kinds_) {
        kind->validate_all(pass_context);
    }
}

void ContentDatabase::collect_mod_defs(const ModInfo& mod, RawDefs& raw, std::vector<ContentIssue>& issues) {
    for (const fs::path& file : json_files_under(mod.root / "defs", mod.root)) {
        collect_defs_file(mod, file, display_path(file, mod.root), raw, issues);
    }
    const fs::path time_file = mod.root / "defines" / "time.json";
    std::error_code ec;
    if (fs::is_regular_file(time_file, ec)) {
        load_time_defines(time_file, mod.root.generic_string(), display_path(time_file, mod.root), issues);
    }
}

void ContentDatabase::collect_defs_file(const ModInfo& mod, const fs::path& path,
                                        const std::string& display_name, RawDefs& raw,
                                        std::vector<ContentIssue>& issues) {
    ParseContext context(issues);
    context.set_mod_root(mod.root.generic_string());
    context.set_file(display_name);

    const std::optional<nlohmann::json> parsed = read_json_file(path, context);
    if (!parsed) {
        return;
    }
    const nlohmann::json& root = *parsed;
    if (!root.is_object() || !root.contains("defs")) {
        context.error("", "expected an object with a \"defs\" array");
        return;
    }
    const nlohmann::json& defs = root["defs"];
    if (!defs.is_array()) {
        context.error("/defs", "\"defs\" must be an array");
        return;
    }
    context.set_pointer("");
    context.warn_unknown_keys(root, {"defs"});

    const std::string& mod_id = mod.manifest.id;
    for (std::size_t index = 0; index < defs.size(); ++index) {
        const nlohmann::json& def = defs[index];
        const std::string pointer = "/defs/" + std::to_string(index);
        context.set_pointer(pointer);
        if (!def.is_object()) {
            context.error("expected an object");
            continue;
        }

        const auto type = context.read_string(def, "type");
        const auto id_text = context.read_string(def, "id");
        const std::size_t errors_before = context.error_count();
        const bool patch = context.read_bool(def, "patch", false).value_or(false);
        if (!type || !id_text || context.error_count() != errors_before) {
            continue; // a missing field or a "patch" that is not true/false: already reported
        }
        std::string reason;
        const auto id = ContentId::parse(*id_text, &reason);
        if (!id) {
            context.error(context.child_pointer("id"), "invalid id '" + *id_text + "': " + reason);
            continue;
        }
        if (!by_name_.contains(*type)) {
            context.error(context.child_pointer("type"), "unknown definition type '" + *type + "'");
            continue;
        }

        const std::pair<std::string, std::string> key{*type, id->str()};
        const auto existing = raw.find(key);
        DefSource here{display_name, mod.root.generic_string(), pointer, mod_id, {}};

        if (patch) {
            if (existing == raw.end()) {
                context.error(context.child_pointer("id"),
                              "cannot patch '" + id->str() + "': no earlier " + *type +
                                  " with that id (patches apply to a definition from "
                                  "this mod or one that loads before it)");
                continue;
            }
            // The patch is the definition minus the "patch" marker itself.
            nlohmann::json patch_body = def;
            patch_body.erase("patch");
            existing->second.json = merge_patch(std::move(existing->second.json), patch_body);
            here.touched_by = existing->second.source.touched_by;
            add_if_new(here.touched_by, mod_id);
            existing->second.source = std::move(here);
            continue;
        }

        if (existing != raw.end()) {
            const DefSource& first = existing->second.source;
            if (first.mod_id == mod_id) {
                // Replacing is for overriding another mod; a repeat inside one mod is almost certainly a
                // mistake.
                context.error("duplicate id '" + id->str() + "' (first defined in " + first.file + " at " +
                              first.pointer + ")");
                continue;
            }
            here.touched_by = first.touched_by; // a later mod replaces it wholesale; keep the history
            add_if_new(here.touched_by, mod_id);
            existing->second = RawDef{def, std::move(here)};
            continue;
        }
        here.touched_by = {mod_id};
        raw.emplace(key, RawDef{def, std::move(here)});
    }
}

void ContentDatabase::load_time_defines(const fs::path& path, const std::string& mod_root,
                                        const std::string& display_name, std::vector<ContentIssue>& issues) {
    ParseContext context(issues);
    context.set_mod_root(mod_root);
    context.set_file(display_name);

    const std::optional<nlohmann::json> parsed = read_json_file(path, context);
    if (!parsed) {
        return;
    }
    const nlohmann::json& json = *parsed;
    context.set_pointer("");
    if (!json.is_object()) {
        context.error("", "expected an object");
        return;
    }
    context.warn_unknown_keys(json,
                              {"base_tick_rate", "speed_multipliers", "movement_scale", "day_length_ticks"});

    // Fields that are absent keep their previous values: the built-in defaults, or what an earlier mod set.
    time::TimeDefines defines = time_defines_;
    if (const auto value = context.read_u32(json, "base_tick_rate", false)) {
        defines.base_tick_rate = *value;
    }
    if (const auto value = context.read_number(json, "movement_scale", false)) {
        defines.movement_scale = static_cast<float>(*value);
    }
    if (const auto value = context.read_u32(json, "day_length_ticks", false)) {
        defines.day_length_ticks = *value;
    }
    if (const auto it = json.find("speed_multipliers"); it != json.end()) {
        if (!it->is_array() || it->size() != defines.speed_multipliers.size()) {
            context.error("/speed_multipliers", "expected an array of " +
                                                    std::to_string(defines.speed_multipliers.size()) +
                                                    " whole numbers");
        } else {
            for (std::size_t i = 0; i < it->size(); ++i) {
                const nlohmann::json& element = (*it)[i];
                if (!element.is_number_unsigned()) {
                    context.error("/speed_multipliers/" + std::to_string(i),
                                  "expected a non-negative whole number");
                } else {
                    defines.speed_multipliers[i] = element.get<std::uint32_t>();
                }
            }
        }
    }

    // Values that parse but cannot work.
    if (json.contains("base_tick_rate") && defines.base_tick_rate == 0) {
        context.error("/base_tick_rate", "must be greater than 0");
    }
    if (json.contains("day_length_ticks") && defines.day_length_ticks == 0) {
        context.error("/day_length_ticks", "must be greater than 0");
    }
    if (json.contains("movement_scale") && !(defines.movement_scale > 0.0F)) {
        context.error("/movement_scale", "must be greater than 0");
    }
    for (std::size_t speed = 1; speed < defines.speed_multipliers.size(); ++speed) {
        if (defines.speed_multipliers[speed] == 0) {
            context.error("/speed_multipliers/" + std::to_string(speed),
                          "speeds 1 to 3 need a multiplier above 0");
        }
    }
    time_defines_ = defines;
}

} // namespace citysim::content
