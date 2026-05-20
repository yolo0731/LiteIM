#include "liteim/service/FriendService.hpp"

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
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"
#include "liteim/storage/MySqlStorage.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
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

std::shared_ptr<liteim::Session> makeSession(liteim::EventLoop& loop, std::uint64_t session_id) {
    auto sockets = makeSocketPair();
    return std::make_shared<liteim::Session>(&loop, std::move(sockets.server), session_id);
}

liteim::Packet makePacket(liteim::MessageType type, std::uint64_t seq_id, liteim::Bytes body) {
    liteim::Packet packet;
    packet.header.msg_type = type;
    packet.header.seq_id = seq_id;
    packet.body = std::move(body);
    return packet;
}

liteim::Bytes addFriendBody(std::uint64_t target_user_id) {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::TargetUserId, target_user_id, body).isOk());
    return body;
}

std::vector<std::uint64_t> uint64Fields(const liteim::Packet& packet, liteim::TlvType type) {
    liteim::TlvMap fields;
    EXPECT_TRUE(liteim::parseTlvMap(packet.body, fields).isOk());
    std::vector<std::uint64_t> values;
    EXPECT_TRUE(liteim::getRepeatedUint64(fields, type, values).isOk());
    return values;
}

std::vector<std::string> stringFields(const liteim::Packet& packet, liteim::TlvType type) {
    liteim::TlvMap fields;
    EXPECT_TRUE(liteim::parseTlvMap(packet.body, fields).isOk());
    std::vector<std::string> values;
    EXPECT_TRUE(liteim::getRepeatedString(fields, type, values).isOk());
    return values;
}

std::uint64_t firstUint64Field(const liteim::Packet& packet, liteim::TlvType type) {
    const auto values = uint64Fields(packet, type);
    EXPECT_FALSE(values.empty());
    return values.empty() ? 0 : values.front();
}

std::string firstStringField(const liteim::Packet& packet, liteim::TlvType type) {
    const auto values = stringFields(packet, type);
    EXPECT_FALSE(values.empty());
    return values.empty() ? std::string{} : values.front();
}

class FakeStorage final : public liteim::IStorage {
public:
    liteim::Status createUser(const liteim::CreateUserRequest& request,
                              liteim::UserRecord& created_user) override {
        liteim::UserRecord user;
        user.user_id = next_user_id++;
        user.username = request.username;
        user.password_hash = request.password_hash;
        user.password_salt = request.password_salt;
        user.nickname = request.nickname;
        user.created_at_ms = 1000 + static_cast<std::int64_t>(user.user_id);
        users[user.user_id] = user;
        created_user = user;
        return liteim::Status::ok();
    }

    liteim::Status findUserByUsername(const std::string& username,
                                      liteim::UserRecord& user) override {
        for (const auto& item : users) {
            if (item.second.username == username) {
                user = item.second;
                return liteim::Status::ok();
            }
        }
        return liteim::Status::error(liteim::ErrorCode::NotFound, "user was not found");
    }

    liteim::Status findUserById(std::uint64_t user_id, liteim::UserRecord& user) override {
        const auto it = users.find(user_id);
        if (it == users.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user was not found");
        }
        user = it->second;
        return liteim::Status::ok();
    }

    liteim::Status findMessageByClientMessageId(std::uint64_t, const std::string&,
                                                liteim::MessageRecord&) override {
        return liteim::Status::error(liteim::ErrorCode::NotFound, "message was not found");
    }

