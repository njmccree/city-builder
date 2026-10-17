#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace citysim::headless {

struct Options {
    std::uint64_t seed = 1;
    std::uint64_t ticks = 1000;
    std::uint64_t hash_every = 100; // 0 prints only the final hash
    std::optional<std::string> record_log;
    std::optional<std::string> replay;
    bool seed_given = false;
    bool ticks_given = false;
    bool help = false;
};

struct ParseResult {
    Options options;
    std::string error; // empty on success
};

// Hand-written on purpose: the tool has no other need for an argument library. `args` excludes argv[0].
ParseResult parse_options(std::span<const std::string_view> args);

std::string usage();

} // namespace citysim::headless
