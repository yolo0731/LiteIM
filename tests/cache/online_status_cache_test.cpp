#include "liteim/base/Config.hpp"
#include "liteim/cache/OnlineStatusCache.hpp"
#include "liteim/cache/RedisClient.hpp"
#include "liteim/cache/RedisPool.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
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

std::uint64_t testUserId(std::uint64_t offset) {
    return 900000000000ULL + static_cast<std::uint64_t>(::getpid()) * 100ULL + offset;
}

class OnlineStatusCacheIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        liteim::RedisClient probe;
        const auto status = probe.connect(testRedisConfig());
        if (!status.isOk()) {
            GTEST_SKIP() << "local LiteIM Redis is not available: " << status.message();
        }

        pool = std::make_unique<liteim::RedisPool>(testRedisConfig());
        ASSERT_TRUE(pool->start().isOk());
        cache = std::make_unique<liteim::OnlineStatusCache>(*pool);
    }

    void TearDown() override {
        if (cache) {
            for (const auto user_id : created_users) {
                (void)cache->setUserOffline(user_id);
            }
        }
    }

    std::uint64_t rememberUser(std::uint64_t offset) {
        const auto user_id = testUserId(offset);
        created_users.push_back(user_id);
        return user_id;
    }

    std::unique_ptr<liteim::RedisPool> pool;
    std::unique_ptr<liteim::OnlineStatusCache> cache;
    std::vector<std::uint64_t> created_users;
};

}  // namespace

TEST(OnlineStatusCacheTest, HeaderIsSelfContained) {
    liteim::RedisPool pool(testRedisConfig(1));
    liteim::OnlineStatusCache cache(pool);

    bool online = true;
    const auto status = cache.isUserOnline(0, online);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
}

TEST_F(OnlineStatusCacheIntegrationTest, SetUserOnlineMakesUserQueryable) {
    const auto user_id = rememberUser(1);

    const auto status = cache->setUserOnline(user_id, "server-a", 7001, 30s);

    ASSERT_TRUE(status.isOk()) << status.message();

    bool online = false;
    ASSERT_TRUE(cache->isUserOnline(user_id, online).isOk());
    EXPECT_TRUE(online);

    liteim::OnlineSession session;
    ASSERT_TRUE(cache->getOnlineSession(user_id, session).isOk());
    EXPECT_EQ(session.user_id, user_id);
    EXPECT_EQ(session.session_id, 7001U);
    EXPECT_EQ(session.server_id, "server-a");
    EXPECT_GT(session.last_active_time_ms, 0);
}

TEST_F(OnlineStatusCacheIntegrationTest, ServerIdMayContainColon) {
    const auto user_id = rememberUser(6);

    ASSERT_TRUE(cache->setUserOnline(user_id, "server:a:zone1", 7006, 30s).isOk());

    liteim::OnlineSession session;
    ASSERT_TRUE(cache->getOnlineSession(user_id, session).isOk());
    EXPECT_EQ(session.server_id, "server:a:zone1");
}

TEST_F(OnlineStatusCacheIntegrationTest, RefreshUserOnlineExtendsTtl) {
    const auto user_id = rememberUser(2);
    ASSERT_TRUE(cache->setUserOnline(user_id, "server-a", 7002, 1s).isOk());

    std::this_thread::sleep_for(700ms);
    const auto refresh_status = cache->refreshUserOnline(user_id, 2s);
    ASSERT_TRUE(refresh_status.isOk()) << refresh_status.message();

    std::this_thread::sleep_for(700ms);
    bool online = false;
    ASSERT_TRUE(cache->isUserOnline(user_id, online).isOk());
    EXPECT_TRUE(online);
}

TEST_F(OnlineStatusCacheIntegrationTest, SetUserOfflineRemovesOnlineSession) {
    const auto user_id = rememberUser(3);
    ASSERT_TRUE(cache->setUserOnline(user_id, "server-a", 7003, 30s).isOk());

    const auto offline_status = cache->setUserOffline(user_id);
    ASSERT_TRUE(offline_status.isOk()) << offline_status.message();

    bool online = true;
    ASSERT_TRUE(cache->isUserOnline(user_id, online).isOk());
    EXPECT_FALSE(online);

    liteim::OnlineSession session;
    const auto session_status = cache->getOnlineSession(user_id, session);
    EXPECT_FALSE(session_status.isOk());
    EXPECT_EQ(session_status.code(), liteim::ErrorCode::NotFound);
}

TEST_F(OnlineStatusCacheIntegrationTest, TtlExpiryMakesUserOffline) {
    const auto user_id = rememberUser(4);
    ASSERT_TRUE(cache->setUserOnline(user_id, "server-a", 7004, 1s).isOk());

    std::this_thread::sleep_for(1500ms);

    bool online = true;
    ASSERT_TRUE(cache->isUserOnline(user_id, online).isOk());
    EXPECT_FALSE(online);
}

TEST_F(OnlineStatusCacheIntegrationTest, RefreshMissingUserReturnsNotFound) {
    const auto status = cache->refreshUserOnline(testUserId(5), 30s);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::NotFound);
}
