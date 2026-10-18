#include "headless/cli_options.hpp"

#include <charconv>

namespace citysim::headless {

namespace {
bool parse_u64(std::string_view text, std::uint64_t& out) {
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, out);
    return result.ec == std::errc{} && result.ptr == end && !text.empty();
}
} // namespace

ParseResult parse_options(std::span<const std::string_view> args) {
    ParseResult result;
    Options& options = result.options;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        const auto next_value = [&](std::string_view& value) {
            if (i + 1 >= args.size()) {
                result.error = "missing value for " + std::string(arg);
                return false;
            }
            value = args[++i];
            return true;
        };

        std::string_view value;
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--seed" || arg == "--ticks" || arg == "--hash-every" || arg == "--threads") {
            if (!next_value(value)) {
                return result;
            }
            std::uint64_t number = 0;
            if (!parse_u64(value, number)) {
                result.error = "invalid number for " + std::string(arg) + ": '" + std::string(value) + "'";
                return result;
            }
            if (arg == "--seed") {
                options.seed = number;
                options.seed_given = true;
            } else if (arg == "--ticks") {
                options.ticks = number;
                options.ticks_given = true;
            } else if (arg == "--threads") {
                options.threads = number;
            } else {
                options.hash_every = number;
            }
        } else if (arg == "--record-log" || arg == "--replay") {
            if (!next_value(value)) {
                return result;
            }
            (arg == "--record-log" ? options.record_log : options.replay) = std::string(value);
        } else if (arg == "--bench") {
            if (!next_value(value)) {
                return result;
            }
            BenchSpec spec;
            const std::string reason = parse_bench_spec(value, spec);
            if (!reason.empty()) {
                result.error = "invalid value for --bench: " + reason;
                return result;
            }
            options.bench = spec;
        } else if (arg == "--load") {
            if (!next_value(value)) {
                return result;
            }
            options.load_file = std::string(value);
        } else if (arg == "--save-at") {
            std::string_view file;
            if (!next_value(value) || !next_value(file)) {
                return result;
            }
            std::uint64_t number = 0;
            if (!parse_u64(value, number)) {
                result.error = "invalid number for --save-at: '" + std::string(value) + "'";
                return result;
            }
            options.save_at_tick = number;
            options.save_at_file = std::string(file);
        } else {
            result.error = "unknown option: " + std::string(arg);
            return result;
        }
    }

    if (options.bench &&
        (options.replay || options.record_log || options.load_file || options.save_at_tick)) {
        result.error = "--bench cannot be combined with --replay, --record-log, --load or --save-at";
    } else if (options.replay && options.record_log) {
        result.error = "--replay and --record-log cannot be combined";
    } else if (options.load_file && (options.replay || options.record_log)) {
        result.error = "--load cannot be combined with --replay or --record-log";
    }
    return result;
}

std::string parse_bench_spec(std::string_view text, BenchSpec& spec) {
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        const std::string_view pair = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
        const std::size_t equals = pair.find('=');
        if (equals == std::string_view::npos) {
            return "'" + std::string(pair) + "' is not key=value";
        }
        const std::string_view key = pair.substr(0, equals);
        if (key == "map" && pair.substr(equals + 1) == "world") {
            spec.world = true;
            continue;
        }
        std::uint64_t number = 0;
        if (!parse_u64(pair.substr(equals + 1), number) || number > 0xFFFFFFFFULL) {
            return "'" + std::string(pair) + "' needs a whole number";
        }
        const auto value = static_cast<std::uint32_t>(number);
        if (key == "agents") {
            spec.agents = value;
        } else if (key == "ticks") {
            spec.ticks = value;
        } else if (key == "threads") {
            spec.threads = value;
        } else if (key == "map") {
            spec.map = value;
            spec.world = false;
        } else if (key == "searches") {
            spec.searches = value;
        } else {
            return "unknown key '" + std::string(key) +
                   "' (agents, ticks, threads, map, searches); map is a number of cells or \"world\"";
        }
    }
    if (spec.ticks == 0 || spec.map == 0 || spec.searches == 0) {
        return "ticks, map and searches must be greater than 0";
    }
    return {};
}

std::string usage() {
    return "usage: citysim_headless [options]\n"
           "  --seed N         world seed (default 1)\n"
           "  --ticks N        number of ticks to run (default 1000)\n"
           "  --hash-every N   print \"tick hash\" every N ticks; 0 prints only the final hash (default "
           "100)\n"
           "  --threads N      worker threads for the job system; 0 = hardware default (never changes the "
           "result)\n"
           "  --record-log F   write the input log of this run to F\n"
           "  --replay F       re-run the game recorded in F (seed and ticks come from the file)\n"
           "  --bench SPEC     measure speed instead of playing: SPEC is "
           "agents=N,ticks=N,threads=N,map=N,searches=N (map=world: real terrain)\n"
           "                   (defaults 10000, 2000, 0, 512, 200); prints ms per tick and path searches\n"
           "  --save-at T F    after T ticks, save the game to F\n"
           "  --load F         resume from the save F; --ticks is the final tick (seed comes from the file)\n"
           "  --help           show this text\n";
}

} // namespace citysim::headless
