#include "headless/input_log_text.hpp"

#include <charconv>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace citysim::headless {

using citysim::core::Command;
using citysim::core::DebugSetComponentValue;
using citysim::core::InputLogEntry;
using citysim::core::NoOp;

namespace {

// One overload per command type: adding a command without one fails to compile.
struct WriteCommand {
    std::ostream& out;
    void operator()(const NoOp&) const { out << "NoOp"; }
    void operator()(const DebugSetComponentValue& cmd) const {
        out << "DebugSetComponentValue " << cmd.entity << ' ' << cmd.value;
    }
};

[[noreturn]] void fail(std::size_t line, const std::string& message) {
    throw std::runtime_error("input log line " + std::to_string(line) + ": " + message);
}

template <class T> T parse_number(const std::string& text, std::size_t line, const char* what) {
    T value{};
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (text.empty() || result.ec != std::errc{} || result.ptr != end) {
        fail(line, std::string("invalid ") + what + " '" + text + "'");
    }
    return value;
}

std::vector<std::string> split(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> tokens;
    for (std::string token; stream >> token;) {
        tokens.push_back(token);
    }
    return tokens;
}

void expect_arg_count(const std::vector<std::string>& tokens, std::size_t expected, std::size_t line) {
    if (tokens.size() != 3 + expected) {
        fail(line, "'" + tokens[2] + "' takes " + std::to_string(expected) + " argument(s)");
    }
}

Command parse_command(const std::vector<std::string>& tokens, std::size_t line) {
    const std::string& name = tokens[2];
    if (name == "NoOp") {
        expect_arg_count(tokens, 0, line);
        return NoOp{};
    }
    if (name == "DebugSetComponentValue") {
        expect_arg_count(tokens, 2, line);
        return DebugSetComponentValue{parse_number<std::uint32_t>(tokens[3], line, "entity"),
                                      parse_number<std::int32_t>(tokens[4], line, "value")};
    }
    fail(line, "unknown command '" + name + "'");
}

} // namespace

void write_log_file(std::ostream& out, const LogFile& log) {
    out << "citysim-input-log 1\n";
    out << "seed " << log.seed << '\n';
    out << "ticks " << log.ticks << '\n';
    for (const InputLogEntry& entry : log.entries) {
        out << entry.tick << ' ' << entry.sequence << ' ';
        std::visit(WriteCommand{out}, entry.cmd);
        out << '\n';
    }
}

LogFile read_log_file(std::istream& in) {
    LogFile log;
    std::string line;
    std::size_t line_number = 0;
    int header_lines = 0; // magic, seed, ticks, in that order

    while (std::getline(in, line)) {
        ++line_number;
        const std::vector<std::string> tokens = split(line);
        if (tokens.empty() || tokens[0][0] == '#') {
            continue;
        }

        if (header_lines == 0) {
            if (tokens.size() != 2 || tokens[0] != "citysim-input-log") {
                fail(line_number, "not a citysim input log");
            }
            if (tokens[1] != "1") {
                fail(line_number, "unsupported format version '" + tokens[1] + "'");
            }
            ++header_lines;
        } else if (header_lines == 1) {
            if (tokens.size() != 2 || tokens[0] != "seed") {
                fail(line_number, "expected 'seed <number>'");
            }
            log.seed = parse_number<std::uint64_t>(tokens[1], line_number, "seed");
            ++header_lines;
        } else if (header_lines == 2) {
            if (tokens.size() != 2 || tokens[0] != "ticks") {
                fail(line_number, "expected 'ticks <number>'");
            }
            log.ticks = parse_number<std::uint64_t>(tokens[1], line_number, "ticks");
            ++header_lines;
        } else {
            if (tokens.size() < 3) {
                fail(line_number, "expected '<tick> <sequence> <command> [arguments]'");
            }
            InputLogEntry entry;
            entry.tick = parse_number<std::uint64_t>(tokens[0], line_number, "tick");
            entry.sequence = parse_number<std::uint64_t>(tokens[1], line_number, "sequence");
            entry.cmd = parse_command(tokens, line_number);
            if (!log.entries.empty() && entry.tick < log.entries.back().tick) {
                fail(line_number, "entries must be in tick order");
            }
            log.entries.push_back(std::move(entry));
        }
    }

    if (header_lines < 3) {
        throw std::runtime_error("input log is missing its header (magic, seed, ticks)");
    }
    return log;
}

} // namespace citysim::headless
