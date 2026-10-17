#include "headless/bench.hpp"
#include "headless/cli_options.hpp"
#include "headless/demo_scenario.hpp"
#include "headless/input_log_text.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace citysim;
using namespace citysim::headless;

namespace {
ParseResult parse(std::vector<std::string_view> args) {
    return parse_options(args);
}

LogFile read_text(const std::string& text) {
    std::istringstream in(text);
    return read_log_file(in);
}
} // namespace

TEST_CASE("CLI options: defaults and every flag", "[tools][headless]") {
    const auto defaults = parse({});
    REQUIRE(defaults.error.empty());
    REQUIRE(defaults.options.seed == 1);
    REQUIRE(defaults.options.ticks == 1000);
    REQUIRE(defaults.options.hash_every == 100);
    REQUIRE_FALSE(defaults.options.seed_given);

    const auto all = parse(
        {"--seed", "42", "--ticks", "500", "--hash-every", "0", "--threads", "8", "--record-log", "out.txt"});
    REQUIRE(all.error.empty());
    REQUIRE(all.options.seed == 42);
    REQUIRE(all.options.seed_given);
    REQUIRE(all.options.ticks == 500);
    REQUIRE(all.options.hash_every == 0);
    REQUIRE(all.options.threads == 8);
    REQUIRE(defaults.options.threads == 0);
    REQUIRE(all.options.record_log == "out.txt");

    const auto replay = parse({"--replay", "in.txt"});
    REQUIRE(replay.options.replay == "in.txt");
    REQUIRE(parse({"--help"}).options.help);
}

TEST_CASE("CLI options: bad input is reported, not guessed", "[tools][headless]") {
    REQUIRE_FALSE(parse({"--bogus"}).error.empty());
    REQUIRE_FALSE(parse({"--seed"}).error.empty());
    REQUIRE_FALSE(parse({"--seed", "abc"}).error.empty());
    REQUIRE_FALSE(parse({"--ticks", "-5"}).error.empty());
    REQUIRE_FALSE(parse({"--ticks", "12x"}).error.empty());
    REQUIRE_FALSE(parse({"--replay", "a", "--record-log", "b"}).error.empty());

    const auto saving = parse({"--save-at", "400", "game.csav", "--load", "other.csav"});
    REQUIRE(saving.error.empty());
    REQUIRE(saving.options.save_at_tick == 400);
    REQUIRE(saving.options.save_at_file == "game.csav");
    REQUIRE(saving.options.load_file == "other.csav");
    REQUIRE_FALSE(parse({"--save-at", "400"}).error.empty());
    REQUIRE_FALSE(parse({"--save-at", "x", "f"}).error.empty());
    REQUIRE_FALSE(parse({"--load", "a", "--replay", "b"}).error.empty());
    REQUIRE(usage().find("--replay") != std::string::npos);
}

TEST_CASE("input log text round-trips", "[tools][headless]") {
    LogFile log;
    log.seed = 42;
    log.ticks = 500;
    log.entries.push_back({25, 1, core::DebugSetComponentValue{12, -40}});
    log.entries.push_back({40, 2, core::NoOp{}});
    log.entries.push_back({40, 3, core::DebugSetComponentValue{0, 2147483647}});

    std::ostringstream out;
    write_log_file(out, log);
    REQUIRE(out.str() == "citysim-input-log 1\nseed 42\nticks 500\n"
                         "25 1 DebugSetComponentValue 12 -40\n"
                         "40 2 NoOp\n"
                         "40 3 DebugSetComponentValue 0 2147483647\n");

    const LogFile back = read_text(out.str());
    REQUIRE(back.seed == 42);
    REQUIRE(back.ticks == 500);
    REQUIRE(back.entries.size() == 3);
    REQUIRE(back.entries[0].tick == 25);
    REQUIRE(back.entries[0].sequence == 1);
    REQUIRE(std::get<core::DebugSetComponentValue>(back.entries[0].cmd).value == -40);
    REQUIRE(std::holds_alternative<core::NoOp>(back.entries[1].cmd));
}

TEST_CASE("input log text round-trips the clock commands", "[tools][headless]") {
    LogFile log;
    log.seed = 1;
    log.ticks = 10;
    log.entries.push_back({3, 1, citysim::time::SetSpeed{3}});
    log.entries.push_back({4, 2, citysim::time::TogglePause{}});

    std::ostringstream out;
    write_log_file(out, log);
    REQUIRE(out.str() == "citysim-input-log 1\nseed 1\nticks 10\n3 1 SetSpeed 3\n4 2 TogglePause\n");

    const LogFile back = read_text(out.str());
    REQUIRE(back.entries.size() == 2);
    REQUIRE(std::get<citysim::time::SetSpeed>(back.entries[0].cmd).speed == 3);
    REQUIRE(std::holds_alternative<citysim::time::TogglePause>(back.entries[1].cmd));

    // Speeds that do not fit the field are rejected by the parser, not wrapped around.
    REQUIRE_THROWS(read_text("citysim-input-log 1\nseed 1\nticks 1\n1 1 SetSpeed 300\n"));
}

