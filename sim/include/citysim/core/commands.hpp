#pragma once

#include "citysim/time/time_commands.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace citysim::core {

// Every external change to game state is a command (architecture §5.3). A command is a plain struct:
// data only, no pointers or references, so it can be logged, replayed and serialized (S0.5).
//
// HOW TO ADD A COMMAND
//   1. Define a plain struct here (game commands from later tasks live in their own module's header and
//      are included and listed below).
//   2. Append it to the END of `Command`. The variant index is what saved input logs record, so never
//      reorder or remove an entry.
//   3. Add a `describe` overload in commands.cpp. The visit there is exhaustive, so the build fails until
//      you do.
//   4. Register a handler: `simulation.dispatcher().register_handler<MyCommand>(...)`.

struct NoOp {};

// Test-only: handlers in tests give it meaning.
struct DebugSetComponentValue {
    std::uint32_t entity = 0;
    std::int32_t value = 0;
};

using Command = std::variant<NoOp, DebugSetComponentValue, time::SetSpeed, time::TogglePause, time::SetEra>;

struct SequencedCommand {
    std::uint64_t sequence = 0;   // assigned by CommandQueue::submit, strictly increasing
    std::uint64_t issue_tick = 0; // the tick the issuer observed when submitting; informational
    Command cmd;
};

// One-line text form for logs and debug dumps.
std::string describe(const Command& cmd);

} // namespace citysim::core