    liteim::Status createFriendRequest(std::uint64_t requester_id, std::uint64_t target_user_id,
                                       liteim::FriendRequestRecord& request) override {
        ++create_friend_request_calls;
        last_requester_id = requester_id;
        last_target_user_id = target_user_id;
        if (requester_id == 0 || target_user_id == 0 || requester_id == target_user_id) {
            return liteim::Status::error(liteim::ErrorCode::InvalidArgument,
                                         "friend request user ids are invalid");
        }
        if (areFriendIds(requester_id, target_user_id) ||
            pending_requests.count({requester_id, target_user_id}) != 0U ||
            accepted_requests.count({requester_id, target_user_id}) != 0U ||
            rejected_requests.count({requester_id, target_user_id}) != 0U) {
            return liteim::Status::error(liteim::ErrorCode::AlreadyExists,
                                         "friend request already exists");
        }
        pending_requests.insert({requester_id, target_user_id});
        request = liteim::FriendRequestRecord{requester_id, target_user_id,
                                              liteim::FriendRequestStatus::kPending, 1001, 1001};
        return liteim::Status::ok();
    }

    liteim::Status acceptFriendRequest(std::uint64_t requester_id,
                                       std::uint64_t target_user_id) override {
        ++accept_friend_request_calls;
        last_requester_id = requester_id;
        last_target_user_id = target_user_id;
        const auto key = std::make_pair(requester_id, target_user_id);
        if (pending_requests.erase(key) == 0U) {
            if (accepted_requests.count(key) != 0U || areFriendIds(requester_id, target_user_id)) {
                return liteim::Status::error(liteim::ErrorCode::AlreadyExists,
                                             "friend request already accepted");
            }
            return liteim::Status::error(liteim::ErrorCode::NotFound,
                                         "pending friend request was not found");
        }
        accepted_requests.insert(key);
        addExistingFriendship(requester_id, target_user_id);
        return liteim::Status::ok();
    }

    liteim::Status rejectFriendRequest(std::uint64_t requester_id,
                                       std::uint64_t target_user_id) override {
        ++reject_friend_request_calls;
        last_requester_id = requester_id;
        last_target_user_id = target_user_id;
        const auto key = std::make_pair(requester_id, target_user_id);
        if (pending_requests.erase(key) == 0U) {
            return liteim::Status::error(liteim::ErrorCode::AlreadyExists,
                                         "friend request is not pending");
        }
        rejected_requests.insert(key);
        return liteim::Status::ok();
    }

    liteim::Status areFriends(std::uint64_t user_id, std::uint64_t friend_id,
                              bool& are_friends) override {
        ++are_friends_calls;
        last_are_friends_user_id = user_id;
        last_are_friends_friend_id = friend_id;
        are_friends = areFriendIds(user_id, friend_id);
        return liteim::Status::ok();
    }

    liteim::Status addFriendship(std::uint64_t user_id, std::uint64_t friend_id) override {
        ++add_friendship_calls;
        if (user_id == 0 || friend_id == 0 || user_id == friend_id) {
            return liteim::Status::error(liteim::ErrorCode::InvalidArgument,
                                         "friendship user ids are invalid");
        }
        if (users.find(user_id) == users.end() || users.find(friend_id) == users.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user was not found");
        }
        addExistingFriendship(user_id, friend_id);
        return liteim::Status::ok();
    }

