#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace citysim::headless {

// What --bench measures: "agents=10000,ticks=2000,threads=0,map=512,searches=200". `map=world` runs the
// agents on the real terrain of a synthetic world instead of the flat test map.
struct BenchSpec {
    std::uint32_t agents = 10000;
    std::uint32_t ticks = 2000;
    std::uint32_t threads = 0; // JobSystem worker count; 0 = hardware default
    std::uint32_t map = 512;   // the test map is map x map cells
    bool world = false; // map=world: a world game on a synthetic 16 km map (one city, 2,048 x 2,048 cells)
    std::uint32_t searches = 200; // path searches allowed per tick
};

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
    std::optional<BenchSpec> bench;       // --bench: measure speed instead of running a game
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

// Parses the value of --bench: comma-separated key=value pairs, any subset of the keys of BenchSpec. Returns
// an empty string on success, otherwise the reason.
std::string parse_bench_spec(std::string_view text, BenchSpec& spec);

std::string usage();

} // namespace citysim::headless
