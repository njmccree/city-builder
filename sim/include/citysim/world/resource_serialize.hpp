#pragma once

// Save/load of the resource fields' state (architecture §10, §15.8; pattern in citysim/save/archive.hpp).
// Only the deltas travel, never a base value. Field order is the save format: append with a version bump,
// never reorder.

#include "citysim/save/archive.hpp"
#include "citysim/world/resource_fields.hpp"
#include "citysim/world/terrain_serialize.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace citysim::world {

// A changed layer travels as its field name, its tile and one zstd frame of the offsets. The reader refuses
// a frame that is not exactly one layer.
template <class Ar> void serialize(Ar& ar, SavedFieldDelta& delta, std::uint32_t version) {
    save::io(ar, delta.field, version);
    save::io(ar, delta.coord, version);
    std::vector<std::uint8_t> packed;
    if constexpr (!Ar::is_loading) {
        packed = compress_resource_offsets(delta.offsets);
    }
    save::io(ar, packed, version);
    if constexpr (Ar::is_loading) {
        try {
            delta.offsets = decompress_resource_offsets(packed);
        } catch (const std::invalid_argument& error) {
            ar.fail(error.what());
        }
    }
}

template <class Ar> void serialize(Ar& ar, ResourceFields& fields, std::uint32_t version) {
    std::vector<SavedFieldDelta> saved;
    if constexpr (!Ar::is_loading) {
        saved = fields.saved_state();
    }
    save::io(ar, saved, version);
    if constexpr (Ar::is_loading) {
        try {
            fields.restore(std::move(saved));
        } catch (const std::invalid_argument& error) {
            ar.fail(error.what());
        }
    }
}

} // namespace citysim::world
