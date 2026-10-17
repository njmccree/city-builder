#include "citysim/core/log.hpp"

#include <array>
#include <memory>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <stdexcept>

namespace citysim::log {

namespace {

constexpr std::array<std::string_view, k_category_count> k_names{"core", "runner", "content",
                                                                 "save", "sim",    "bridge"};

constexpr const char* k_pattern = "[%H:%M:%S.%e] [%n] [%l] %v";

struct Registry {
    std::array<std::shared_ptr<spdlog::logger>, k_category_count> loggers;

    Registry() {
        // Before init(): Info to stderr, so early messages are not lost.
        for (std::size_t i = 0; i < k_category_count; ++i) {
            auto sink = std::make_shared<spdlog::sinks::stderr_sink_mt>();
            loggers[i] = std::make_shared<spdlog::logger>(std::string(k_names[i]), sink);
            loggers[i]->set_level(spdlog::level::info);
            loggers[i]->set_pattern(k_pattern);
        }
    }
};

// Created on first use, so logging works from static initializers too.
Registry& registry() {
    static Registry instance;
    return instance;
}

} // namespace

std::string_view category_name(Category category) {
    return k_names[static_cast<std::size_t>(category)];
}

spdlog::logger& logger(Category category) {
    return *registry().loggers[static_cast<std::size_t>(category)];
}

void init(const LogConfig& config) {
    // Open the file first so a failure leaves the previous configuration untouched.
    std::shared_ptr<spdlog::sinks::sink> file_sink;
    if (config.file_path && !config.file_path->empty()) {
        try {
            file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(*config.file_path, true);
        } catch (const spdlog::spdlog_ex& error) {
            throw std::runtime_error(std::string("cannot open log file: ") + error.what());
        }
    }

    Registry& reg = registry();
    for (std::size_t i = 0; i < k_category_count; ++i) {
        spdlog::logger& target = *reg.loggers[i];
        target.sinks().clear();
        if (config.console) {
            target.sinks().push_back(std::make_shared<spdlog::sinks::stderr_sink_mt>());
        }
        if (file_sink) {
            target.sinks().push_back(file_sink);
        }
        target.set_level(to_spdlog(config.levels[i]));
        target.set_pattern(k_pattern);
        target.flush_on(spdlog::level::warn);
    }
}

void flush() {
    for (const auto& logger_ptr : registry().loggers) {
        logger_ptr->flush();
    }
}

} // namespace citysim::log
