#pragma once

#include "citysim/save/binary_reader.hpp"
#include "citysim/save/binary_writer.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace citysim::save {

// Upgrades old saves one version at a time (architecture §10). Not tied to a clock: configuration, filled in
// before anything is loaded.
//
// Two kinds of migration, both "read the layout of version V from `in`, write the layout of version V+1 to
// `out`":
//   * a type migration is keyed by the stable name of a component or singleton ("farm.Farm") and handles ONE
//     instance of it, so it never has to know how many there are or where they end;
//   * a section migration is keyed by the section tag ("CMDQ") and handles the whole section.
// On load, an older save is run through the chain V, V+1, ... up to this build's version, and then read with
// the current serialize() function, which therefore only ever has to understand the current layout. A save
// whose chain has a gap is refused with a message naming the type and the versions.
//
// Entity references reach a type migration as they are in the file (index + 1, 0 for none, as a varint): copy
// them with varint(); the reader's entity() would need the remap, which the migration's writer does not have.
using MigrationFn = std::function<void(BinaryReader& in, BinaryWriter& out)>;

class MigrationRegistry {
  public:
    // `from_version` -> `from_version + 1`. Throws std::logic_error if one is already registered.
    void add_type(const std::string& stable_name, std::uint32_t from_version, MigrationFn fn);
    void add_section(const std::string& tag, std::uint32_t from_version, MigrationFn fn);

    const MigrationFn* find_type(const std::string& stable_name, std::uint32_t from_version) const;
    const MigrationFn* find_section(const std::string& tag, std::uint32_t from_version) const;

  private:
    std::map<std::pair<std::string, std::uint32_t>, MigrationFn> types_;
    std::map<std::pair<std::string, std::uint32_t>, MigrationFn> sections_;
};

// Reads one instance written at `saved_version` from `in` (which must be positioned at it) and hands a reader
// of the current layout to `read_current`, running the registered type migrations in between. With
// saved_version == current_version it passes `in` straight through. `registry` may be null (no migrations).
// Throws SaveFormatError, naming `name` and the versions, if a step is missing, and if a migration leaves
// bytes unread. A reader handed to `read_current` is positioned at the instance and must be used up to its
// end by it.
void read_migrated_instance(const MigrationRegistry* registry, const std::string& name,
                            std::uint32_t saved_version, std::uint32_t current_version, BinaryReader& in,
                            const std::function<void(BinaryReader&)>& read_current);

// Runs the section migrations for `tag` on a whole section. Returns `data` unchanged if the versions match.
std::vector<std::byte> migrate_section(const MigrationRegistry* registry, const std::string& tag,
                                       std::uint32_t saved_version, std::uint32_t current_version,
                                       std::span<const std::byte> data);

} // namespace citysim::save
