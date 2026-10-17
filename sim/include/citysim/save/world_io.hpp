#pragma once

#include "citysim/save/binary_reader.hpp"
#include "citysim/save/binary_writer.hpp"
#include "citysim/save/migration_registry.hpp"

#include <string>
#include <vector>

namespace citysim::core {
class World;
}

namespace citysim::save {

// What load_world did that the caller may want to tell the user about.
struct WorldLoadReport {
    // Data in the save for types this build does not register (a mod that was removed, for example); skipped.
    std::vector<std::string> skipped_types;
};

// Writes the whole World state (architecture §10): the entity count, then each registered component type
// and each present singleton, sorted by stable name. Each type is a block of: name, version, byte length,
// then (entity index, data) pairs sorted by entity or the singleton's data. The length lets a reader skip a
// type it does not know. Entities are written as dense indices (see EntityRemap), so the bytes depend only
// on the state, not on how the registry allocated ids.
//
// The writer's content database (if any) resolves definition references; the writer's remap is replaced for
// the duration. Throws std::logic_error if a registered non-empty type has no serialize hooks (an
// unserializable type would silently vanish from saves, which §5.1 forbids).
void save_world(const core::World& world, BinaryWriter& out);

// Reads what save_world wrote into `world`, which must have the same types registered and no entities. New
// entities are created for the saved ones; singletons are overwritten in place. Throws SaveFormatError on
// truncated, corrupt or newer-than-supported data, or data that refers to something that does not exist.
// On an error the world is left partly loaded and must be discarded.
//
// A type saved at an older version than this build's is upgraded through `migrations` (see
// MigrationRegistry) and read with the current serialize(), which is always called with the current
// version; without a migration for every step the load fails with a message naming the type.
WorldLoadReport load_world(core::World& world, BinaryReader& in,
                           const MigrationRegistry* migrations = nullptr);

} // namespace citysim::save
