#include "citysim/core/input_log.hpp"

#include <ostream>

namespace citysim::core {

void InputLog::record(std::uint64_t tick, std::uint64_t sequence, const Command& cmd) {
    entries_.push_back(InputLogEntry{tick, sequence, cmd});
}

void InputLog::dump(std::ostream& out) const {
    for (const InputLogEntry& entry : entries_) {
        out << entry.tick << ' ' << entry.sequence << ' ' << describe(entry.cmd) << '\n';
    }
}

} // namespace citysim::core
