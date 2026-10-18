#pragma once

#include "mapgen/synthetic_map.hpp"
#include "support/temp_dir.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace citysim::test {

// A content root holding one mod, "test", with the synthetic map in it as a game would have a map: the
// definition (test:map.synthetic) in defs/, its text in loc/en/, the package in maps/synthetic/. Load it with
// ContentDatabase::load({mod_dir()}) or, next to the base game, through load_mods with the root as one of the
// roots and "test" enabled.
class MapContent {
  public:
    // A small map generated here (16 km: 4 x 4 detail tiles, tiles (2..5, 3..6), on an 8 x 8 tile map).
    MapContent(const TempDir& dir, std::uint64_t seed = 1) : root_(dir / "map_content") {
        mapgen::write_synthetic_package(mapgen::SyntheticSpec{seed, 16.0}, package_dir());
        write_mod_files();
    }

    // A copy of an existing package, such as the full-size synthetic one (synthetic_map_dir()).
    MapContent(const TempDir& dir, const std::filesystem::path& package) : root_(dir / "map_content") {
        std::filesystem::create_directories(package_dir().parent_path());
        std::filesystem::copy(package, package_dir(), std::filesystem::copy_options::recursive);
        write_mod_files();
    }

    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path mod_dir() const { return root_ / "test"; }
    std::filesystem::path package_dir() const {
        return mod_dir() / "maps" / mapgen::k_synthetic_package_folder;
    }
    static constexpr const char* map_id() { return mapgen::k_synthetic_map_id; }

    // Replaces the package with one made from another seed (another package hash), keeping the definition.
    void regenerate_package(std::uint64_t seed) const {
        std::filesystem::remove_all(package_dir());
        mapgen::write_synthetic_package(mapgen::SyntheticSpec{seed, 16.0}, package_dir());
    }

  private:
    static std::string read_text(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    static void write_text(const std::filesystem::path& path, const std::string& text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }

    void write_mod_files() const {
        write_text(mod_dir() / "mod.json", R"({ "id": "test", "name_key": "mod.test.name", "version": "1.0.0",
  "depends_on": ["base"], "enabled_by_default": false })");
        write_text(mod_dir() / "defs" / "maps.json", read_text(package_dir() / "map_def.json"));
        write_text(mod_dir() / "loc" / "en" / "maps.json", R"({ "mod.test.name": "Test",
  "map.synthetic.name": "Synthetic", "map.synthetic.desc": "A synthetic test map." })");
    }

    std::filesystem::path root_;
};

} // namespace citysim::test