    liteim::Status getFriends(std::uint64_t user_id,
                              std::vector<liteim::UserProfileRecord>& output) override {
        output = friends[user_id];
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

    void addUser(std::uint64_t user_id, const std::string& username,
                 const std::string& nickname) {
        liteim::UserRecord user;
        user.user_id = user_id;
        user.username = username;
        user.nickname = nickname;
        user.password_hash = "hash";
        user.password_salt = "salt";
        user.created_at_ms = 1000 + static_cast<std::int64_t>(user_id);
        users[user_id] = user;
    }

    void addExistingFriendship(std::uint64_t user_id, std::uint64_t friend_id) {
        insertFriendProfile(user_id, users[friend_id]);
        insertFriendProfile(friend_id, users[user_id]);
    }

    std::unordered_map<std::uint64_t, liteim::UserRecord> users;
    std::unordered_map<std::uint64_t, std::vector<liteim::UserProfileRecord>> friends;
    std::set<std::pair<std::uint64_t, std::uint64_t>> pending_requests;
    std::set<std::pair<std::uint64_t, std::uint64_t>> accepted_requests;
    std::set<std::pair<std::uint64_t, std::uint64_t>> rejected_requests;
    std::uint64_t next_user_id{10000};
    std::uint64_t last_requester_id{0};
    std::uint64_t last_target_user_id{0};
    std::uint64_t last_are_friends_user_id{0};
    std::uint64_t last_are_friends_friend_id{0};
    int create_friend_request_calls{0};
    int accept_friend_request_calls{0};
    int reject_friend_request_calls{0};
    int are_friends_calls{0};
    int add_friendship_calls{0};

private:
    bool areFriendIds(std::uint64_t user_id, std::uint64_t friend_id) const {
        const auto it = friends.find(user_id);
        if (it == friends.end()) {
            return false;
        }
        return std::any_of(it->second.begin(), it->second.end(),
                           [&](const liteim::UserProfileRecord& profile) {
                               return profile.user_id == friend_id;
                           });
    }

    void insertFriendProfile(std::uint64_t user_id, const liteim::UserRecord& friend_user) {
        auto& list = friends[user_id];
        const auto exists =
            std::any_of(list.begin(), list.end(), [&](const liteim::UserProfileRecord& profile) {
                return profile.user_id == friend_user.user_id;
            });
        if (exists) {
            return;
        }
        list.push_back(liteim::UserProfileRecord{friend_user.user_id, friend_user.username,
                                                 friend_user.nickname,
                                                 friend_user.created_at_ms});
        std::sort(list.begin(), list.end(),
                  [](const liteim::UserProfileRecord& lhs,
                     const liteim::UserProfileRecord& rhs) {
                      return lhs.user_id < rhs.user_id;
                  });
    }
};

class FakeCache final : public liteim::ICache {
public:
    liteim::Status setUserOnline(const liteim::OnlineSession& session,
                                 std::chrono::seconds) override {
        online_users.insert(session.user_id);
        return liteim::Status::ok();
    }

    liteim::Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds) override {
        if (online_users.find(user_id) == online_users.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user is offline");
        }
        return liteim::Status::ok();
    }

    liteim::Status setUserOffline(std::uint64_t user_id) override {
        online_users.erase(user_id);
        return liteim::Status::ok();
    }

    liteim::Status isUserOnline(std::uint64_t user_id, bool& online) override {
        queried_online_users.push_back(user_id);
        if (fail_online_status) {
            return liteim::Status::error(liteim::ErrorCode::IoError, "redis online failed");
        }
        online = online_users.find(user_id) != online_users.end();
        return liteim::Status::ok();
    }

    liteim::Status getOnlineSession(std::uint64_t user_id,
                                    liteim::OnlineSession& session) override {
        if (online_users.find(user_id) == online_users.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user is offline");
        }
        session.user_id = user_id;
        session.session_id = 9000 + user_id;
        session.server_id = "server-a";
        session.last_active_time_ms = 1000;
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

    std::set<std::uint64_t> online_users;
    std::vector<std::uint64_t> queried_online_users;
    bool fail_online_status{false};
};

class FriendServiceFixture : public ::testing::Test {
protected:
    FriendServiceFixture() : online(sessions, cache, "server-a", 30s),
                             service(storage, cache, online) {
        storage.addUser(1001, "alice", "Alice");
        storage.addUser(1002, "bob", "Bob");
        storage.addUser(1003, "charlie", "Charlie");
    }

    liteim::MessageRouter::RouterRequest requestFor(const liteim::Session::Ptr& session,
                                                    liteim::MessageType type,
                                                    liteim::Bytes body = {}) {
        liteim::TlvMap fields;
        EXPECT_TRUE(liteim::parseTlvMap(body, fields).isOk());
        return liteim::MessageRouter::RouterRequest{
            session, makePacket(type, 35, std::move(body)), std::move(fields)};
    }

    liteim::Session::Ptr bindAlice(std::uint64_t session_id = 7001) {
        auto session = makeSession(loop, session_id);
        EXPECT_TRUE(online.bindUser(1001, session).isOk());
        return session;
    }

