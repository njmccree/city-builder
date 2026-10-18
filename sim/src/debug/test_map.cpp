#include "citysim/debug/test_map.hpp"

#include "citysim/core/rng.hpp"

#include <algorithm>
#include <cmath>

namespace citysim::debug {

TestMap make_test_map(std::uint64_t seed, std::uint32_t width, std::uint32_t height, float cell_size) {
    TestMap map(width, height, cell_size);
    if (width == 0 || height == 0) {
        return map;
    }
    const std::uint64_t stream = rng::stream("debug.map");
    const std::uint64_t obstacles =
        std::max<std::uint64_t>(1, static_cast<std::uint64_t>(width) * height / 600);
    for (std::uint64_t i = 0; i < obstacles; ++i) {
        // Each number of the obstacle is its own counter-based draw, so nothing depends on draw order.
        const auto draw = [&](std::uint64_t part, std::int64_t lo, std::int64_t hi) {
            return rng::uniform_int(rng::hash64(seed, stream, 0, i, part), lo, hi);
        };
        const std::int64_t w = draw(0, 2, 10);
        const std::int64_t h = draw(1, 2, 10);
        const std::int64_t x0 = draw(2, 0, static_cast<std::int64_t>(width) - 1);
        const std::int64_t y0 = draw(3, 0, static_cast<std::int64_t>(height) - 1);
        for (std::int64_t y = y0; y < std::min<std::int64_t>(y0 + h, height); ++y) {
            for (std::int64_t x = x0; x < std::min<std::int64_t>(x0 + w, width); ++x) {
                map.set_walkable(map.index_of(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)),
                                 false);
            }
        }
    }
    return map;
}

} // namespace citysim::debug
