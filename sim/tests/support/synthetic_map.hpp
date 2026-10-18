#pragma once

#include <filesystem>

#ifndef CITYSIM_SYNTHETIC_MAP_DIR
#error                                                                                                       \
    "CITYSIM_SYNTHETIC_MAP_DIR must name the folder of the synthetic map package (sim/tests/CMakeLists.txt)"
#endif

namespace citysim::test {

// The default synthetic map package (`citysim_mapgen --synthetic`, seed 1, 96 km): 375 detail tiles over a
// valley, a river that splits into a delta, and a coast. CTest generates it once before the tests that need
// it run (the `synthetic_map` fixture in sim/tools/mapgen/CMakeLists.txt); those tests carry the Catch2 tag
// [synthetic_map]. Running the test binary by hand works if the package was generated before (no getenv here:
// MSVC warns about it and the build treats warnings as errors).
inline std::filesystem::path synthetic_map_dir() {
    return CITYSIM_SYNTHETIC_MAP_DIR;
}

} // namespace citysim::test