    liteim::Session::Ptr bindBob(std::uint64_t session_id = 7002) {
        auto session = makeSession(loop, session_id);
        EXPECT_TRUE(online.bindUser(1002, session).isOk());
        return session;
    }

    liteim::EventLoop loop;
    FakeStorage storage;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService online;
    liteim::FriendService service;
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

std::string uniqueStep35Suffix() {
    static std::atomic<int> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(static_cast<long long>(::getpid())) + "_" +
           std::to_string(static_cast<long long>(ticks % 1000000LL)) + "_" +
           std::to_string(counter.fetch_add(1));
}

std::string uniqueStep35Username(const std::string& label) {
    return "s35_" + label + "_" + uniqueStep35Suffix();
}

void executeCleanupSql(liteim::MySqlConnection& connection, const std::string& sql) {
    liteim::PreparedStatement statement(connection);
    if (!statement.prepare(sql).isOk()) {
        return;
    }

    std::uint64_t affected_rows = 0;
    (void)statement.executeUpdate(affected_rows);
}

void cleanupStep35Rows(const liteim::MySqlConfig& config) {
    liteim::MySqlConnection connection;
    const auto connect_status = connection.connect(config);
    if (!connect_status.isOk()) {
        return;
    }

    executeCleanupSql(
        connection, "DELETE FROM friendships "
                    "WHERE user_id IN (SELECT user_id FROM users WHERE username LIKE 's35\\_%') "
                    "OR friend_id IN (SELECT user_id FROM users WHERE username LIKE 's35\\_%')");
    executeCleanupSql(
        connection,
        "DELETE FROM friend_requests "
        "WHERE requester_id IN (SELECT user_id FROM users WHERE username LIKE 's35\\_%') "
        "OR target_user_id IN (SELECT user_id FROM users WHERE username LIKE 's35\\_%')");
    executeCleanupSql(connection, "DELETE FROM users WHERE username LIKE 's35\\_%'");
}

class FriendServiceIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        mysql_config = testMySqlConfig();
        redis_config = testRedisConfig();

        liteim::MySqlConnection mysql_probe;
        const auto mysql_status = mysql_probe.connect(mysql_config);
        if (!mysql_status.isOk()) {
            GTEST_SKIP() << "local LiteIM MySQL is not available: " << mysql_status.message();
        }

        liteim::RedisClient redis_probe;
        const auto redis_status = redis_probe.connect(redis_config);
        if (!redis_status.isOk()) {
            GTEST_SKIP() << "local LiteIM Redis is not available: " << redis_status.message();
        }

        cleanupStep35Rows(mysql_config);
        mysql_pool = std::make_unique<liteim::MySqlPool>(mysql_config);
        ASSERT_TRUE(mysql_pool->start().isOk());
        redis_pool = std::make_unique<liteim::RedisPool>(redis_config);
        ASSERT_TRUE(redis_pool->start().isOk());
        storage = std::make_unique<liteim::MySqlStorage>(*mysql_pool);
        cache = std::make_unique<liteim::RedisCache>(*redis_pool);
        online = std::make_unique<liteim::OnlineService>(sessions, *cache, "step35-server", 30s);
        service = std::make_unique<liteim::FriendService>(*storage, *cache, *online);
    }

    void TearDown() override {
        if (cache) {
            for (const auto user_id : online_user_ids) {
                (void)cache->setUserOffline(user_id);
            }
        }
        service.reset();
        online.reset();
        cache.reset();
        storage.reset();
        if (redis_pool) {
            redis_pool->close();
            redis_pool.reset();
        }
        if (mysql_pool) {
            mysql_pool->close();
            mysql_pool.reset();
        }
        cleanupStep35Rows(mysql_config);
    }

