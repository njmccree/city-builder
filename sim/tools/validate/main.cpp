// citysim_validate: checks game content without starting the game (architecture §8, §13).
//
//     citysim_validate <content_root> [--strict] [--verbose]
//
// <content_root> is either one mod (a folder with mod.json, defs/ or defines/) or a folder of mods. It prints
// every problem as "path:json_pointer: severity: message" and exits 0 if the content is fine, 1 if there are
// errors (or warnings with --strict), 2 if it could not run (bad arguments, missing folder, no mods found).

#include "citysim/content/content_database.hpp"

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

// The folder itself if it is a mod, otherwise its mods and those in its "mods" subfolder (where optional
// mods live, game/content/mods). Discovery handles the rest.
std::vector<fs::path> search_roots(const fs::path& content_root) {
    if (looks_like_mod_root(content_root)) {
        return {content_root};
    }
    std::vector<fs::path> roots{content_root};
    std::error_code ignored;
    if (fs::is_directory(content_root / "mods", ignored)) {
        roots.push_back(content_root / "mods");
    }
    return roots;
}

void print_issue(const content::ContentIssue& issue, const char* severity) {
    const std::string path = issue.mod_root.empty() ? issue.file : issue.mod_root + "/" + issue.file;
    std::cout << path << ':' << issue.pointer << ": " << severity << ": " << issue.message << '\n';
}

void print_usage(std::ostream& out) {
    out << "usage: citysim_validate <content_root> [--strict] [--verbose]\n"
           "  --strict    treat warnings as errors\n"
           "  --verbose   also list notes, such as unused localization keys\n";
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    bool strict = false;
    bool verbose = false;
    std::string root_arg;
    for (const std::string_view arg : args) {
        if (arg == "--strict") {
            strict = true;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
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
    content::ContentDatabase database;
    content::ModLoadRequest request;
    request.roots = search_roots(content_root);
    request.order.enable_all = true; // check every mod, not only those on by default
    const content::LoadResult result = database.load_mods(request);
    if (database.mods().empty() && result.errors.empty()) {
        std::cerr << "error: no mods found under " << root_arg
                  << " (expected a folder with mod.json, defs/ or defines/)\n";
        return 2;
    }

    for (const content::ContentIssue& issue : result.errors) {
        print_issue(issue, "error");
    }
    for (const content::ContentIssue& issue : result.warnings) {
        print_issue(issue, "warning");
    }
    if (verbose) {
        for (const content::ContentIssue& issue : result.infos) {
            print_issue(issue, "info");
        }
    }
    std::cout << database.mods().size() << " mod(s) checked: " << result.errors.size() << " error(s), "
              << result.warnings.size() << " warning(s)\n";

    if (!result.errors.empty() || (strict && !result.warnings.empty())) {
        return 1;
    }
    return 0;
}
