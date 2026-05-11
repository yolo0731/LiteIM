#include "liteim/base/Config.hpp"
#include "liteim/cache/LoginRateLimiter.hpp"
#include "liteim/cache/RedisClient.hpp"
#include "liteim/cache/RedisPool.hpp"
#include "liteim/cache/UnreadCounter.hpp"

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
    return 910000000000ULL + static_cast<std::uint64_t>(::getpid()) * 1000ULL + offset;
}

std::string testName(const std::string& suffix) {
    const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string("step30_") + std::to_string(static_cast<long long>(::getpid())) + "_" +
           test_info->test_suite_name() + "_" + test_info->name() + "_" + suffix;
}

liteim::UnreadKey unreadKey(std::uint64_t user_id, liteim::ConversationType type, std::uint64_t conversation_id) {
    liteim::UnreadKey key;
    key.user_id = user_id;
    key.conversation = {type, conversation_id};
    return key;
}

class Step30CacheIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        liteim::RedisClient probe;
        const auto status = probe.connect(testRedisConfig());
        if (!status.isOk()) {
            GTEST_SKIP() << "local LiteIM Redis is not available: " << status.message();
        }

        pool = std::make_unique<liteim::RedisPool>(testRedisConfig());
        ASSERT_TRUE(pool->start().isOk());
        unread_counter = std::make_unique<liteim::UnreadCounter>(*pool);
        login_limiter = std::make_unique<liteim::LoginRateLimiter>(*pool);
    }

    void TearDown() override {
        if (unread_counter) {
            for (const auto& key : unread_keys) {
                (void)unread_counter->clearUnread(key);
            }
        }
        if (login_limiter) {
            for (const auto& key : login_keys) {
                (void)login_limiter->clear(key);
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
    std::unique_ptr<liteim::UnreadCounter> unread_counter;
    std::unique_ptr<liteim::LoginRateLimiter> login_limiter;
    std::vector<liteim::UnreadKey> unread_keys;
    std::vector<liteim::LoginAttemptKey> login_keys;
};

} // namespace

TEST(UnreadCounterTest, RejectsInvalidInputBeforeBorrowingRedis) {
    liteim::RedisPool pool(testRedisConfig(1));
    liteim::UnreadCounter counter(pool);
    std::uint64_t unread_count = 100;

    auto status = counter.incrUnread(unreadKey(0, liteim::ConversationType::kPrivate, 1), 1, unread_count);
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);

    status = counter.incrUnread(unreadKey(1, liteim::ConversationType::kPrivate, 1), 0, unread_count);
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);

    status = counter.getUnread(unreadKey(1, liteim::ConversationType::kPrivate, 0), unread_count);
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
}

TEST(LoginRateLimiterTest, RejectsInvalidInputBeforeBorrowingRedis) {
    liteim::RedisPool pool(testRedisConfig(1));
    liteim::LoginRateLimiter limiter(pool);
    bool allowed = true;

    auto status = limiter.allow({"", "127.0.0.1"}, 3, allowed);
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);

    status = limiter.allow({"alice", ""}, 3, allowed);
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);

    status = limiter.allow({"alice", "127.0.0.1"}, 0, allowed);
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);

    status = limiter.recordFailure({"alice", "127.0.0.1"}, 0s);
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
}

TEST_F(Step30CacheIntegrationTest, UnreadCounterIncrementsReadsAndClearsCount) {
    const auto key = rememberUnreadKey(unreadKey(testUserId(1), liteim::ConversationType::kPrivate, 7001));
    std::uint64_t unread_count = 0;

    ASSERT_TRUE(unread_counter->getUnread(key, unread_count).isOk());
    EXPECT_EQ(unread_count, 0U);

    ASSERT_TRUE(unread_counter->incrUnread(key, 1, unread_count).isOk());
    EXPECT_EQ(unread_count, 1U);

    ASSERT_TRUE(unread_counter->incrUnread(key, 2, unread_count).isOk());
    EXPECT_EQ(unread_count, 3U);

    ASSERT_TRUE(unread_counter->getUnread(key, unread_count).isOk());
    EXPECT_EQ(unread_count, 3U);

    ASSERT_TRUE(unread_counter->clearUnread(key).isOk());
    ASSERT_TRUE(unread_counter->getUnread(key, unread_count).isOk());
    EXPECT_EQ(unread_count, 0U);
}

