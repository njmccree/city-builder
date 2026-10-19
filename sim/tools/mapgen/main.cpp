#include "citysim/world/map_format_error.hpp"
#include "mapgen/synthetic_map.hpp"

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string usage() {
    return "usage: citysim_mapgen --synthetic (--out DIR | --mod ROOT) [--seed N] [--size-km N]\n"
           "  --synthetic    make a synthetic map package (a valley, river, delta and coast; no real data)\n"
           "  --out DIR      the folder to write the package into (created if needed)\n"
           "  --mod ROOT     write a whole mod 'test' with the map (test:map.synthetic) into ROOT/test,\n"
           "                 for a content root a game can start on (e.g. game/content_dev)\n"
           "  --seed N       the map seed (default 1); one seed always gives the same files\n"
           "  --size-km N    the map is N x N km, whole number from 16 to 1000 (default 96)\n";
}

bool parse_number(std::string_view text, std::uint64_t& out) {
    const char* end = text.data() + text.size();
    const auto [pointer, error] = std::from_chars(text.data(), end, out);
    return error == std::errc() && pointer == end;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    bool synthetic = false;
    std::string out;
    std::string mod_root;
    citysim::mapgen::SyntheticSpec spec;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto value = [&]() -> std::string_view {
            return i + 1 < args.size() ? args[++i] : std::string_view();
        };
        std::uint64_t number = 0;
        if (arg == "--help" || arg == "-h") {
            std::cout << usage();
            return 0;
        }
        if (arg == "--synthetic") {
            synthetic = true;
        } else if (arg == "--out") {
            out = std::string(value());
        } else if (arg == "--mod") {
            mod_root = std::string(value());
        } else if (arg == "--seed") {
            if (!parse_number(value(), number)) {
                std::cerr << "--seed needs a whole number\n" << usage();
                return 2;
            }
            spec.seed = number;
        } else if (arg == "--size-km") {
            if (!parse_number(value(), number)) {
                std::cerr << "--size-km needs a whole number\n" << usage();
                return 2;
            }
            spec.size_km = static_cast<double>(number);
        } else {
            std::cerr << "unknown option '" << arg << "'\n" << usage();
            return 2;
        }
    }
    if (!synthetic || out.empty() == mod_root.empty()) {
        std::cerr << "--synthetic and one of --out or --mod are required\n" << usage();
        return 2;
    }
    if (const std::string problem = citysim::mapgen::check_spec(spec); !problem.empty()) {
        std::cerr << problem << "\n" << usage();
        return 2;
    }
    try {
        const citysim::world::PackageManifest manifest =
            mod_root.empty() ? citysim::mapgen::write_synthetic_package(spec, out)
                             : citysim::mapgen::write_synthetic_mod(spec, mod_root);
        const std::string written =
            mod_root.empty() ? out : mod_root + "/" + citysim::mapgen::k_synthetic_mod_id;
        std::cout << "wrote " << written << ": " << manifest.detail.tiles.size() << " detail tiles, "
                  << manifest.files.size() << " files, package hash "
                  << citysim::world::hash_to_hex(manifest.package_hash) << "\n";
    } catch (const std::exception& error) {
        std::cerr << "citysim_mapgen: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
