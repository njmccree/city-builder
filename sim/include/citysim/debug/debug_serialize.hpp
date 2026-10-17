#pragma once

// Save/load of the walking skeleton's state (architecture §10; pattern in citysim/save/archive.hpp). Field
// order is the save format: append with a version bump, never reorder.

#include "citysim/debug/agent_components.hpp"
#include "citysim/debug/debug_commands.hpp"
#include "citysim/debug/test_map.hpp"
#include "citysim/save/archive.hpp"

#include <cstdint>

namespace citysim::debug {

template <class Ar> void serialize(Ar& ar, Position& position, std::uint32_t version) {
    save::io(ar, position.x, version);
    save::io(ar, position.z, version);
}

template <class Ar> void serialize(Ar& ar, Heading& heading, std::uint32_t version) {
    save::io(ar, heading.yaw, version);
}

template <class Ar> void serialize(Ar& ar, PathFollower& follower, std::uint32_t version) {
    save::io(ar, follower.cells, version);
    save::io(ar, follower.next, version);
    save::io(ar, follower.speed_mps, version);
}

template <class Ar> void serialize(Ar& ar, DebugWanderer& wanderer, std::uint32_t version) {
    save::io(ar, wanderer.trips, version);
}

template <class Ar> void serialize(Ar& ar, DebugAgentCounter& counter, std::uint32_t version) {
    save::io(ar, counter.spawned, version);
}

template <class Ar> void serialize(Ar& ar, TestMap& map, std::uint32_t version) {
    std::uint32_t width = map.width();
    std::uint32_t height = map.height();
    float cell_size = map.cell_size();
    save::io(ar, width, version);
    save::io(ar, height, version);
    save::io(ar, cell_size, version);
    if constexpr (Ar::is_loading) {
        if (static_cast<std::uint64_t>(width) * height > (std::uint64_t{1} << 28)) {
            ar.fail("the test map is implausibly large");
        }
        map.resize_for_load(width, height, cell_size);
    }
    save::io(ar, map.words(), version);
    if constexpr (Ar::is_loading) {
        if (map.words().size() != (static_cast<std::size_t>(width) * height + 63U) / 64U) {
            ar.fail("the test map's cells do not match its size");
        }
    }
}

template <class Ar> void serialize(Ar& ar, SpawnDebugAgents& command, std::uint32_t version) {
    save::io(ar, command.count, version);
}

template <class Ar> void serialize(Ar&, ClearDebugAgents&, std::uint32_t) {}

} // namespace citysim::debug
