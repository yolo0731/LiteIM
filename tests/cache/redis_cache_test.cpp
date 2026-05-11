#include "liteim/base/Config.hpp"
#include "liteim/cache/RedisCache.hpp"
#include "liteim/cache/RedisClient.hpp"
#include "liteim/cache/RedisPool.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
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
    return 920000000000ULL + static_cast<std::uint64_t>(::getpid()) * 1000ULL + offset;
}

std::string testName(const std::string& suffix) {
    const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string("pre31_") + std::to_string(static_cast<long long>(::getpid())) + "_" +
           test_info->test_suite_name() + "_" + test_info->name() + "_" + suffix;
}

liteim::UnreadKey unreadKey(std::uint64_t user_id, liteim::ConversationType type, std::uint64_t conversation_id) {
    liteim::UnreadKey key;
    key.user_id = user_id;
    key.conversation = {type, conversation_id};
    return key;
}

class RedisCacheIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        liteim::RedisClient probe;
        const auto status = probe.connect(testRedisConfig());
        if (!status.isOk()) {
            GTEST_SKIP() << "local LiteIM Redis is not available: " << status.message();
        }

        pool = std::make_unique<liteim::RedisPool>(testRedisConfig());
        ASSERT_TRUE(pool->start().isOk());
        cache = std::make_unique<liteim::RedisCache>(*pool);
    }

    void TearDown() override {
        if (cache) {
            for (const auto& user_id : online_user_ids) {
                (void)cache->setUserOffline(user_id);
            }
            for (const auto& key : unread_keys) {
                (void)cache->clearUnread(key);
            }
            for (const auto& key : login_keys) {
                (void)cache->clearLoginFailure(key);
            }
        }
    }

    liteim::UnreadKey rememberUnreadKey(liteim::UnreadKey key) {
        unread_keys.push_back(key);
        return key;
    }

    liteim::LoginAttemptKey rememberLoginKey(liteim::LoginAttemptKey key) {
        login_keys.push_back(key);
        return key;
    }

    std::unique_ptr<liteim::RedisPool> pool;
    std::unique_ptr<liteim::RedisCache> cache;
    std::vector<std::uint64_t> online_user_ids;
    std::vector<liteim::UnreadKey> unread_keys;
    std::vector<liteim::LoginAttemptKey> login_keys;
};

} // namespace

TEST(RedisCacheTest, HeaderIsSelfContained) {
    liteim::RedisPool pool(testRedisConfig());
    liteim::RedisCache cache(pool);
    liteim::ICache* interface = &cache;
    EXPECT_NE(interface, nullptr);
}

TEST_F(RedisCacheIntegrationTest, ImplementsICacheForOnlineUnreadAndLoginFailureState) {
    liteim::ICache& interface = *cache;
    const auto user_id = testUserId(1);
    online_user_ids.push_back(user_id);

    liteim::OnlineSession session;
    session.user_id = user_id;
    session.session_id = 7001;
    session.server_id = testName("server");
    session.last_active_time_ms = 1001;

    ASSERT_TRUE(interface.setUserOnline(session, 30s).isOk());
    bool online = false;
    ASSERT_TRUE(interface.isUserOnline(user_id, online).isOk());
    EXPECT_TRUE(online);

    liteim::OnlineSession found_session;
    ASSERT_TRUE(interface.getOnlineSession(user_id, found_session).isOk());
    EXPECT_EQ(found_session.session_id, session.session_id);

    const auto unread = rememberUnreadKey(unreadKey(user_id, liteim::ConversationType::kPrivate, 8001));
    std::uint64_t unread_count = 0;
    ASSERT_TRUE(interface.incrUnread(unread, 2, unread_count).isOk());
    EXPECT_EQ(unread_count, 2U);
    ASSERT_TRUE(interface.clearUnread(unread).isOk());

    const auto login = rememberLoginKey({testName("alice"), "127.0.0.11"});
    bool allowed = false;
    ASSERT_TRUE(interface.allowLoginAttempt(login, 1, allowed).isOk());
    EXPECT_TRUE(allowed);
    ASSERT_TRUE(interface.recordLoginFailure(login, 30s).isOk());
    ASSERT_TRUE(interface.allowLoginAttempt(login, 1, allowed).isOk());
    EXPECT_FALSE(allowed);
    ASSERT_TRUE(interface.clearLoginFailure(login).isOk());
}