TEST_F(Step30CacheIntegrationTest, UnreadCounterSeparatesUsersAndConversations) {
    const auto alice_private = rememberUnreadKey(unreadKey(testUserId(2), liteim::ConversationType::kPrivate, 7002));
    const auto alice_group = rememberUnreadKey(unreadKey(testUserId(2), liteim::ConversationType::kGroup, 9001));
    const auto bob_private = rememberUnreadKey(unreadKey(testUserId(3), liteim::ConversationType::kPrivate, 7002));
    std::uint64_t unread_count = 0;

    ASSERT_TRUE(unread_counter->incrUnread(alice_private, 1, unread_count).isOk());
    ASSERT_TRUE(unread_counter->incrUnread(alice_group, 2, unread_count).isOk());
    ASSERT_TRUE(unread_counter->incrUnread(bob_private, 3, unread_count).isOk());

    ASSERT_TRUE(unread_counter->getUnread(alice_private, unread_count).isOk());
    EXPECT_EQ(unread_count, 1U);
    ASSERT_TRUE(unread_counter->getUnread(alice_group, unread_count).isOk());
    EXPECT_EQ(unread_count, 2U);
    ASSERT_TRUE(unread_counter->getUnread(bob_private, unread_count).isOk());
    EXPECT_EQ(unread_count, 3U);
}

TEST_F(Step30CacheIntegrationTest, LoginRateLimiterRejectsAfterFailureThresholdAndClearAllowsAgain) {
    const auto key = rememberLoginKey({testName("alice"), "127.0.0.1"});
    bool allowed = false;

    ASSERT_TRUE(login_limiter->allow(key, 3, allowed).isOk());
    EXPECT_TRUE(allowed);

    ASSERT_TRUE(login_limiter->recordFailure(key, 30s).isOk());
    ASSERT_TRUE(login_limiter->allow(key, 3, allowed).isOk());
    EXPECT_TRUE(allowed);

    ASSERT_TRUE(login_limiter->recordFailure(key, 30s).isOk());
    ASSERT_TRUE(login_limiter->allow(key, 3, allowed).isOk());
    EXPECT_TRUE(allowed);

    ASSERT_TRUE(login_limiter->recordFailure(key, 30s).isOk());
    ASSERT_TRUE(login_limiter->allow(key, 3, allowed).isOk());
    EXPECT_FALSE(allowed);

    ASSERT_TRUE(login_limiter->clear(key).isOk());
    ASSERT_TRUE(login_limiter->allow(key, 3, allowed).isOk());
    EXPECT_TRUE(allowed);
}

TEST_F(Step30CacheIntegrationTest, LoginRateLimiterTtlExpiryAllowsAgain) {
    const auto key = rememberLoginKey({testName("ttl"), "127.0.0.2"});
    bool allowed = true;

    ASSERT_TRUE(login_limiter->recordFailure(key, 1s).isOk());
    ASSERT_TRUE(login_limiter->recordFailure(key, 1s).isOk());
    ASSERT_TRUE(login_limiter->allow(key, 2, allowed).isOk());
    EXPECT_FALSE(allowed);

    std::this_thread::sleep_for(1500ms);

    ASSERT_TRUE(login_limiter->allow(key, 2, allowed).isOk());
    EXPECT_TRUE(allowed);
}

TEST_F(Step30CacheIntegrationTest, LoginRateLimiterSeparatesUsernameAndRemoteIp) {
    const auto alice_from_first_ip = rememberLoginKey({testName("alice"), "127.0.0.3"});
    const auto alice_from_second_ip = rememberLoginKey({testName("alice"), "127.0.0.4"});
    const auto bob_from_first_ip = rememberLoginKey({testName("bob"), "127.0.0.3"});
    bool allowed = false;

    ASSERT_TRUE(login_limiter->recordFailure(alice_from_first_ip, 30s).isOk());
    ASSERT_TRUE(login_limiter->recordFailure(alice_from_first_ip, 30s).isOk());

    ASSERT_TRUE(login_limiter->allow(alice_from_first_ip, 2, allowed).isOk());
    EXPECT_FALSE(allowed);
    ASSERT_TRUE(login_limiter->allow(alice_from_second_ip, 2, allowed).isOk());
    EXPECT_TRUE(allowed);
    ASSERT_TRUE(login_limiter->allow(bob_from_first_ip, 2, allowed).isOk());
    EXPECT_TRUE(allowed);
}
