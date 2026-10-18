#include "citysim/content/mod_manifest.hpp"

#include <algorithm>
#include <cctype>

namespace citysim::content {

bool is_valid_mod_id(std::string_view id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
    });
}

namespace {

bool all_digits(std::string_view text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool valid_identifier_list(std::string_view text) { // the "beta.1" part of "-beta.1" or "+build.5"
    if (text.empty()) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' ||
               c == '.';
    });
}

// Reads an array of mod ids into `out`; reports a wrong shape or an invalid id at its own pointer.
void read_id_list(const nlohmann::json& json, ParseContext& context, std::string_view key,
                  std::vector<std::string>& out) {
    const auto it = json.find(std::string(key));
    if (it == json.end()) {
        return;
    }
    if (!it->is_array()) {
        context.error(context.child_pointer(key), "expected an array of mod ids");
        return;
    }
    for (std::size_t i = 0; i < it->size(); ++i) {
        const nlohmann::json& element = (*it)[i];
        const std::string pointer = context.child_pointer(key) + "/" + std::to_string(i);
        if (!element.is_string()) {
            context.error(pointer, "expected a mod id string");
        } else if (!is_valid_mod_id(element.get<std::string>())) {
            context.error(pointer, "invalid mod id '" + element.get<std::string>() +
                                       "' (allowed: a-z, 0-9, '_' and '.')");
        } else {
            out.push_back(element.get<std::string>());
        }
    }
}

} // namespace

bool is_valid_semver(std::string_view version) {
    std::string_view rest = version;
    std::string_view build;
    if (const auto plus = rest.find('+'); plus != std::string_view::npos) {
        build = rest.substr(plus + 1);
        rest = rest.substr(0, plus);
        if (!valid_identifier_list(build)) {
            return false;
        }
    }
    std::string_view prerelease;
    if (const auto dash = rest.find('-'); dash != std::string_view::npos) {
        prerelease = rest.substr(dash + 1);
        rest = rest.substr(0, dash);
        if (!valid_identifier_list(prerelease)) {
            return false;
        }
    }
    // MAJOR.MINOR.PATCH
    const auto first = rest.find('.');
    if (first == std::string_view::npos) {
        return false;
    }
    const auto second = rest.find('.', first + 1);
    if (second == std::string_view::npos) {
        return false;
    }
    return all_digits(rest.substr(0, first)) && all_digits(rest.substr(first + 1, second - first - 1)) &&
           all_digits(rest.substr(second + 1));
}

std::optional<ModManifest> parse_mod_manifest(const nlohmann::json& json, ParseContext& context) {
    const std::size_t errors_before = context.error_count();
    ModManifest manifest;
    if (!json.is_object()) {
        context.error("", "expected an object");
        return std::nullopt;
    }
    context.warn_unknown_keys(
        json, {"id", "name_key", "version", "depends_on", "load_after", "enabled_by_default"});

    if (const auto id = context.read_string(json, "id")) {
        if (!is_valid_mod_id(*id)) {
            context.error(context.child_pointer("id"),
                          "invalid mod id '" + *id + "' (allowed: a-z, 0-9, '_' and '.')");
        } else {
            manifest.id = *id;
        }
    }
    manifest.name_key = context.read_string(json, "name_key").value_or("");
    if (const auto version = context.read_string(json, "version")) {
        if (!is_valid_semver(*version)) {
            context.error(context.child_pointer("version"),
                          "'" + *version + "' is not a semantic version (expected MAJOR.MINOR.PATCH)");
        } else {
            manifest.version = *version;
        }
    }
    read_id_list(json, context, "depends_on", manifest.depends_on);
    read_id_list(json, context, "load_after", manifest.load_after);
    manifest.enabled_by_default = context.read_bool(json, "enabled_by_default", false).value_or(true);

    if (context.error_count() != errors_before) {
        return std::nullopt;
    }
    return manifest;
}

} // namespace citysim::content
