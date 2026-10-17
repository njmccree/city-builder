#pragma once

#include "citysim/core/commands.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace citysim::core {

// Thread-safe multi-producer queue between the main thread (UI, console, scripts) and the simulation
// thread (architecture §5.3, §7.1). A plain mutex is enough at this volume. Not tied to a clock: the
// runner drains it between ticks.
class CommandQueue {
  public:
    // Returns the assigned sequence number. Numbers start at 1 and increase by one per command, in
    // the order the calls acquired the lock.
    std::uint64_t submit(Command cmd, std::uint64_t issue_tick = 0);

    // Appends everything submitted so far to `out`, in sequence order, and empties the queue.
    void drain(std::vector<SequencedCommand>& out);

    std::size_t size() const;

  private:
    mutable std::mutex mutex_;
    std::vector<SequencedCommand> pending_;
    std::uint64_t next_sequence_ = 1;
};

} // namespace citysim::core
