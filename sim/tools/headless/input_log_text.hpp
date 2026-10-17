#pragma once

#include "citysim/core/input_log.hpp"

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace citysim::headless {

// Text form of a recorded game, for citysim_headless --record-log / --replay. S0.5 replaces it with the
// binary container; until then this is the documented format:
//
//     citysim-input-log 1            first line: magic and format version
//     seed 42                        the world seed
//     ticks 500                      how many ticks the recorded run executed
//     25 1 DebugSetComponentValue 12 -40
//     40 2 NoOp                      one line per command: <tick> <sequence> <name> <arguments...>
//
// `tick` is the tick whose ApplyCommands phase consumed the command. Blank lines and lines starting with
// '#' are ignored. Arguments are in the order the command struct declares them.
struct LogFile {
    std::uint64_t seed = 0;
    std::uint64_t ticks = 0;
    std::vector<citysim::core::InputLogEntry> entries;
};

void write_log_file(std::ostream& out, const LogFile& log);

// Throws std::runtime_error naming the offending line on any malformed input.
LogFile read_log_file(std::istream& in);

} // namespace citysim::headless
