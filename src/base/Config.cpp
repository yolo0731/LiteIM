#include "liteim/base/Config.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace liteim {
namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

Status parseUint32(const std::string& value, std::uint32_t& output) {
    try {
        std::size_t parsed = 0;
        const unsigned long number = std::stoul(value, &parsed, 10);
        if (parsed != value.size() || number > std::numeric_limits<std::uint32_t>::max()) {
            return Status::error(ErrorCode::ParseError, "invalid unsigned integer: " + value);
        }
        output = static_cast<std::uint32_t>(number);
        return Status::ok();
    } catch (const std::exception&) {
        return Status::error(ErrorCode::ParseError, "invalid unsigned integer: " + value);
    }
}

Status parsePort(const std::string& value, std::uint16_t& output) {
    std::uint32_t port = 0;
    auto status = parseUint32(value, port);
    if (!status.isOk()) {
        return status;
    }
    if (port == 0 || port > 65535) {
        return Status::error(ErrorCode::ParseError, "invalid port: " + value);
    }
    output = static_cast<std::uint16_t>(port);
    return Status::ok();
}

}  // namespace

Config Config::defaults() {
    return Config{};
}

Status Config::loadFromFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return Status::error(ErrorCode::NotFound, "config file not found: " + path.string());
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    std::uint32_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line.erase(comment_pos);
        }

        line = trim(std::move(line));
        if (line.empty()) {
            continue;
        }

        const auto equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            return Status::error(ErrorCode::ParseError,
                                 "invalid config line " + std::to_string(line_number));
        }

        auto key = trim(line.substr(0, equals_pos));
        auto value = trim(line.substr(equals_pos + 1));
        if (key.empty()) {
            return Status::error(ErrorCode::ParseError,
                                 "empty config key at line " + std::to_string(line_number));
        }

        values[std::move(key)] = std::move(value);
    }

    for (const auto& [key, value] : values) {
        Status status = Status::ok();
        if (key == "server.host") {
            server_host = value;
        } else if (key == "server.port") {
            status = parsePort(value, server_port);
        } else if (key == "server.io_threads") {
            status = parseUint32(value, io_threads);
        } else if (key == "server.business_threads") {
            status = parseUint32(value, business_threads);
        } else if (key == "log.level") {
            log_level = value;
        } else if (key == "mysql.host") {
            mysql.host = value;
        } else if (key == "mysql.port") {
            status = parsePort(value, mysql.port);
        } else if (key == "mysql.user") {
            mysql.user = value;
        } else if (key == "mysql.password") {
            mysql.password = value;
        } else if (key == "mysql.database") {
            mysql.database = value;
        } else if (key == "mysql.pool_size") {
            status = parseUint32(value, mysql.pool_size);
        } else if (key == "redis.host") {
            redis.host = value;
        } else if (key == "redis.port") {
            status = parsePort(value, redis.port);
        } else if (key == "redis.password") {
            redis.password = value;
        } else if (key == "redis.db") {
            status = parseUint32(value, redis.db);
        } else if (key == "redis.pool_size") {
            status = parseUint32(value, redis.pool_size);
        } else if (key == "qt.server_host") {
            qt_client.server_host = value;
        } else if (key == "qt.server_port") {
            status = parsePort(value, qt_client.server_port);
        } else {
            return Status::error(ErrorCode::InvalidArgument, "unknown config key: " + key);
        }

        if (!status.isOk()) {
            return status;
        }
    }

    return Status::ok();
}

}  // namespace liteim
