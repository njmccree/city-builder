#include "citysim/core/log.hpp"
#include "citysim/profiling/profile.hpp"
#include "support/determinism.hpp"
#include "support/fn_system.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

using namespace citysim;
using citysim::log::Category;
using citysim::log::Level;
using citysim::log::LogConfig;

namespace {
// Puts logging back to a quiet default when a test ends, so tests cannot affect each other.
struct RestoreLogging {
    ~RestoreLogging() { log::init(LogConfig::all(Level::Off)); }
};

std::filesystem::path temp_log_path(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("citysim_log_test_" + name + ".log");
}

// Owns a temp log file. On destruction it first points logging away from the file, because Windows cannot
// delete a file that is still open (the sink holds it), then deletes it without ever throwing.
struct TempLogFile {
    explicit TempLogFile(const std::string& name) : path(temp_log_path(name)) {}
    ~TempLogFile() {
        log::init(LogConfig::all(Level::Off));
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    TempLogFile(const TempLogFile&) = delete;
    TempLogFile& operator=(const TempLogFile&) = delete;

    std::filesystem::path path;
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}
} // namespace

TEST_CASE("log categories have stable names", "[core][log]") {
    REQUIRE(log::category_name(Category::Core) == "core");
    REQUIRE(log::category_name(Category::Runner) == "runner");
    REQUIRE(log::category_name(Category::Content) == "content");
    REQUIRE(log::category_name(Category::Save) == "save");
    REQUIRE(log::category_name(Category::Sim) == "sim");
    REQUIRE(log::category_name(Category::Bridge) == "bridge");
}

TEST_CASE("log levels are applied per category", "[core][log]") {
    const RestoreLogging restore;
    const TempLogFile file("levels");
    const auto& path = file.path;

    LogConfig config = LogConfig::all(Level::Info);
    config.levels[static_cast<std::size_t>(Category::Sim)] = Level::Debug;
    config.levels[static_cast<std::size_t>(Category::Core)] = Level::Error;
    config.file_path = path.string();
    config.console = false;
    log::init(config);

    CS_LOG_TRACE(Sim, "sim-trace");
    CS_LOG_DEBUG(Sim, "sim-debug {}", 1);
    CS_LOG_INFO(Runner, "runner-info {}", "text");
    CS_LOG_DEBUG(Runner, "runner-debug");
    CS_LOG_WARN(Core, "core-warn");
    CS_LOG_ERROR(Core, "core-error");
    CS_LOG_INFO(Save, "save-info");
    log::flush();

    const std::string text = read_file(path);
    REQUIRE_FALSE(contains(text, "sim-trace"));
    REQUIRE(contains(text, "[sim] [debug] sim-debug 1"));
    REQUIRE(contains(text, "[runner] [info] runner-info text"));
    REQUIRE_FALSE(contains(text, "runner-debug"));
    REQUIRE_FALSE(contains(text, "core-warn")); // Core is at Error
    REQUIRE(contains(text, "[core] [error] core-error"));
    REQUIRE(contains(text, "[save] [info] save-info"));
}

TEST_CASE("Level::Off silences a category", "[core][log]") {
    const RestoreLogging restore;
    const TempLogFile file("off");
    const auto& path = file.path;
    LogConfig config = LogConfig::all(Level::Off);
    config.file_path = path.string();
    config.console = false;
    log::init(config);

    CS_LOG_ERROR(Core, "should-not-appear");
    log::flush();
    REQUIRE(read_file(path).empty());
}

TEST_CASE("log arguments are not evaluated when the level is disabled", "[core][log]") {
    // This is why log arguments must never have side effects: the program would behave differently
    // depending on the log level. The test documents the rule by showing the behavior.
    const RestoreLogging restore;
    LogConfig config = LogConfig::all(Level::Warn);
    config.console = false;
    log::init(config);

    int evaluations = 0;
    CS_LOG_DEBUG(Sim, "{}", ++evaluations);
    REQUIRE(evaluations == 0);
    CS_LOG_WARN(Sim, "{}", ++evaluations);
    REQUIRE(evaluations == 1);
}

TEST_CASE("init throws for an unwritable file and keeps the previous setup", "[core][log]") {
    const RestoreLogging restore;
    const TempLogFile file("keep");
    const auto& path = file.path;
    LogConfig good = LogConfig::all(Level::Info);
    good.file_path = path.string();
    good.console = false;
    log::init(good);

    // spdlog creates missing directories, so make the parent a regular file: that can never be opened.
    const auto not_a_directory = temp_log_path("not_a_directory");
    std::ofstream(not_a_directory) << "plain file";
    LogConfig bad = good;
    bad.file_path = (not_a_directory / "x.log").string();
    REQUIRE_THROWS_AS(log::init(bad), std::runtime_error);
    std::error_code ignored;
    std::filesystem::remove(not_a_directory, ignored);

    CS_LOG_INFO(Core, "still-working");
    log::flush();
    REQUIRE(contains(read_file(path), "still-working"));
}

TEST_CASE("log settings never change simulation results", "[core][log][determinism]") {
    const RestoreLogging restore;

    // A scenario that logs a lot: every 10th tick submits a command nobody handles, which warns.
    const auto setup = [](core::Simulation& sim) {
        sim.add_system(core::Phase::Update, test::make_system("tick-logger", [](core::TickContext& context) {
                           CS_LOG_TRACE(Sim, "tick {}", context.tick);
                           CS_LOG_DEBUG(Sim, "entities {}", context.world.entity_count());
                           context.world.create();
                       }));
    };
    test::Script script;
    for (std::uint64_t tick = 0; tick < 100; tick += 10) {
        script[tick] = {core::NoOp{}};
    }

    const TempLogFile file("determinism");
    const auto& path = file.path;
    LogConfig loud = LogConfig::all(Level::Trace);
    loud.file_path = path.string();
    loud.console = false;
    log::init(loud);
    const auto with_logging = test::run_scenario(5, 100, setup, script);
    log::flush();
    REQUIRE(contains(read_file(path), "no handler for command 'NoOp'")); // it really did log

    log::init(LogConfig::all(Level::Off));
    const auto without_logging = test::run_scenario(5, 100, setup, script);

    test::expect_same_hashes(with_logging, without_logging);
}

TEST_CASE("profiling macros compile in every configuration", "[core][profiling]") {
    std::string_view name = "dynamic-name";
    CS_PROFILE_ZONE("test-zone");
    { CS_PROFILE_ZONE_DYNAMIC(name); }
    CS_PROFILE_FRAME_MARK;
    SUCCEED();
}
