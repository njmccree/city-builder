#include "citysim/content/mod_discovery.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace citysim::content {

namespace fs = std::filesystem;

namespace {

bool is_mod_folder(const fs::path& dir) {
    std::error_code ignored;
    return fs::is_regular_file(dir / "mod.json", ignored) || fs::is_directory(dir / "defs", ignored) ||
           fs::is_directory(dir / "defines", ignored);
}

std::string folder_name_as_mod_id(const fs::path& folder) {
    std::string name = folder.filename().generic_string();
    if (name.empty()) { // a path ending in a slash
        name = folder.parent_path().filename().generic_string();
    }
    return is_valid_mod_id(name) ? name : "unnamed";
}

} // namespace

std::optional<ModInfo> read_mod_folder(const fs::path& folder, std::vector<ContentIssue>& issues) {
    ModInfo info;
    info.root = folder;
    const fs::path manifest_path = folder / "mod.json";
    std::error_code ignored;
    if (!fs::is_regular_file(manifest_path, ignored)) {
        info.has_manifest_file = false;
        info.manifest.id = folder_name_as_mod_id(folder);
        info.manifest.version = "0.0.0";
        return info;
    }

    ParseContext context(issues);
    context.set_mod_root(folder.generic_string());
    context.set_file("mod.json");

    std::ifstream in(manifest_path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception& error) {
        context.error("", std::string("invalid JSON: ") + error.what());
        return std::nullopt;
    }
    auto manifest = parse_mod_manifest(json, context);
    if (!manifest) {
        return std::nullopt;
    }
    info.manifest = std::move(*manifest);
    return info;
}

DiscoveryResult discover_mods(const std::vector<fs::path>& roots) {
    DiscoveryResult result;
    std::set<std::string> seen_ids;

    const auto add = [&](const fs::path& folder) {
        auto info = read_mod_folder(folder, result.issues);
        if (!info) {
            return;
        }
        if (!seen_ids.insert(info->manifest.id).second) {
            result.issues.push_back(ContentIssue{
                Severity::Error, info->has_manifest_file ? "mod.json" : "",
                info->has_manifest_file ? "/id" : "",
                "a mod with the id '" + info->manifest.id + "' was already found", folder.generic_string()});
            return;
        }
        result.mods.push_back(std::move(*info));
    };

    for (const fs::path& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            result.issues.push_back(
                ContentIssue{Severity::Error, root.generic_string(), "", "mod root is not a directory", ""});
            continue;
        }
        if (is_mod_folder(root)) {
            add(root);
            continue;
        }
        std::vector<fs::path> folders;
        for (const auto& entry : fs::directory_iterator(root)) {
            if (entry.is_directory() && is_mod_folder(entry.path())) {
                folders.push_back(entry.path());
            }
        }
        std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) {
            return a.generic_string() < b.generic_string();
        });
        for (const fs::path& folder : folders) {
            add(folder);
        }
    }
    return result;
}

} // namespace citysim::content
