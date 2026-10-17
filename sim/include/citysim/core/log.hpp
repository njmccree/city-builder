#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>

// Categorized logging over spdlog (architecture §3, §4).
//
//     CS_LOG_INFO(Sim, "tick {} took {} ms", tick, ms);
//
// LOGGING MUST NEVER CHANGE SIMULATION STATE. The macros skip evaluating their arguments when the level
// is disabled, so an argument with a side effect (`CS_LOG_DEBUG(Sim, "{}", ++counter)`) would make the
// program behave differently depending on the log level. Arguments must be plain reads. Log output goes
// to stderr (stdout belongs to command-line tools) and optionally a file, never into the simulation.
//
// Not tied to a clock. Safe to call from any thread once init() has returned.
namespace citysim::log {

enum class Category : std::uint8_t { Core, Runner, Content, Save, Sim, Bridge };
inline constexpr std::size_t k_category_count = 6;

enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error, Off };

struct LogConfig {
    std::array<Level, k_category_count> levels = {Level::Info, Level::Info, Level::Info,
                                                  Level::Info, Level::Info, Level::Info};
    std::optional<std::string> file_path; // also write to this file (truncated); empty = no file
    bool console = true;                  // write to stderr

    static LogConfig all(Level level) {
        LogConfig config;
        config.levels.fill(level);
        return config;
    }
};

// Applies the configuration. Call once at startup, before other threads log; calling it again later
// (for example in tests) replaces the outputs but must not race with logging. Throws std::runtime_error if
// the file cannot be opened. Until it is called, every category logs at Info to stderr.
void init(const LogConfig& config);

void flush();

std::string_view category_name(Category category);

// The logger for a category. Prefer the macros.
spdlog::logger& logger(Category category);

constexpr spdlog::level::level_enum to_spdlog(Level level) {
    switch (level) {
    case Level::Trace:
        return spdlog::level::trace;
    case Level::Debug:
        return spdlog::level::debug;
    case Level::Info:
        return spdlog::level::info;
    case Level::Warn:
        return spdlog::level::warn;
    case Level::Error:
        return spdlog::level::err;
    case Level::Off:
        return spdlog::level::off;
    }
    return spdlog::level::off;
}

} // namespace citysim::log

#define CS_LOG_IMPL(level, category, ...)                                                                    \
    do {                                                                                                     \
        ::spdlog::logger& cs_log_logger_ = ::citysim::log::logger(::citysim::log::Category::category);       \
        if (cs_log_logger_.should_log(::citysim::log::to_spdlog(::citysim::log::Level::level))) {            \
            cs_log_logger_.log(::citysim::log::to_spdlog(::citysim::log::Level::level), __VA_ARGS__);        \
        }                                                                                                    \
    } while (false)

#define CS_LOG_TRACE(category, ...) CS_LOG_IMPL(Trace, category, __VA_ARGS__)
#define CS_LOG_DEBUG(category, ...) CS_LOG_IMPL(Debug, category, __VA_ARGS__)
#define CS_LOG_INFO(category, ...) CS_LOG_IMPL(Info, category, __VA_ARGS__)
#define CS_LOG_WARN(category, ...) CS_LOG_IMPL(Warn, category, __VA_ARGS__)
#define CS_LOG_ERROR(category, ...) CS_LOG_IMPL(Error, category, __VA_ARGS__)
