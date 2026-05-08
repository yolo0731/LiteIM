#pragma once

#include <memory>
#include <string>

#include <spdlog/logger.h>

namespace liteim {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off,
};

LogLevel parseLogLevel(const std::string& level);

class Logger {
public:
    static void init(LogLevel level = LogLevel::Info);
    static std::shared_ptr<spdlog::logger> get();
    static void setLevel(LogLevel level);
};

} // namespace liteim
