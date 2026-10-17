#pragma once

#include "citysim/core/component_registry.hpp"
#include "citysim/save/archive.hpp"

#include <cstdint>

namespace citysim::save {

// Points `hooks` at the type's serialize() function (see archive.hpp) for both saving and loading, and sets
// the layout version written by this build. Call before registering the type.
template <class T> void bind_serializer(core::Hooks<T>& hooks, std::uint32_t version = 1) {
    hooks.version = version;
    hooks.write = [](BinaryWriter& ar, T& value, std::uint32_t saved_version) {
        io(ar, value, saved_version);
    };
    hooks.read = [](BinaryReader& ar, T& value, std::uint32_t saved_version) {
        io(ar, value, saved_version);
    };
}

} // namespace citysim::save