TEST_CASE("input log text round-trips SetEra", "[tools][headless]") {
    LogFile log;
    log.seed = 1;
    log.ticks = 10;
    log.entries.push_back({3, 1, citysim::time::SetEra{"base:era.modern"}});

    std::ostringstream out;
    write_log_file(out, log);
    REQUIRE(out.str() == "citysim-input-log 1\nseed 1\nticks 10\n3 1 SetEra base:era.modern\n");
    const LogFile back = read_text(out.str());
    REQUIRE(back.entries.size() == 1);
    REQUIRE(std::get<citysim::time::SetEra>(back.entries[0].cmd).era_id == "base:era.modern");
    REQUIRE_THROWS(read_text("citysim-input-log 1\nseed 1\nticks 1\n1 1 SetEra\n")); // missing its argument
}

TEST_CASE("input log reader skips comments and blank lines", "[tools][headless]") {
    const LogFile log =
        read_text("# a comment\n\ncitysim-input-log 1\n\nseed 3\nticks 9\n# note\n5 1 NoOp\n");
    REQUIRE(log.seed == 3);
    REQUIRE(log.entries.size() == 1);
}

TEST_CASE("input log reader rejects malformed files with a line number", "[tools][headless]") {
    const auto message_of = [](const std::string& text) -> std::string {
        try {
            (void)read_text(text);
        } catch (const std::runtime_error& error) {
            return error.what();
        }
        return {};
    };
    const std::string header = "citysim-input-log 1\nseed 1\nticks 10\n";

    REQUIRE(message_of("not a log\n").find("line 1") != std::string::npos);
    REQUIRE(message_of("citysim-input-log 2\nseed 1\nticks 1\n").find("unsupported") != std::string::npos);
    REQUIRE_FALSE(message_of("citysim-input-log 1\nseed 1\n").empty()); // truncated header
    REQUIRE(message_of(header + "5 1 Bogus\n").find("line 4") != std::string::npos);
    REQUIRE_FALSE(message_of(header + "5 1 NoOp extra\n").empty());
    REQUIRE_FALSE(message_of(header + "5 1 DebugSetComponentValue 1\n").empty());
    REQUIRE_FALSE(message_of(header + "x 1 NoOp\n").empty());
    REQUIRE_FALSE(message_of(header + "5 1 DebugSetComponentValue 1 99999999999\n").empty());
    REQUIRE_FALSE(message_of(header + "9 1 NoOp\n5 2 NoOp\n").empty()); // out of tick order
    REQUIRE_FALSE(message_of("").empty());
}

TEST_CASE("demo input depends only on the seed and tick", "[tools][headless]") {
    REQUIRE(demo_input(42, 25).size() == 1);
    REQUIRE(demo_input(42, 40).size() == 1);  // a NoOp
    REQUIRE(demo_input(42, 200).size() == 2); // both
    REQUIRE(demo_input(42, 7).empty());
    REQUIRE(demo_input(42, 0).empty());
    REQUIRE(std::get<core::DebugSetComponentValue>(demo_input(42, 25)[0]).value ==
            std::get<core::DebugSetComponentValue>(demo_input(42, 25)[0]).value);
}

TEST_CASE("the bench option takes key=value pairs and keeps the other defaults", "[headless][bench]") {
    const auto defaults = parse({"--bench", ""});
    REQUIRE(defaults.error.empty());
    REQUIRE(defaults.options.bench.has_value());
    CHECK(defaults.options.bench->agents == 10000);
    CHECK(defaults.options.bench->ticks == 2000);
    CHECK(defaults.options.bench->map == 512);

    const auto custom =
        parse({"--bench", "agents=50000,ticks=300,threads=1,map=256,searches=100", "--seed", "5"});
    REQUIRE(custom.error.empty());
    CHECK(custom.options.bench->agents == 50000);
    CHECK(custom.options.bench->ticks == 300);
    CHECK(custom.options.bench->threads == 1);
    CHECK(custom.options.bench->map == 256);
    CHECK(custom.options.bench->searches == 100);
    CHECK(custom.options.seed == 5);

    CHECK_FALSE(parse({"--bench", "agents"}).error.empty());
    CHECK_FALSE(parse({"--bench", "agents=many"}).error.empty());
    CHECK_FALSE(parse({"--bench", "colour=3"}).error.empty());
    CHECK_FALSE(parse({"--bench", "ticks=0"}).error.empty());
    CHECK_FALSE(parse({"--bench"}).error.empty());
    CHECK_FALSE(parse({"--bench", "agents=1", "--replay", "x"}).error.empty());
    CHECK(usage().find("--bench") != std::string::npos);
}

TEST_CASE("timing summaries use nearest-rank percentiles", "[headless][bench]") {
    std::vector<double> values;
    for (int i = 100; i >= 1; --i) {
        values.push_back(static_cast<double>(i)); // 1..100, unsorted
    }
    const auto summary = citysim::headless::summarize(values);
    CHECK(summary.avg == 50.5);
    CHECK(summary.p50 == 50.0);
    CHECK(summary.p95 == 95.0);
    CHECK(summary.max == 100.0);

    const auto one = citysim::headless::summarize({7.0});
    CHECK(one.p50 == 7.0);
    CHECK(one.p95 == 7.0);
    CHECK(citysim::headless::summarize({}).max == 0.0);
}