    liteim::UserRecord createUser(const std::string& label) {
        const auto username = uniqueStep35Username(label);
        liteim::CreateUserRequest request;
        request.username = username;
        request.password_hash = "hash_" + username;
        request.password_salt = "salt_" + username;
        request.nickname = "Nick " + username;

        liteim::UserRecord user;
        const auto status = storage->createUser(request, user);
        EXPECT_TRUE(status.isOk()) << status.message();
        return user;
    }

    liteim::MessageRouter::RouterRequest requestFor(const liteim::Session::Ptr& session,
                                                    liteim::MessageType type,
                                                    liteim::Bytes body = {}) {
        liteim::TlvMap fields;
        EXPECT_TRUE(liteim::parseTlvMap(body, fields).isOk());
        return liteim::MessageRouter::RouterRequest{
            session, makePacket(type, 135, std::move(body)), std::move(fields)};
    }

    liteim::EventLoop loop;
    liteim::SessionManager sessions;
    liteim::MySqlConfig mysql_config;
    liteim::RedisConfig redis_config;
    std::unique_ptr<liteim::MySqlPool> mysql_pool;
    std::unique_ptr<liteim::RedisPool> redis_pool;
    std::unique_ptr<liteim::MySqlStorage> storage;
    std::unique_ptr<liteim::RedisCache> cache;
    std::unique_ptr<liteim::OnlineService> online;
    std::unique_ptr<liteim::FriendService> service;
    std::vector<std::uint64_t> online_user_ids;
};

}  // namespace

