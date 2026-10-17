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
    std::uint64_t threads = 0;      // JobSystem worker count; 0 = hardware default. Never changes the result
    std::optional<std::string> record_log;
    std::optional<std::string> replay;
    std::optional<std::uint64_t> save_at_tick; // with save_at_file: save after this many ticks
    std::string save_at_file;
    std::optional<std::string> load_file; // resume from a save; --ticks is then the final tick
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
