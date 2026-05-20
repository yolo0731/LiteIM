#include "liteim/service/AuthService.hpp"

#include "liteim/base/Config.hpp"
#include "liteim/cache/ICache.hpp"
#include "liteim/cache/RedisCache.hpp"
#include "liteim/cache/RedisClient.hpp"
#include "liteim/cache/RedisPool.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim/service/MessageRouter.hpp"
#include "liteim/service/OnlineService.hpp"
#include "liteim/service/SessionManager.hpp"
#include "liteim/storage/IStorage.hpp"
#include "liteim/storage/MySqlPool.hpp"
#include "liteim/storage/MySqlStorage.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
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

std::shared_ptr<liteim::Session> makeSession(liteim::EventLoop& loop, std::uint64_t session_id,
                                             std::string peer_ip = {}) {
    auto sockets = makeSocketPair();
    return std::make_shared<liteim::Session>(&loop, std::move(sockets.server), session_id,
                                             std::move(peer_ip));
}

liteim::Packet makePacket(liteim::MessageType type, std::uint64_t seq_id, liteim::Bytes body) {
    liteim::Packet packet;
    packet.header.msg_type = type;
    packet.header.seq_id = seq_id;
    packet.body = std::move(body);
    return packet;
}

liteim::Bytes authBody(const std::string& username, const std::string& password,
                       const std::optional<std::string>& nickname = std::nullopt) {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendString(liteim::TlvType::Username, username, body).isOk());
    EXPECT_TRUE(liteim::appendString(liteim::TlvType::Password, password, body).isOk());
    if (nickname.has_value()) {
        EXPECT_TRUE(liteim::appendString(liteim::TlvType::Nickname, *nickname, body).isOk());
    }
    return body;
}

std::uint64_t uint64Field(const liteim::Packet& packet, liteim::TlvType type) {
    liteim::TlvMap fields;
    EXPECT_TRUE(liteim::parseTlvMap(packet.body, fields).isOk());
    std::uint64_t value = 0;
    EXPECT_TRUE(liteim::getUint64(fields, type, value).isOk());
    return value;
}

std::string stringField(const liteim::Packet& packet, liteim::TlvType type) {
    liteim::TlvMap fields;
    EXPECT_TRUE(liteim::parseTlvMap(packet.body, fields).isOk());
    std::string value;
    EXPECT_TRUE(liteim::getString(fields, type, value).isOk());
    return value;
}

class FakeStorage final : public liteim::IStorage {
public:
    liteim::Status createUser(const liteim::CreateUserRequest& request,
                              liteim::UserRecord& created_user) override {
        if (users_by_name.find(request.username) != users_by_name.end()) {
            return liteim::Status::error(liteim::ErrorCode::AlreadyExists,
                                         "username already exists");
        }

        liteim::UserRecord user;
        user.user_id = next_user_id++;
        user.username = request.username;
        user.password_hash = request.password_hash;
        user.password_salt = request.password_salt;
        user.nickname = request.nickname;
        user.created_at_ms = 1000 + static_cast<std::int64_t>(user.user_id);
        users_by_name[user.username] = user;
        users_by_id[user.user_id] = user;
        created_user = user;
        ++create_user_calls;
        return liteim::Status::ok();
    }

    liteim::Status findUserByUsername(const std::string& username,
                                      liteim::UserRecord& user) override {
        const auto it = users_by_name.find(username);
        if (it == users_by_name.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user was not found");
        }
        user = it->second;
        return liteim::Status::ok();
    }

    liteim::Status findUserById(std::uint64_t user_id, liteim::UserRecord& user) override {
        const auto it = users_by_id.find(user_id);
        if (it == users_by_id.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user was not found");
        }
        user = it->second;
        return liteim::Status::ok();
    }

    liteim::Status findMessageByClientMessageId(std::uint64_t, const std::string&,
                                                liteim::MessageRecord&) override {
        return liteim::Status::error(liteim::ErrorCode::NotFound, "message was not found");
    }