TEST(FriendServiceTest, HeaderIsSelfContained) {
    using Service = liteim::FriendService;

    static_assert(std::is_constructible_v<Service, liteim::IStorage&, liteim::ICache&,
                                          liteim::OnlineService&>);
    static_assert(
        std::is_same_v<decltype(&Service::registerHandlers),
                       liteim::Status (Service::*)(liteim::MessageRouter&)>);
    static_assert(std::is_same_v<decltype(&Service::handleAddFriend),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
    static_assert(std::is_same_v<decltype(&Service::handleAcceptFriend),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
    static_assert(std::is_same_v<decltype(&Service::handleRejectFriend),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
    static_assert(std::is_same_v<decltype(&Service::handleListFriends),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
}

TEST_F(FriendServiceFixture, AddFriendRequiresLoggedInSession) {
    auto session = makeSession(loop, 7001);
    auto request = requestFor(session, liteim::MessageType::AddFriendRequest, addFriendBody(1002));
    liteim::Packet response;

    const auto status = service.handleAddFriend(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.create_friend_request_calls, 0);
}

TEST_F(FriendServiceFixture, AddFriendCreatesPendingRequestAndReturnsFriendOnlineStatus) {
    auto session = bindAlice();
    cache.online_users.insert(1002);
    auto request = requestFor(session, liteim::MessageType::AddFriendRequest, addFriendBody(1002));
    liteim::Packet response;

    const auto status = service.handleAddFriend(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(storage.create_friend_request_calls, 1);
    EXPECT_EQ(storage.last_requester_id, 1001U);
    EXPECT_EQ(storage.last_target_user_id, 1002U);
    EXPECT_TRUE(storage.friends[1001].empty());
    EXPECT_EQ(storage.pending_requests.count({1001, 1002}), 1U);
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::AddFriendResponse);
    EXPECT_EQ(firstUint64Field(response, liteim::TlvType::FriendId), 1002U);
    EXPECT_EQ(firstStringField(response, liteim::TlvType::Username), "bob");
    EXPECT_EQ(firstStringField(response, liteim::TlvType::Nickname), "Bob");
    EXPECT_EQ(firstUint64Field(response, liteim::TlvType::OnlineStatus), 1U);
    EXPECT_EQ(firstUint64Field(response, liteim::TlvType::FriendRequestStatus),
              static_cast<std::uint64_t>(liteim::FriendRequestStatus::kPending));
}

TEST_F(FriendServiceFixture, RepeatedAddFriendReturnsAlreadyExists) {
    auto session = bindAlice();
    storage.pending_requests.insert({1001, 1002});
    auto request = requestFor(session, liteim::MessageType::AddFriendRequest, addFriendBody(1002));
    liteim::Packet response;

    const auto status = service.handleAddFriend(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::AlreadyExists);
    EXPECT_EQ(storage.create_friend_request_calls, 1);
}

TEST_F(FriendServiceFixture, AcceptFriendRequestCreatesAcceptedFriendship) {
    auto session = bindBob();
    cache.online_users.insert(1001);
    storage.pending_requests.insert({1001, 1002});
    auto request =
        requestFor(session, liteim::MessageType::AcceptFriendRequest, addFriendBody(1001));
    liteim::Packet response;

    const auto status = service.handleAcceptFriend(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(storage.accept_friend_request_calls, 1);
    EXPECT_EQ(storage.last_requester_id, 1001U);
    EXPECT_EQ(storage.last_target_user_id, 1002U);
    ASSERT_EQ(storage.friends[1001].size(), 1U);
    ASSERT_EQ(storage.friends[1002].size(), 1U);
    EXPECT_EQ(storage.friends[1001].front().user_id, 1002U);
    EXPECT_EQ(storage.friends[1002].front().user_id, 1001U);
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::AcceptFriendResponse);
    EXPECT_EQ(firstUint64Field(response, liteim::TlvType::FriendId), 1001U);
    EXPECT_EQ(firstStringField(response, liteim::TlvType::Username), "alice");
    EXPECT_EQ(firstUint64Field(response, liteim::TlvType::OnlineStatus), 1U);
    EXPECT_EQ(firstUint64Field(response, liteim::TlvType::FriendRequestStatus),
              static_cast<std::uint64_t>(liteim::FriendRequestStatus::kAccepted));
}

TEST_F(FriendServiceFixture, RejectFriendRequestDoesNotCreateFriendship) {
    auto session = bindBob();
    storage.pending_requests.insert({1001, 1002});
    auto request =
        requestFor(session, liteim::MessageType::RejectFriendRequest, addFriendBody(1001));
    liteim::Packet response;

    const auto status = service.handleRejectFriend(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(storage.reject_friend_request_calls, 1);
    EXPECT_EQ(storage.rejected_requests.count({1001, 1002}), 1U);
    EXPECT_TRUE(storage.friends[1001].empty());
    EXPECT_TRUE(storage.friends[1002].empty());
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::RejectFriendResponse);
    EXPECT_EQ(firstUint64Field(response, liteim::TlvType::FriendId), 1001U);
    EXPECT_EQ(firstUint64Field(response, liteim::TlvType::FriendRequestStatus),
              static_cast<std::uint64_t>(liteim::FriendRequestStatus::kRejected));
}

TEST_F(FriendServiceFixture, RepeatedAcceptFriendRequestReturnsAlreadyExists) {
    auto session = bindBob();
    storage.accepted_requests.insert({1001, 1002});
    storage.addExistingFriendship(1001, 1002);
    auto request =
        requestFor(session, liteim::MessageType::AcceptFriendRequest, addFriendBody(1001));
    liteim::Packet response;

    const auto status = service.handleAcceptFriend(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::AlreadyExists);
    EXPECT_EQ(storage.accept_friend_request_calls, 1);
}

TEST_F(FriendServiceFixture, ListFriendsReturnsProfilesAndOnlineStatus) {
    auto session = bindAlice();
    storage.addExistingFriendship(1001, 1002);
    storage.addExistingFriendship(1001, 1003);
    cache.online_users.insert(1002);
    auto request = requestFor(session, liteim::MessageType::ListFriendsRequest);
    liteim::Packet response;

    const auto status = service.handleListFriends(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::ListFriendsResponse);
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::FriendId),
              (std::vector<std::uint64_t>{1002, 1003}));
    EXPECT_EQ(stringFields(response, liteim::TlvType::Username),
              (std::vector<std::string>{"bob", "charlie"}));
    EXPECT_EQ(stringFields(response, liteim::TlvType::Nickname),
              (std::vector<std::string>{"Bob", "Charlie"}));
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::OnlineStatus),
              (std::vector<std::uint64_t>{1, 0}));
    EXPECT_EQ(cache.queried_online_users, (std::vector<std::uint64_t>{1002, 1003}));
}

