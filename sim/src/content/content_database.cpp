#include "citysim/content/content_database.hpp"

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

} // namespace

ContentDatabase::ContentDatabase() {
    register_def_type<EraDef>("era", parse_era_def, resolve_era_def, validate_era_def);
}

std::string ContentDatabase::type_name_of(std::type_index type) const {
    const auto it = by_type_.find(type);
    return it == by_type_.end() ? std::string("definition") : std::string(it->second->type_name());
}

LoadResult ContentDatabase::load(const std::vector<fs::path>& mod_roots) {
    if (loaded_) {
        throw std::logic_error("ContentDatabase::load can only be called once");
    }
    loaded_ = true;

    std::vector<ContentIssue> issues;
    for (const fs::path& root : mod_roots) {
        load_mod_root(root, issues);
    }
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

    LoadResult result;
    for (ContentIssue& issue : issues) {
        (issue.severity == Severity::Error ? result.errors : result.warnings).push_back(std::move(issue));
    }
    return result;
}

void ContentDatabase::load_mod_root(const fs::path& root, std::vector<ContentIssue>& issues) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        issues.push_back(
            ContentIssue{Severity::Error, root.generic_string(), "", "mod root is not a directory", ""});
        return;
    }

    std::vector<fs::path> files;
    const fs::path defs_dir = root / "defs";
    if (fs::is_directory(defs_dir, ec)) {
        for (const auto& entry : fs::recursive_directory_iterator(defs_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
    }
    // Sorted by the displayed path, so load order does not depend on the file system's own ordering.
    std::sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) {
        return display_path(a, root) < display_path(b, root);
    });
    for (const fs::path& file : files) {
        load_defs_file(file, root.generic_string(), display_path(file, root), issues);
    }

    const fs::path time_file = root / "defines" / "time.json";
    if (fs::is_regular_file(time_file, ec)) {
        load_time_defines(time_file, root.generic_string(), display_path(time_file, root), issues);
    }
}

void ContentDatabase::load_defs_file(const fs::path& path, const std::string& mod_root,
                                     const std::string& display_name, std::vector<ContentIssue>& issues) {
    ParseContext context(issues);
    context.set_mod_root(mod_root);
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
        if (!type || !id_text) {
            continue;
        }
        std::string reason;
        const auto id = ContentId::parse(*id_text, &reason);
        if (!id) {
            context.error(context.child_pointer("id"), "invalid id '" + *id_text + "': " + reason);
            continue;
        }
        const auto kind = by_name_.find(*type);
        if (kind == by_name_.end()) {
            context.error(context.child_pointer("type"), "unknown definition type '" + *type + "'");
            continue;
        }
        context.set_def_id(*id);
        kind->second->parse_and_add(def, *id, context);
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

    // Fields that are absent keep their built-in defaults.
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