    liteim::Status addFriendship(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status getFriends(std::uint64_t,
                              std::vector<liteim::UserProfileRecord>&) override {
        return liteim::Status::ok();
    }

    liteim::Status createGroup(const liteim::CreateGroupRequest&,
                               liteim::GroupRecord&) override {
        return liteim::Status::ok();
    }

    liteim::Status addGroupMember(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status removeGroupMember(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status getGroupMembers(std::uint64_t,
                                   std::vector<liteim::GroupMemberRecord>&) override {
        return liteim::Status::ok();
    }

    liteim::Status findGroupById(std::uint64_t,
                                 liteim::GroupRecord&) override {
        return liteim::Status::ok();
    }

    liteim::Status getGroupsForUser(std::uint64_t,
                                    std::vector<liteim::GroupRecord>&) override {
        return liteim::Status::ok();
    }

    liteim::Status saveMessage(const liteim::MessageRecord&, std::uint64_t&) override {
        return liteim::Status::ok();
    }

    liteim::Status
    saveMessageWithOfflineRecipients(const liteim::MessageRecord&,
                                     const std::vector<std::uint64_t>&,
                                     liteim::MessageRecord&) override {
        return liteim::Status::ok();
    }

    liteim::Status saveOfflineMessage(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status getOfflineMessages(std::uint64_t, std::uint32_t,
                                      std::vector<liteim::OfflineMessageRecord>&) override {
        return liteim::Status::ok();
    }

    liteim::Status markOfflineDelivered(std::uint64_t,
                                        const std::vector<std::uint64_t>&) override {
        return liteim::Status::ok();
    }

    liteim::Status
    ackOfflineMessages(std::uint64_t, const std::vector<std::uint64_t>&,
                       std::vector<liteim::OfflineMessageRecord>& acked_messages) override {
        acked_messages.clear();
        return liteim::Status::ok();
    }

    liteim::Status ackPrivateMessageDelivery(std::uint64_t, std::uint64_t,
                                             liteim::MessageRecord&) override {
        return liteim::Status::error(liteim::ErrorCode::NotFound,
                                     "private message delivery target was not found");
    }

    liteim::Status getHistory(const liteim::HistoryQuery&,
                              std::vector<liteim::MessageRecord>&) override {
        return liteim::Status::ok();
    }

    std::unordered_map<std::string, liteim::UserRecord> users_by_name;
    std::unordered_map<std::uint64_t, liteim::UserRecord> users_by_id;
    std::uint64_t next_user_id{1001};
    int create_user_calls{0};
};

class FakeCache final : public liteim::ICache {
public:
    liteim::Status setUserOnline(const liteim::OnlineSession& session,
                                 std::chrono::seconds ttl) override {
        online_sessions[session.user_id] = session;
        online_ttls[session.user_id] = ttl;
        ++set_online_calls;
        return liteim::Status::ok();
    }

    liteim::Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl) override {
        const auto it = online_sessions.find(user_id);
        if (it == online_sessions.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user is offline");
        }
        online_ttls[user_id] = ttl;
        return liteim::Status::ok();
    }

    liteim::Status setUserOffline(std::uint64_t user_id) override {
        online_sessions.erase(user_id);
        online_ttls.erase(user_id);
        return liteim::Status::ok();
    }

    liteim::Status isUserOnline(std::uint64_t user_id, bool& online) override {
        online = online_sessions.find(user_id) != online_sessions.end();
        return liteim::Status::ok();
    }

    liteim::Status getOnlineSession(std::uint64_t user_id,
                                    liteim::OnlineSession& session) override {
        const auto it = online_sessions.find(user_id);
        if (it == online_sessions.end()) {
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

    liteim::Status allowLoginAttempt(const liteim::LoginAttemptKey& key,
                                     std::uint32_t max_failures, bool& allowed) override {
        login_keys.push_back(key);
        allowed = failure_counts[key.username + "|" + key.remote_ip] < max_failures;
        return liteim::Status::ok();
    }

    liteim::Status recordLoginFailure(const liteim::LoginAttemptKey& key,
                                      std::chrono::seconds ttl) override {
        login_keys.push_back(key);
        failure_ttls[key.username + "|" + key.remote_ip] = ttl;
        ++failure_counts[key.username + "|" + key.remote_ip];
        ++record_failure_calls;
        return liteim::Status::ok();
    }

    liteim::Status clearLoginFailure(const liteim::LoginAttemptKey& key) override {
        login_keys.push_back(key);
        failure_counts.erase(key.username + "|" + key.remote_ip);
        ++clear_failure_calls;
        return liteim::Status::ok();
    }

    int failureCount(const std::string& username, const std::string& remote_ip) const {
        const auto it = failure_counts.find(username + "|" + remote_ip);
        return it == failure_counts.end() ? 0 : static_cast<int>(it->second);
    }

    std::unordered_map<std::uint64_t, liteim::OnlineSession> online_sessions;
    std::unordered_map<std::uint64_t, std::chrono::seconds> online_ttls;
    std::unordered_map<std::string, std::uint32_t> failure_counts;
    std::unordered_map<std::string, std::chrono::seconds> failure_ttls;
    std::vector<liteim::LoginAttemptKey> login_keys;
    int set_online_calls{0};
    int record_failure_calls{0};
    int clear_failure_calls{0};
};

class AuthServiceFixture : public ::testing::Test {
protected:
    AuthServiceFixture()
        : online(sessions, cache, "server-a", 30s),
          service(storage, cache, online, liteim::AuthServiceOptions{2, 30s, "127.0.0.1"}) {}

    liteim::MessageRouter::RouterRequest requestFor(const liteim::Session::Ptr& session,
                                                    liteim::MessageType type,
                                                    liteim::Bytes body) {
        liteim::TlvMap fields;
        EXPECT_TRUE(liteim::parseTlvMap(body, fields).isOk());
        return liteim::MessageRouter::RouterRequest{
            session, makePacket(type, 7, std::move(body)), std::move(fields)};
    }

    liteim::Status registerUser(const liteim::Session::Ptr& session, const std::string& username,
                                const std::string& password,
                                const std::string& nickname = "Alice") {
        auto request =
            requestFor(session, liteim::MessageType::RegisterRequest,
                       authBody(username, password, std::optional<std::string>(nickname)));
        liteim::Packet response;
        return service.handleRegister(request, response);
    }

    liteim::EventLoop loop;
    FakeStorage storage;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService online;
    liteim::AuthService service;
};

liteim::MySqlConfig testMySqlConfig(std::uint32_t pool_size = 2) {
    auto config = liteim::Config::defaults().mysql;
    config.pool_size = pool_size;
    return config;
}

liteim::RedisConfig testRedisConfig(std::uint32_t pool_size = 2) {
    auto config = liteim::Config::defaults().redis;
    config.pool_size = pool_size;
    return config;
}

std::string uniqueUsername(const std::string& suffix) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::string("s34_") + std::to_string(static_cast<long long>(::getpid())) + "_" +
           std::to_string(static_cast<long long>(now % 1000000000LL)) + "_" + suffix;
}

class AuthServiceIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        liteim::MySqlConnection mysql_probe;
        const auto mysql_status = mysql_probe.connect(testMySqlConfig());
        if (!mysql_status.isOk()) {
            GTEST_SKIP() << "local LiteIM MySQL is not available: " << mysql_status.message();
        }

        liteim::RedisClient redis_probe;
        const auto redis_status = redis_probe.connect(testRedisConfig());
        if (!redis_status.isOk()) {
            GTEST_SKIP() << "local LiteIM Redis is not available: " << redis_status.message();
        }

        mysql_pool = std::make_unique<liteim::MySqlPool>(testMySqlConfig());
        ASSERT_TRUE(mysql_pool->start().isOk());
        redis_pool = std::make_unique<liteim::RedisPool>(testRedisConfig());
        ASSERT_TRUE(redis_pool->start().isOk());
        storage = std::make_unique<liteim::MySqlStorage>(*mysql_pool);
        cache = std::make_unique<liteim::RedisCache>(*redis_pool);
        online = std::make_unique<liteim::OnlineService>(sessions, *cache, uniqueUsername("srv"),
                                                         30s);
        service = std::make_unique<liteim::AuthService>(
            *storage, *cache, *online, liteim::AuthServiceOptions{3, 30s, "127.0.0.9"});
    }

    void TearDown() override {
        if (cache) {
            for (const auto user_id : online_user_ids) {
                (void)cache->setUserOffline(user_id);
            }
        }
    }

    liteim::MessageRouter::RouterRequest requestFor(const liteim::Session::Ptr& session,
                                                    liteim::MessageType type,
                                                    liteim::Bytes body) {
        liteim::TlvMap fields;
        EXPECT_TRUE(liteim::parseTlvMap(body, fields).isOk());
        return liteim::MessageRouter::RouterRequest{
            session, makePacket(type, 9, std::move(body)), std::move(fields)};
    }

    liteim::EventLoop loop;
    liteim::SessionManager sessions;
    std::unique_ptr<liteim::MySqlPool> mysql_pool;
    std::unique_ptr<liteim::RedisPool> redis_pool;
    std::unique_ptr<liteim::MySqlStorage> storage;
    std::unique_ptr<liteim::RedisCache> cache;
    std::unique_ptr<liteim::OnlineService> online;
    std::unique_ptr<liteim::AuthService> service;
    std::vector<std::uint64_t> online_user_ids;
};

}  // namespace

TEST(AuthServiceTest, HeaderIsSelfContained) {
    using Service = liteim::AuthService;
    using Options = liteim::AuthServiceOptions;

    static_assert(std::is_constructible_v<Service, liteim::IStorage&, liteim::ICache&,
                                          liteim::OnlineService&, Options>);
    static_assert(
        std::is_same_v<decltype(&Service::registerHandlers),
                       liteim::Status (Service::*)(liteim::MessageRouter&)>);
    static_assert(std::is_same_v<decltype(&Service::handleRegister),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
    static_assert(std::is_same_v<decltype(&Service::handleLogin),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
}

TEST_F(AuthServiceFixture, RegisterCreatesSaltedHashAndReturnsUserFields) {
    auto session = makeSession(loop, 5001);
    auto request =
        requestFor(session, liteim::MessageType::RegisterRequest,
                   authBody("alice", "secret", std::optional<std::string>("Alice")));
    liteim::Packet response;

    const auto status = service.handleRegister(request, response);
    ASSERT_TRUE(status.isOk()) << status.message();

    ASSERT_EQ(storage.users_by_name.count("alice"), 1U);
    const auto& user = storage.users_by_name["alice"];
    EXPECT_FALSE(user.password_salt.empty());
    EXPECT_FALSE(user.password_hash.empty());
    EXPECT_NE(user.password_hash, "secret");

    EXPECT_EQ(response.header.msg_type, liteim::MessageType::RegisterResponse);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::UserId), user.user_id);
    EXPECT_EQ(stringField(response, liteim::TlvType::Username), "alice");
    EXPECT_EQ(stringField(response, liteim::TlvType::Nickname), "Alice");
}

TEST_F(AuthServiceFixture, DuplicateUsernameRegisterReturnsAlreadyExists) {
    auto session = makeSession(loop, 5001);
    ASSERT_TRUE(registerUser(session, "alice", "secret").isOk());

    auto request =
        requestFor(session, liteim::MessageType::RegisterRequest,
                   authBody("alice", "another", std::optional<std::string>("Alice2")));
    liteim::Packet response;
    const auto status = service.handleRegister(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::AlreadyExists);
    EXPECT_EQ(storage.create_user_calls, 1);
}

TEST_F(AuthServiceFixture, RegisterRejectsFieldsLongerThanServiceLimits) {
    auto session = makeSession(loop, 5001);
    liteim::Packet response;

    auto long_username_request =
        requestFor(session, liteim::MessageType::RegisterRequest,
                   authBody(std::string(65, 'u'), "secret", std::optional<std::string>("Alice")));
    auto status = service.handleRegister(long_username_request, response);
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);

