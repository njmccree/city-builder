#pragma once

#include <compare>
#include <cstdint>

namespace citysim::content {

// A 32-bit index into a DefRegistry<T> (architecture §8). Stable for a given set of loaded mods, so it is
// fine inside a running simulation, but saves store ids, never handles.
template <class T> struct DefHandle {
    static constexpr std::uint32_t k_invalid = 0xFFFFFFFFU;

    std::uint32_t index = k_invalid;

    constexpr bool valid() const { return index != k_invalid; }
    constexpr auto operator<=>(const DefHandle&) const = default;
};

} // namespace citysim::content
