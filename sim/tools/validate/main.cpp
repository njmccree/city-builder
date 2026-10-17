// citysim_validate: checks game content without starting the game (architecture §8, §13).
//
//     citysim_validate <content_root> [--strict]
//
// <content_root> is either one mod (a folder with mod.json, defs/ or defines/) or a folder of mods. It prints
// every problem as "path:json_pointer: severity: message" and exits 0 if the content is fine, 1 if there are
// errors (or warnings with --strict), 2 if it could not run (bad arguments, missing folder, no mods found).

#include "citysim/content/content_database.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace citysim;

namespace {

bool looks_like_mod_root(const fs::path& dir) {
    std::error_code ignored;
    return fs::is_regular_file(dir / "mod.json", ignored) || fs::is_directory(dir / "defs", ignored) ||
           fs::is_directory(dir / "defines", ignored);
}

// The folder itself if it is a mod, otherwise its subfolders that are, by name. Proper manifest-based
// discovery with dependencies and load order arrives in T0.4.2.1.
std::vector<fs::path> find_mod_roots(const fs::path& content_root) {
    if (looks_like_mod_root(content_root)) {
        return {content_root};
    }
    std::vector<fs::path> roots;
    for (const auto& entry : fs::directory_iterator(content_root)) {
        if (entry.is_directory() && looks_like_mod_root(entry.path())) {
            roots.push_back(entry.path());
        }
    }
    std::sort(roots.begin(), roots.end(),
              [](const fs::path& a, const fs::path& b) { return a.generic_string() < b.generic_string(); });
    return roots;
}

void print_usage(std::ostream& out) {
    out << "usage: citysim_validate <content_root> [--strict]\n"
           "  --strict   treat warnings as errors\n";
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    bool strict = false;
    std::string root_arg;
    for (const std::string_view arg : args) {
        if (arg == "--strict") {
            strict = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return 0;
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "error: unknown option " << arg << '\n';
            print_usage(std::cerr);
            return 2;
        } else if (root_arg.empty()) {
            root_arg = std::string(arg);
        } else {
            std::cerr << "error: more than one content root given\n";
            print_usage(std::cerr);
            return 2;
        }
    }
    if (root_arg.empty()) {
        print_usage(std::cerr);
        return 2;
    }

    const fs::path content_root(root_arg);
    std::error_code ec;
    if (!fs::is_directory(content_root, ec)) {
        std::cerr << "error: " << root_arg << " is not a directory\n";
        return 2;
    }
    const std::vector<fs::path> mod_roots = find_mod_roots(content_root);
    if (mod_roots.empty()) {
        std::cerr << "error: no mods found under " << root_arg
                  << " (expected a folder with mod.json, defs/ or defines/)\n";
        return 2;
    }

    content::ContentDatabase database;
    const content::LoadResult result = database.load(mod_roots);

    for (const content::ContentIssue& issue : result.errors) {
        std::cout << issue.mod_root << '/' << issue.file << ':' << issue.pointer
                  << ": error: " << issue.message << '\n';
    }
    for (const content::ContentIssue& issue : result.warnings) {
        std::cout << issue.mod_root << '/' << issue.file << ':' << issue.pointer
                  << ": warning: " << issue.message << '\n';
    }
    std::cout << mod_roots.size() << " mod(s) checked: " << result.errors.size() << " error(s), "
              << result.warnings.size() << " warning(s)\n";

    if (!result.errors.empty() || (strict && !result.warnings.empty())) {
        return 1;
    }
    return 0;
}