    auto long_password_request =
        requestFor(session, liteim::MessageType::RegisterRequest,
                   authBody("alice_long_pw", std::string(129, 'p'),
                            std::optional<std::string>("Alice")));
    status = service.handleRegister(long_password_request, response);
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);

    auto long_nickname_request =
        requestFor(session, liteim::MessageType::RegisterRequest,
                   authBody("alice_long_nick", "secret",
                            std::optional<std::string>(std::string(65, 'n'))));
    status = service.handleRegister(long_nickname_request, response);
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.create_user_calls, 0);
}

TEST_F(AuthServiceFixture, LoginRejectsFieldsLongerThanServiceLimitsBeforeLimiter) {
    auto session = makeSession(loop, 5001);
    auto request =
        requestFor(session, liteim::MessageType::LoginRequest,
                   authBody(std::string(65, 'u'), std::string(129, 'p')));
    liteim::Packet response;

    const auto status = service.handleLogin(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_TRUE(cache.login_keys.empty());
}

TEST_F(AuthServiceFixture, LoginWithCorrectPasswordBindsSessionWritesOnlineAndClearsFailures) {
    auto session = makeSession(loop, 5001);
    ASSERT_TRUE(registerUser(session, "alice", "secret").isOk());
    ASSERT_TRUE(cache.recordLoginFailure({"alice", "127.0.0.1"}, 30s).isOk());

    auto request =
        requestFor(session, liteim::MessageType::LoginRequest, authBody("alice", "secret"));
    liteim::Packet response;
    const auto status = service.handleLogin(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::LoginResponse);
    EXPECT_EQ(stringField(response, liteim::TlvType::Username), "alice");
    EXPECT_EQ(uint64Field(response, liteim::TlvType::UserId), storage.users_by_name["alice"].user_id);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::SessionId), session->id());
    EXPECT_EQ(cache.clear_failure_calls, 1);
    EXPECT_EQ(cache.failureCount("alice", "127.0.0.1"), 0);

    std::uint64_t bound_user_id = 0;
    ASSERT_TRUE(sessions.getUserBySession(session->id(), bound_user_id).isOk());
    EXPECT_EQ(bound_user_id, storage.users_by_name["alice"].user_id);
    ASSERT_EQ(cache.online_sessions.count(bound_user_id), 1U);
    EXPECT_EQ(cache.online_sessions[bound_user_id].session_id, session->id());
}

