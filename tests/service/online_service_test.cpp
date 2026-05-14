#include "liteim/service/OnlineService.hpp"

#include "liteim/base/Config.hpp"
#include "liteim/cache/ICache.hpp"
#include "liteim/cache/RedisCache.hpp"
#include "liteim/cache/RedisClient.hpp"
#include "liteim/cache/RedisPool.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;

struct SocketPair {
    liteim::UniqueFd server;
    liteim::UniqueFd peer;
};

SocketPair makeSocketPair() {
    int fds[2] = {liteim::kInvalidFd, liteim::kInvalidFd};
    const int rc = ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    EXPECT_EQ(rc, 0) << "socketpair errno=" << errno;
    return SocketPair{liteim::UniqueFd(fds[0]), liteim::UniqueFd(fds[1])};
}

std::shared_ptr<liteim::Session> makeSession(liteim::EventLoop& loop, std::uint64_t session_id) {
    auto sockets = makeSocketPair();
    return std::make_shared<liteim::Session>(&loop, std::move(sockets.server), session_id);
}

class FakeCache final : public liteim::ICache {
public:
    liteim::Status setUserOnline(const liteim::OnlineSession& session,
                                 std::chrono::seconds ttl) override {
        sessions[session.user_id] = session;
        ttls[session.user_id] = ttl;
        ++set_online_calls;
        return liteim::Status::ok();
    }

    liteim::Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl) override {
        auto it = sessions.find(user_id);
        if (it == sessions.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user is offline");
        }
        ttls[user_id] = ttl;
        ++refresh_calls;
        return liteim::Status::ok();
    }

    liteim::Status setUserOffline(std::uint64_t user_id) override {
        sessions.erase(user_id);
        ttls.erase(user_id);
        ++set_offline_calls;
        offline_user_ids[user_id] += 1;
        return liteim::Status::ok();
    }

    liteim::Status isUserOnline(std::uint64_t user_id, bool& online) override {
        online = sessions.find(user_id) != sessions.end();
        return liteim::Status::ok();
    }

    liteim::Status getOnlineSession(std::uint64_t user_id, liteim::OnlineSession& session) override {
        auto it = sessions.find(user_id);
        if (it == sessions.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user is offline");
        }
        session = it->second;
        return liteim::Status::ok();
    }

    liteim::Status incrUnread(const liteim::UnreadKey&, std::uint64_t delta,
                              std::uint64_t& unread_count) override {
        unread_count += delta;
        return liteim::Status::ok();
    }

    liteim::Status getUnread(const liteim::UnreadKey&, std::uint64_t& unread_count) override {
        unread_count = 0;
        return liteim::Status::ok();
    }

    liteim::Status clearUnread(const liteim::UnreadKey&) override {
        return liteim::Status::ok();
    }

    liteim::Status allowLoginAttempt(const liteim::LoginAttemptKey&, std::uint32_t,
                                     bool& allowed) override {
        allowed = true;
        return liteim::Status::ok();
    }

    liteim::Status recordLoginFailure(const liteim::LoginAttemptKey&,
                                      std::chrono::seconds) override {
        return liteim::Status::ok();
    }

    liteim::Status clearLoginFailure(const liteim::LoginAttemptKey&) override {
        return liteim::Status::ok();
    }

    std::unordered_map<std::uint64_t, liteim::OnlineSession> sessions;
    std::unordered_map<std::uint64_t, std::chrono::seconds> ttls;
    std::unordered_map<std::uint64_t, int> offline_user_ids;
    int set_online_calls{0};
    int refresh_calls{0};
    int set_offline_calls{0};
};

liteim::RedisConfig testRedisConfig(std::uint32_t pool_size = 2) {
    auto config = liteim::Config::defaults().redis;
    config.pool_size = pool_size;
    return config;
}

std::uint64_t testUserId(std::uint64_t offset) {
    return 930000000000ULL + static_cast<std::uint64_t>(::getpid()) * 1000ULL + offset;
}

std::string testServerId() {
    const auto* test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    return std::string("step32_") + std::to_string(static_cast<long long>(::getpid())) + "_" +
           test_info->test_suite_name() + "_" + test_info->name();
}

