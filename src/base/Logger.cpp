#include "liteim/base/Logger.hpp"

#include <mutex>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace liteim {
namespace {

std::mutex g_logger_mutex;
std::shared_ptr<spdlog::logger> g_logger;

spdlog::level::level_enum toSpdlogLevel(LogLevel level);

void createLoggerIfNeeded(LogLevel level) {
    if (!g_logger) {
        g_logger = spdlog::stdout_color_mt("liteim");
        g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::set_default_logger(g_logger);
    }
    g_logger->set_level(toSpdlogLevel(level));
}

spdlog::level::level_enum toSpdlogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
            return spdlog::level::trace;
        case LogLevel::Debug:
            return spdlog::level::debug;
        case LogLevel::Info:
            return spdlog::level::info;
        case LogLevel::Warn:
            return spdlog::level::warn;
        case LogLevel::Error:
            return spdlog::level::err;
        case LogLevel::Critical:
            return spdlog::level::critical;
        case LogLevel::Off:
            return spdlog::level::off;
    }

    return spdlog::level::info;
}

}  // namespace

LogLevel parseLogLevel(std::string_view level) {
    if (level == "trace") {
        return LogLevel::Trace;
    }
    if (level == "debug") {
        return LogLevel::Debug;
    }
    if (level == "warn" || level == "warning") {
        return LogLevel::Warn;
    }
    if (level == "error") {
        return LogLevel::Error;
    }
    if (level == "critical") {
        return LogLevel::Critical;
    }
    if (level == "off") {
        return LogLevel::Off;
    }
    return LogLevel::Info;
}

void Logger::init(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    createLoggerIfNeeded(level);
}

std::shared_ptr<spdlog::logger> Logger::get() {
    std::lock_guard<std::mutex> lock(g_logger_mutex);
    createLoggerIfNeeded(LogLevel::Info);
    return g_logger;
}

void Logger::setLevel(LogLevel level) {
    get()->set_level(toSpdlogLevel(level));
}

}  // namespace liteim
