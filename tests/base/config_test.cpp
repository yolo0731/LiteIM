#include "liteim/base/Config.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace {

std::filesystem::path writeTempConfig(const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() /
                      std::filesystem::path{"liteim_config_test.conf"};
    std::ofstream output(path);
    output << content;
    return path;
}

}  // namespace

TEST(ConfigTest, DefaultsContainExpectedValues) {
    const auto config = liteim::Config::defaults();

    EXPECT_EQ(config.server_host, "0.0.0.0");
    EXPECT_EQ(config.server_port, 9000);
    EXPECT_EQ(config.io_threads, 4U);
    EXPECT_EQ(config.business_threads, 4U);
    EXPECT_EQ(config.mysql.host, "127.0.0.1");
    EXPECT_EQ(config.mysql.port, 3306);
    EXPECT_EQ(config.redis.port, 6379);
    EXPECT_EQ(config.qt_client.server_port, 9000);
}

TEST(ConfigTest, LoadFromFileOverridesConfiguredValues) {
    auto config = liteim::Config::defaults();
    const auto path = writeTempConfig(R"(
        # LiteIM test config
        server.host = 127.0.0.1
        server.port = 10086
        server.io_threads = 2
        server.business_threads = 8
        log.level = debug
        mysql.host = mysql.local
        mysql.port = 3307
        mysql.user = test_user
        mysql.password = test_pass
        mysql.database = test_db
        mysql.pool_size = 6
        redis.host = redis.local
        redis.port = 6380
        redis.password = redis_pass
        redis.db = 2
        redis.pool_size = 7
        qt.server_host = 192.168.1.9
        qt.server_port = 10087
    )");

    const auto status = config.loadFromFile(path);

    EXPECT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(config.server_host, "127.0.0.1");
    EXPECT_EQ(config.server_port, 10086);
    EXPECT_EQ(config.io_threads, 2U);
    EXPECT_EQ(config.business_threads, 8U);
    EXPECT_EQ(config.log_level, "debug");
    EXPECT_EQ(config.mysql.host, "mysql.local");
    EXPECT_EQ(config.mysql.port, 3307);
    EXPECT_EQ(config.mysql.user, "test_user");
    EXPECT_EQ(config.mysql.password, "test_pass");
    EXPECT_EQ(config.mysql.database, "test_db");
    EXPECT_EQ(config.mysql.pool_size, 6U);
    EXPECT_EQ(config.redis.host, "redis.local");
    EXPECT_EQ(config.redis.port, 6380);
    EXPECT_EQ(config.redis.password, "redis_pass");
    EXPECT_EQ(config.redis.db, 2U);
    EXPECT_EQ(config.redis.pool_size, 7U);
    EXPECT_EQ(config.qt_client.server_host, "192.168.1.9");
    EXPECT_EQ(config.qt_client.server_port, 10087);

    std::filesystem::remove(path);
}

TEST(ConfigTest, MissingValuesKeepDefaults) {
    auto config = liteim::Config::defaults();
    const auto path = writeTempConfig("server.port = 10000\n");

    const auto status = config.loadFromFile(path);

    EXPECT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(config.server_port, 10000);
    EXPECT_EQ(config.server_host, "0.0.0.0");
    EXPECT_EQ(config.mysql.host, "127.0.0.1");
    EXPECT_EQ(config.redis.port, 6379);

    std::filesystem::remove(path);
}

TEST(ConfigTest, MissingFileReturnsNotFound) {
    auto config = liteim::Config::defaults();

    const auto status = config.loadFromFile("/tmp/liteim_missing_config_file.conf");

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::NotFound);
}

TEST(ConfigTest, UnknownKeyFails) {
    auto config = liteim::Config::defaults();
    const auto path = writeTempConfig("unknown.key = value\n");

    const auto status = config.loadFromFile(path);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);

    std::filesystem::remove(path);
}

TEST(ConfigTest, InvalidPortFails) {
    auto config = liteim::Config::defaults();
    const auto path = writeTempConfig("server.port = 70000\n");

    const auto status = config.loadFromFile(path);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::ParseError);

    std::filesystem::remove(path);
}
