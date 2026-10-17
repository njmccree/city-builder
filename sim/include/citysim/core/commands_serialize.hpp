#pragma once

// Save/load of commands and the input log (architecture §5.3, §10). The Command variant is written as the
// alternative's index plus its fields, so appending a command to the variant is compatible with old saves and
// reordering it is not (see the rule in commands.hpp).

#include "citysim/core/commands.hpp"
#include "citysim/core/input_log.hpp"
#include "citysim/debug/debug_serialize.hpp"
#include "citysim/save/archive.hpp"
#include "citysim/time/time_serialize.hpp"

namespace citysim::core {

template <class Ar> void serialize(Ar&, NoOp&, std::uint32_t) {}

template <class Ar> void serialize(Ar& ar, DebugSetComponentValue& command, std::uint32_t version) {
    save::io(ar, command.entity, version);
    save::io(ar, command.value, version);
}

template <class Ar> void serialize(Ar& ar, SaveGame& command, std::uint32_t version) {
    save::io(ar, command.path, version);
}

template <class Ar> void serialize(Ar& ar, LoadGame& command, std::uint32_t version) {
    save::io(ar, command.path, version);
}

template <class Ar> void serialize(Ar& ar, SequencedCommand& command, std::uint32_t version) {
    save::io(ar, command.sequence, version);
    save::io(ar, command.issue_tick, version);
    save::io(ar, command.cmd, version);
}

template <class Ar> void serialize(Ar& ar, InputLogEntry& entry, std::uint32_t version) {
    save::io(ar, entry.tick, version);
    save::io(ar, entry.sequence, version);
    save::io(ar, entry.cmd, version);
}

} // namespace citysim::core
