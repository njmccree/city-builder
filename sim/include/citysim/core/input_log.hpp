#pragma once

#include "citysim/core/commands.hpp"

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace citysim::core {

struct InputLogEntry {
    std::uint64_t tick = 0; // the tick whose ApplyCommands phase consumed the command
    std::uint64_t sequence = 0;
    Command cmd;
};

// Every command the simulation consumed, in the order consumed: seed + this log replays a game
// (architecture §5.3). Commands with no handler are recorded too, since they were still inputs.
// In-memory only for now; the binary form arrives with save/load (S0.5).
class InputLog {
  public:
    void record(std::uint64_t tick, std::uint64_t sequence, const Command& cmd);

    const std::vector<InputLogEntry>& entries() const { return entries_; }

    // One line per entry: "<tick> <sequence> <description>". For debugging, not a stable format.
    void dump(std::ostream& out) const;

  private:
    std::vector<InputLogEntry> entries_;
};

} // namespace citysim::core