TEST_F(FriendServiceFixture, ListFriendsDegradesOfflineWhenRedisOnlineStatusFails) {
    auto session = bindAlice();
    storage.addExistingFriendship(1001, 1002);
    cache.online_users.insert(1002);
    cache.fail_online_status = true;
    auto request = requestFor(session, liteim::MessageType::ListFriendsRequest);
    liteim::Packet response;

    const auto status = service.handleListFriends(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::ListFriendsResponse);
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::FriendId),
              (std::vector<std::uint64_t>{1002}));
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::OnlineStatus),
              (std::vector<std::uint64_t>{0}));
    EXPECT_EQ(cache.queried_online_users, (std::vector<std::uint64_t>{1002}));
}

TEST_F(FriendServiceIntegrationTest,
       RequestAcceptAndListFriendsWithMySqlStorageAndRedisOnlineStatus) {
    ASSERT_NE(service, nullptr);
    const auto alice = createUser("alice");
    const auto bob = createUser("bob");
    auto alice_session = makeSession(loop, 8101);
    auto bob_session = makeSession(loop, 8102);
    ASSERT_TRUE(online->bindUser(alice.user_id, alice_session).isOk());
    ASSERT_TRUE(online->bindUser(bob.user_id, bob_session).isOk());
    online_user_ids.push_back(alice.user_id);
    online_user_ids.push_back(bob.user_id);

    auto add_request =
        requestFor(alice_session, liteim::MessageType::AddFriendRequest,
                   addFriendBody(bob.user_id));
    liteim::Packet add_response;
    const auto add_status = service->handleAddFriend(add_request, add_response);
    ASSERT_TRUE(add_status.isOk()) << add_status.message();
    EXPECT_EQ(firstUint64Field(add_response, liteim::TlvType::FriendId), bob.user_id);
    EXPECT_EQ(firstUint64Field(add_response, liteim::TlvType::OnlineStatus), 1U);
    EXPECT_EQ(firstUint64Field(add_response, liteim::TlvType::FriendRequestStatus),
              static_cast<std::uint64_t>(liteim::FriendRequestStatus::kPending));

    auto pending_list_request =
        requestFor(alice_session, liteim::MessageType::ListFriendsRequest);
    liteim::Packet pending_list_response;
    const auto pending_list_status =
        service->handleListFriends(pending_list_request, pending_list_response);
    ASSERT_TRUE(pending_list_status.isOk()) << pending_list_status.message();
    EXPECT_TRUE(pending_list_response.body.empty());

    auto accept_request =
        requestFor(bob_session, liteim::MessageType::AcceptFriendRequest,
                   addFriendBody(alice.user_id));
    liteim::Packet accept_response;
    const auto accept_status = service->handleAcceptFriend(accept_request, accept_response);
    ASSERT_TRUE(accept_status.isOk()) << accept_status.message();
    EXPECT_EQ(firstUint64Field(accept_response, liteim::TlvType::FriendId), alice.user_id);
    EXPECT_EQ(firstUint64Field(accept_response, liteim::TlvType::FriendRequestStatus),
              static_cast<std::uint64_t>(liteim::FriendRequestStatus::kAccepted));

    auto list_request = requestFor(alice_session, liteim::MessageType::ListFriendsRequest);
    liteim::Packet list_response;
    const auto list_status = service->handleListFriends(list_request, list_response);
    ASSERT_TRUE(list_status.isOk()) << list_status.message();
    EXPECT_EQ(uint64Fields(list_response, liteim::TlvType::FriendId),
              (std::vector<std::uint64_t>{bob.user_id}));
    EXPECT_EQ(uint64Fields(list_response, liteim::TlvType::OnlineStatus),
              (std::vector<std::uint64_t>{1}));
}
