#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "liteim/base/Status.hpp"

namespace liteim {

struct MySqlConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{33060};
    std::string user{"liteim"};
    std::string password{"6"};
    std::string database{"liteim"};
    std::uint32_t pool_size{4};
};

struct RedisConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{63790};
    std::string password{"6"};
    std::uint32_t db{0};
    std::uint32_t pool_size{4};
};

struct QtClientConfig {
    std::string server_host{"127.0.0.1"};
    std::uint16_t server_port{9000};
};

struct Config {
    std::string server_host{"0.0.0.0"};
    std::uint16_t server_port{9000};
    std::uint32_t io_threads{4};
    std::uint32_t business_threads{4};
    std::size_t session_output_high_water_mark{4 * 1024 * 1024};
    std::string log_level{"info"};
    MySqlConfig mysql;
    RedisConfig redis;
    QtClientConfig qt_client;

    static Config defaults();
    Status loadFromFile(const std::filesystem::path& path);
};

} // namespace liteim