TEST_F(AuthServiceFixture, WrongPasswordRecordsFailureAndDoesNotBindSession) {
    auto session = makeSession(loop, 5001);
    ASSERT_TRUE(registerUser(session, "alice", "secret").isOk());

    auto request =
        requestFor(session, liteim::MessageType::LoginRequest, authBody("alice", "wrong"));
    liteim::Packet response;
    const auto status = service.handleLogin(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(cache.failureCount("alice", "127.0.0.1"), 1);
    EXPECT_EQ(cache.record_failure_calls, 1);
    std::uint64_t bound_user_id = 0;
    EXPECT_EQ(sessions.getUserBySession(session->id(), bound_user_id).code(),
              liteim::ErrorCode::NotFound);
}

TEST_F(AuthServiceFixture, LoginFailureUsesSessionPeerIpWhenAvailable) {
    auto session = makeSession(loop, 5001, "10.0.0.5");
    ASSERT_TRUE(registerUser(session, "alice", "secret").isOk());

    auto request =
        requestFor(session, liteim::MessageType::LoginRequest, authBody("alice", "wrong"));
    liteim::Packet response;
    const auto status = service.handleLogin(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(cache.failureCount("alice", "10.0.0.5"), 1);
    EXPECT_EQ(cache.failureCount("alice", "127.0.0.1"), 0);
    ASSERT_FALSE(cache.login_keys.empty());
    EXPECT_EQ(cache.login_keys.back().remote_ip, "10.0.0.5");
}

TEST_F(AuthServiceFixture, RepeatedWrongPasswordTriggersLoginLimit) {
    auto session = makeSession(loop, 5001);
    ASSERT_TRUE(registerUser(session, "alice", "secret").isOk());

    auto first =
        requestFor(session, liteim::MessageType::LoginRequest, authBody("alice", "bad-1"));
    liteim::Packet first_response;
    EXPECT_EQ(service.handleLogin(first, first_response).code(), liteim::ErrorCode::InvalidArgument);

    auto second =
        requestFor(session, liteim::MessageType::LoginRequest, authBody("alice", "bad-2"));
    liteim::Packet second_response;
    EXPECT_EQ(service.handleLogin(second, second_response).code(),
              liteim::ErrorCode::InvalidArgument);

    auto limited =
        requestFor(session, liteim::MessageType::LoginRequest, authBody("alice", "secret"));
    liteim::Packet limited_response;
    const auto limited_status = service.handleLogin(limited, limited_response);

    EXPECT_FALSE(limited_status.isOk());
    EXPECT_EQ(limited_status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(cache.failureCount("alice", "127.0.0.1"), 2);
    EXPECT_EQ(cache.record_failure_calls, 2);
    std::uint64_t bound_user_id = 0;
    EXPECT_EQ(sessions.getUserBySession(session->id(), bound_user_id).code(),
              liteim::ErrorCode::NotFound);
}

TEST_F(AuthServiceIntegrationTest, RegisterAndLoginWithMySqlStorageAndRedisCache) {
    ASSERT_NE(service, nullptr);
    auto session = makeSession(loop, 6001);
    const auto username = uniqueUsername("alice");

    auto register_request = requestFor(session, liteim::MessageType::RegisterRequest,
                                       authBody(username, "secret", username + "_nick"));
    liteim::Packet register_response;
    const auto register_status = service->handleRegister(register_request, register_response);
    ASSERT_TRUE(register_status.isOk()) << register_status.message();
    const auto user_id = uint64Field(register_response, liteim::TlvType::UserId);
    online_user_ids.push_back(user_id);

    auto login_request =
        requestFor(session, liteim::MessageType::LoginRequest, authBody(username, "secret"));
    liteim::Packet login_response;
    const auto login_status = service->handleLogin(login_request, login_response);
    ASSERT_TRUE(login_status.isOk()) << login_status.message();

    EXPECT_EQ(uint64Field(login_response, liteim::TlvType::UserId), user_id);
    bool online_flag = false;
    ASSERT_TRUE(cache->isUserOnline(user_id, online_flag).isOk());
    EXPECT_TRUE(online_flag);
    std::uint64_t bound_user_id = 0;
    ASSERT_TRUE(sessions.getUserBySession(session->id(), bound_user_id).isOk());
    EXPECT_EQ(bound_user_id, user_id);
}
