#include "liteim/base/Config.hpp"
#include "liteim/cache/RedisClient.hpp"
#include "liteim/cache/RedisPool.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;

liteim::RedisConfig testRedisConfig(std::uint32_t pool_size = 2) {
    auto config = liteim::Config::defaults().redis;
    config.pool_size = pool_size;
    return config;
}

std::string testKey(const std::string& name) {
    const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    return "liteim:step28:" + std::to_string(static_cast<long long>(::getpid())) + ":" +
           test_info->test_suite_name() + ":" + test_info->name() + ":" + name;
}

class RedisIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        const auto status = client.connect(testRedisConfig());
        if (!status.isOk()) {
            GTEST_SKIP() << "local LiteIM Redis is not available: " << status.message();
        }
    }

    void TearDown() override {
        for (const auto& key : created_keys) {
            std::uint64_t removed_count = 0;
            (void)client.del(key, removed_count);
        }
    }

    std::string rememberKey(const std::string& name) {
        auto key = testKey(name);
        created_keys.push_back(key);
        return key;
    }

    liteim::RedisClient client;
    std::vector<std::string> created_keys;
};

class RedisPoolIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        liteim::RedisClient probe;
        const auto status = probe.connect(testRedisConfig());
        if (!status.isOk()) {
            GTEST_SKIP() << "local LiteIM Redis is not available: " << status.message();
        }
    }
};

} // namespace

TEST(RedisClientTest, HeaderIsSelfContained) {
    liteim::RedisClient client;
    EXPECT_FALSE(client.isConnected());

    liteim::RedisPool pool(testRedisConfig(1));
    EXPECT_FALSE(pool.started());
    EXPECT_FALSE(pool.closed());

    liteim::RedisConnectionGuard guard;
    EXPECT_FALSE(guard);
    EXPECT_EQ(guard.get(), nullptr);
}

TEST(RedisPoolTest, RejectsZeroPoolSize) {
    liteim::RedisPool pool(testRedisConfig(0));

    const auto status = pool.start();

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_FALSE(pool.started());
}

TEST(RedisClientTest, UnavailableRedisReturnsErrorStatus) {
    auto config = testRedisConfig();
    config.port = 1;

    liteim::RedisClient client;
    const auto status = client.connect(config);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::IoError);
    EXPECT_FALSE(client.isConnected());
}

TEST_F(RedisIntegrationTest, ConnectsAndPingsLocalRedis) {
    EXPECT_TRUE(client.isConnected());

    const auto status = client.ping();

    EXPECT_TRUE(status.isOk()) << status.message();
}

TEST_F(RedisIntegrationTest, SetexAndGetRoundTripValue) {
    const auto key = rememberKey("setex");
    ASSERT_TRUE(client.setex(key, "hello redis", 30s).isOk());

    std::optional<std::string> value;
    const auto status = client.get(key, value);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "hello redis");
}

TEST_F(RedisIntegrationTest, GetMissingKeyReturnsEmptyOptional) {
    std::optional<std::string> value;

    const auto status = client.get(testKey("missing"), value);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_FALSE(value.has_value());
}

TEST_F(RedisIntegrationTest, ExpireRefreshesTtl) {
    const auto key = rememberKey("expire");
    ASSERT_TRUE(client.setex(key, "ttl", 30s).isOk());

    bool updated = false;
    const auto status = client.expire(key, 60s, updated);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_TRUE(updated);
}

TEST_F(RedisIntegrationTest, IncrReturnsIncrementedInteger) {
    const auto key = rememberKey("incr");

    std::int64_t value = 0;
    ASSERT_TRUE(client.incr(key, value).isOk());
    EXPECT_EQ(value, 1);

    ASSERT_TRUE(client.incr(key, value).isOk());
    EXPECT_EQ(value, 2);
}

TEST_F(RedisIntegrationTest, DelRemovesExistingKey) {
    const auto key = rememberKey("del");
    ASSERT_TRUE(client.setex(key, "value", 30s).isOk());

    std::uint64_t removed_count = 0;
    const auto status = client.del(key, removed_count);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(removed_count, 1U);

    std::optional<std::string> value;
    ASSERT_TRUE(client.get(key, value).isOk());
    EXPECT_FALSE(value.has_value());
}

TEST_F(RedisIntegrationTest, EvalCanReadRedisKey) {
    const auto key = rememberKey("eval");
    ASSERT_TRUE(client.setex(key, "script-value", 30s).isOk());

    std::optional<std::string> value;
    const auto status = client.eval("return redis.call('GET', KEYS[1])", {key}, {}, value);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "script-value");
}

TEST_F(RedisPoolIntegrationTest, AcquiresConnectedRedisClient) {
    liteim::RedisPool pool(testRedisConfig(2));
    ASSERT_TRUE(pool.start().isOk());

    liteim::RedisConnectionGuard guard;
    const auto acquire_status = pool.acquire(200ms, guard);

    ASSERT_TRUE(acquire_status.isOk()) << acquire_status.message();
    ASSERT_TRUE(guard);
    ASSERT_NE(guard.get(), nullptr);
    EXPECT_TRUE(guard->ping().isOk());
    EXPECT_EQ(pool.size(), 2U);
}

TEST_F(RedisPoolIntegrationTest, AcquireTimesOutWhenAllClientsAreBorrowed) {
    liteim::RedisPool pool(testRedisConfig(1));
    ASSERT_TRUE(pool.start().isOk());

    liteim::RedisConnectionGuard first;
    ASSERT_TRUE(pool.acquire(200ms, first).isOk());

    liteim::RedisConnectionGuard second;
    const auto status = pool.acquire(20ms, second);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::IoError);
    EXPECT_FALSE(second);
}

TEST_F(RedisPoolIntegrationTest, ReleaseReturnsClientToPool) {
    liteim::RedisPool pool(testRedisConfig(1));
    ASSERT_TRUE(pool.start().isOk());

    liteim::RedisConnectionGuard guard;
    ASSERT_TRUE(pool.acquire(200ms, guard).isOk());
    ASSERT_TRUE(guard);

    pool.release(guard);
    EXPECT_FALSE(guard);

    liteim::RedisConnectionGuard next;
    const auto status = pool.acquire(200ms, next);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_TRUE(next);
}

TEST_F(RedisPoolIntegrationTest, MultipleThreadsAcquireAndReleaseClients) {
    liteim::RedisPool pool(testRedisConfig(2));
    ASSERT_TRUE(pool.start().isOk());

    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;
    for (int index = 0; index < 8; ++index) {
        threads.emplace_back([&]() {
            liteim::RedisConnectionGuard guard;
            const auto status = pool.acquire(1s, guard);
            if (status.isOk() && guard && guard->ping().isOk()) {
                ++success_count;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(success_count.load(), 8);
}

TEST_F(RedisPoolIntegrationTest, ReconnectsClientThatWasClosedWhileBorrowed) {
    liteim::RedisPool pool(testRedisConfig(1));
    ASSERT_TRUE(pool.start().isOk());

    {
        liteim::RedisConnectionGuard guard;
        ASSERT_TRUE(pool.acquire(200ms, guard).isOk());
        ASSERT_TRUE(guard);
        guard->close();
    }

    liteim::RedisConnectionGuard next;
    const auto status = pool.acquire(500ms, next);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_TRUE(next);
    EXPECT_TRUE(next->ping().isOk());
}