class OnlineServiceRedisIntegrationTest : public ::testing::Test {
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
            for (const auto user_id : user_ids) {
                (void)cache->setUserOffline(user_id);
            }
        }
    }

    std::vector<std::uint64_t> user_ids;
    std::unique_ptr<liteim::RedisPool> pool;
    std::unique_ptr<liteim::RedisCache> cache;
};

}  // namespace

TEST(OnlineServiceTest, BindWritesCacheAndMemoryBinding) {
    liteim::EventLoop loop;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService service(sessions, cache, "server-a", 30s);
    auto session = makeSession(loop, 7001);

    const auto status = service.bindUser(42, session);
    ASSERT_TRUE(status.isOk()) << status.message();

    EXPECT_EQ(cache.set_online_calls, 1);
    ASSERT_EQ(cache.sessions.count(42), 1U);
    EXPECT_EQ(cache.sessions[42].session_id, 7001U);
    EXPECT_EQ(cache.sessions[42].server_id, "server-a");

    std::uint64_t user_id = 0;
    ASSERT_TRUE(service.getUserBySession(7001, user_id).isOk());
    EXPECT_EQ(user_id, 42U);
}

TEST(OnlineServiceTest, DuplicateLoginKeepsNewSessionOnlineAndDoesNotOfflineNewRedisKey) {
    liteim::EventLoop loop;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService service(sessions, cache, "server-a", 30s);
    auto old_session = makeSession(loop, 7001);
    auto new_session = makeSession(loop, 7002);

    ASSERT_TRUE(service.bindUser(42, old_session).isOk());
    ASSERT_TRUE(service.bindUser(42, new_session).isOk());
    ASSERT_TRUE(service.unbindUser(42, 7001).isOk());

    EXPECT_TRUE(old_session->closed());
    ASSERT_EQ(cache.sessions.count(42), 1U);
    EXPECT_EQ(cache.sessions[42].session_id, 7002U);
    EXPECT_EQ(cache.set_offline_calls, 0);
}

TEST(OnlineServiceTest, UnbindCurrentSessionClearsMemoryAndCache) {
    liteim::EventLoop loop;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService service(sessions, cache, "server-a", 30s);
    auto session = makeSession(loop, 7001);

    ASSERT_TRUE(service.bindUser(42, session).isOk());
    ASSERT_TRUE(service.unbindUser(42, 7001).isOk());

    EXPECT_EQ(cache.set_offline_calls, 1);
    EXPECT_EQ(cache.sessions.count(42), 0U);
    std::uint64_t user_id = 0;
    EXPECT_EQ(service.getUserBySession(7001, user_id).code(), liteim::ErrorCode::NotFound);
}

TEST(OnlineServiceTest, HeartbeatRefreshRequiresCurrentSessionBinding) {
    liteim::EventLoop loop;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService service(sessions, cache, "server-a", 30s);
    auto old_session = makeSession(loop, 7001);
    auto new_session = makeSession(loop, 7002);

    ASSERT_TRUE(service.bindUser(42, old_session).isOk());
    ASSERT_TRUE(service.bindUser(42, new_session).isOk());

    const auto old_status = service.refreshUserOnline(42, 7001);
    EXPECT_EQ(old_status.code(), liteim::ErrorCode::NotFound);

    ASSERT_TRUE(service.refreshUserOnline(42, 7002).isOk());
    EXPECT_EQ(cache.refresh_calls, 1);
}

TEST_F(OnlineServiceRedisIntegrationTest, SyncsOnlineStateToRedisCache) {
    liteim::EventLoop loop;
    liteim::SessionManager sessions;
    liteim::OnlineService service(sessions, *cache, testServerId(), 30s);
    const auto user_id = testUserId(1);
    user_ids.push_back(user_id);
    auto session = makeSession(loop, 8001);

    ASSERT_TRUE(service.bindUser(user_id, session).isOk());

    bool online = false;
    ASSERT_TRUE(cache->isUserOnline(user_id, online).isOk());
    EXPECT_TRUE(online);

    liteim::OnlineSession redis_session;
    ASSERT_TRUE(cache->getOnlineSession(user_id, redis_session).isOk());
    EXPECT_EQ(redis_session.session_id, 8001U);

    ASSERT_TRUE(service.refreshUserOnline(user_id, 8001).isOk());
    ASSERT_TRUE(service.unbindUser(user_id, 8001).isOk());
    ASSERT_TRUE(cache->isUserOnline(user_id, online).isOk());
    EXPECT_FALSE(online);
}
