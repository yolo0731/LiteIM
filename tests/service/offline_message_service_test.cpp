#include "liteim/service/OfflineMessageService.hpp"

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
#include <optional>
#include <stdexcept>
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

liteim::Bytes offlineRequestBody(std::optional<std::uint64_t> limit = std::nullopt) {
    liteim::Bytes body;
    if (limit.has_value()) {
        EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::Limit, *limit, body).isOk());
    }
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

liteim::OfflineMessageRecord makeOfflineRecord(std::uint64_t offline_id,
                                               std::uint64_t user_id,
                                               liteim::ConversationType conversation_type,
                                               std::uint64_t conversation_id,
                                               std::uint64_t message_id,
                                               std::uint64_t sender_id,
                                               std::uint64_t receiver_id,
                                               const std::string& text,
                                               std::int64_t created_at_ms) {
    liteim::OfflineMessageRecord record;
    record.offline_message_id = offline_id;
    record.user_id = user_id;
    record.created_at_ms = created_at_ms - 10;
    record.message.message_id = message_id;
    record.message.conversation = {conversation_type, conversation_id};
    record.message.sender_id = sender_id;
    record.message.receiver_id = receiver_id;
    record.message.text = text;
    record.message.created_at_ms = created_at_ms;
    return record;
}

class FakeStorage final : public liteim::IStorage {
public:
    liteim::Status createUser(const liteim::CreateUserRequest&,
                              liteim::UserRecord&) override {
        return liteim::Status::ok();
    }

    liteim::Status findUserByUsername(const std::string&, liteim::UserRecord&) override {
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

    liteim::Status getOfflineMessages(std::uint64_t user_id,
                                      std::vector<liteim::OfflineMessageRecord>& messages) override {
        ++get_offline_calls;
        last_get_user_id = user_id;
        messages = pending[user_id];
        return liteim::Status::ok();
    }

    liteim::Status markOfflineDelivered(std::uint64_t user_id,
                                        const std::vector<std::uint64_t>& message_ids) override {
        ++mark_delivered_calls;
        last_mark_user_id = user_id;
        last_mark_message_ids = message_ids;
        auto& list = pending[user_id];
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&](const liteim::OfflineMessageRecord& record) {
                                      return std::find(message_ids.begin(), message_ids.end(),
                                                       record.message.message_id) !=
                                             message_ids.end();
                                  }),
                   list.end());
        return liteim::Status::ok();
    }

    liteim::Status getHistory(const liteim::HistoryQuery&,
                              std::vector<liteim::MessageRecord>&) override {
        return liteim::Status::ok();
    }

    void addUser(std::uint64_t user_id, const std::string& username) {
        liteim::UserRecord user;
        user.user_id = user_id;
        user.username = username;
        user.nickname = username;
        user.password_hash = "hash";
        user.password_salt = "salt";
        user.created_at_ms = 1000 + static_cast<std::int64_t>(user_id);
        users[user_id] = user;
    }

    std::unordered_map<std::uint64_t, liteim::UserRecord> users;
    std::unordered_map<std::uint64_t, std::vector<liteim::OfflineMessageRecord>> pending;
    std::vector<std::uint64_t> last_mark_message_ids;
    std::uint64_t last_get_user_id{0};
    std::uint64_t last_mark_user_id{0};
    int get_offline_calls{0};
    int mark_delivered_calls{0};
};

class FakeCache final : public liteim::ICache {
public:
    liteim::Status setUserOnline(const liteim::OnlineSession& session,
                                 std::chrono::seconds) override {
        online_users[session.user_id] = session;
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
        online = online_users.find(user_id) != online_users.end();
        return liteim::Status::ok();
    }

    liteim::Status getOnlineSession(std::uint64_t user_id,
                                    liteim::OnlineSession& session) override {
        const auto it = online_users.find(user_id);
        if (it == online_users.end()) {
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

    liteim::Status clearUnread(const liteim::UnreadKey& key) override {
        ++clear_unread_calls;
        cleared_unread_keys.push_back(key);
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

    std::unordered_map<std::uint64_t, liteim::OnlineSession> online_users;
    std::vector<liteim::UnreadKey> cleared_unread_keys;
    int clear_unread_calls{0};
};

class OfflineMessageServiceFixture : public ::testing::Test {
protected:
    OfflineMessageServiceFixture()
        : online(sessions, cache, "server-a", 30s), service(storage, cache, online) {
        storage.addUser(1001, "alice");
        storage.addUser(1002, "bob");
    }

    liteim::MessageRouter::RouterRequest requestFor(const liteim::Session::Ptr& session,
                                                    liteim::Bytes body) {
        liteim::TlvMap fields;
        EXPECT_TRUE(liteim::parseTlvMap(body, fields).isOk());
        return liteim::MessageRouter::RouterRequest{
            session, makePacket(liteim::MessageType::OfflineMessagesRequest, 37, std::move(body)),
            std::move(fields)};
    }

    liteim::Session::Ptr bindAlice(std::uint64_t session_id = 7001) {
        auto session = makeSession(loop, session_id);
        EXPECT_TRUE(online.bindUser(1001, session).isOk());
        return session;
    }

    liteim::EventLoop loop;
    FakeStorage storage;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService online;
    liteim::OfflineMessageService service;
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

std::string uniqueStep37Suffix() {
    static std::atomic<int> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(static_cast<long long>(::getpid())) + "_" +
           std::to_string(static_cast<long long>(ticks % 1000000000LL)) + "_" +
           std::to_string(counter.fetch_add(1));
}

void executeCleanupSql(liteim::MySqlConnection& connection, const std::string& sql) {
    liteim::PreparedStatement statement(connection);
    if (!statement.prepare(sql).isOk()) {
        return;
    }
    std::uint64_t affected_rows = 0;
    (void)statement.executeUpdate(affected_rows);
}

void cleanupStep37Rows(const liteim::MySqlConfig& config) {
    liteim::MySqlConnection connection;
    const auto status = connection.connect(config);
    if (!status.isOk()) {
        return;
    }

    executeCleanupSql(connection,
                      "DELETE FROM offline_messages "
                      "WHERE message_id IN ("
                      "SELECT message_id FROM messages WHERE message_text LIKE 'step37\\_%')");
    executeCleanupSql(connection, "DELETE FROM messages WHERE message_text LIKE 'step37\\_%'");
    executeCleanupSql(connection, "DELETE FROM users WHERE username LIKE 'step37\\_%'");
}

class OfflineMessageServiceIntegrationTest : public ::testing::Test {
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

        cleanupStep37Rows(mysql_config);
        mysql_pool = std::make_unique<liteim::MySqlPool>(mysql_config);
        ASSERT_TRUE(mysql_pool->start().isOk());
        redis_pool = std::make_unique<liteim::RedisPool>(redis_config);
        ASSERT_TRUE(redis_pool->start().isOk());
        storage = std::make_unique<liteim::MySqlStorage>(*mysql_pool);
        cache = std::make_unique<liteim::RedisCache>(*redis_pool);
        online = std::make_unique<liteim::OnlineService>(sessions, *cache, "server-step37", 30s);
        service = std::make_unique<liteim::OfflineMessageService>(*storage, *cache, *online);
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
        }
        if (mysql_pool) {
            mysql_pool->close();
        }
        cleanupStep37Rows(mysql_config);
    }

    liteim::MessageRouter::RouterRequest requestFor(const liteim::Session::Ptr& session,
                                                    liteim::Bytes body) {
        liteim::TlvMap fields;
        EXPECT_TRUE(liteim::parseTlvMap(body, fields).isOk());
        return liteim::MessageRouter::RouterRequest{
            session, makePacket(liteim::MessageType::OfflineMessagesRequest, 3701,
                                std::move(body)),
            std::move(fields)};
    }

    liteim::UserRecord createUser(const std::string& label) {
        const auto suffix = uniqueStep37Suffix();
        liteim::CreateUserRequest request;
        request.username = "step37_" + label + "_" + suffix;
        request.nickname = request.username;
        request.password_hash = "hash_" + suffix;
        request.password_salt = "salt_" + suffix;

        liteim::UserRecord user;
        const auto status = storage->createUser(request, user);
        EXPECT_TRUE(status.isOk()) << status.message();
        return user;
    }

    liteim::MySqlConfig mysql_config;
    liteim::RedisConfig redis_config;
    liteim::EventLoop loop;
    liteim::SessionManager sessions;
    std::unique_ptr<liteim::MySqlPool> mysql_pool;
    std::unique_ptr<liteim::RedisPool> redis_pool;
    std::unique_ptr<liteim::MySqlStorage> storage;
    std::unique_ptr<liteim::RedisCache> cache;
    std::unique_ptr<liteim::OnlineService> online;
    std::unique_ptr<liteim::OfflineMessageService> service;
    std::vector<std::uint64_t> online_user_ids;
};

}  // namespace

TEST(OfflineMessageServiceTest, HeaderIsSelfContained) {
    using Service = liteim::OfflineMessageService;
    using Options = liteim::OfflineMessageServiceOptions;

    static_assert(std::is_constructible_v<Service, liteim::IStorage&, liteim::ICache&,
                                          liteim::OnlineService&, Options>);
    static_assert(
        std::is_same_v<decltype(&Service::registerHandlers),
                       liteim::Status (Service::*)(liteim::MessageRouter&)>);
    static_assert(std::is_same_v<decltype(&Service::handleOfflineMessages),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
}

TEST_F(OfflineMessageServiceFixture, OfflineMessagesRequireLoggedInSession) {
    auto session = makeSession(loop, 7001);
    auto request = requestFor(session, offlineRequestBody());
    liteim::Packet response;

    const auto status = service.handleOfflineMessages(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.get_offline_calls, 0);
    EXPECT_EQ(storage.mark_delivered_calls, 0);
    EXPECT_EQ(cache.clear_unread_calls, 0);
}

TEST_F(OfflineMessageServiceFixture, OfflineMessagesReturnPendingRowsMarkDeliveredAndClearUnread) {
    auto session = bindAlice();
    storage.pending[1001] = {
        makeOfflineRecord(1, 1001, liteim::ConversationType::kPrivate, 10011002, 5001, 1002, 1001,
                          "hello alice", 1700000000000LL),
        makeOfflineRecord(2, 1001, liteim::ConversationType::kGroup, 88, 5002, 1002, 88,
                          "group hello", 1700000000001LL),
    };

    auto request = requestFor(session, offlineRequestBody());
    liteim::Packet response;
    const auto status = service.handleOfflineMessages(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::OfflineMessagesResponse);
    EXPECT_EQ(storage.get_offline_calls, 1);
    EXPECT_EQ(storage.last_get_user_id, 1001U);
    EXPECT_EQ(storage.mark_delivered_calls, 1);
    EXPECT_EQ(storage.last_mark_user_id, 1001U);
    EXPECT_EQ(storage.last_mark_message_ids, (std::vector<std::uint64_t>{5001, 5002}));
    EXPECT_EQ(cache.clear_unread_calls, 2);
    ASSERT_EQ(cache.cleared_unread_keys.size(), 2U);
    EXPECT_EQ(cache.cleared_unread_keys[0].user_id, 1001U);
    EXPECT_EQ(cache.cleared_unread_keys[0].conversation.id, 10011002U);
    EXPECT_EQ(cache.cleared_unread_keys[1].conversation.type, liteim::ConversationType::kGroup);
    EXPECT_EQ(cache.cleared_unread_keys[1].conversation.id, 88U);

    EXPECT_EQ(uint64Fields(response, liteim::TlvType::MessageId),
              (std::vector<std::uint64_t>{5001, 5002}));
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::ConversationType),
              (std::vector<std::uint64_t>{
                  static_cast<std::uint64_t>(liteim::ConversationType::kPrivate),
                  static_cast<std::uint64_t>(liteim::ConversationType::kGroup)}));
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::ConversationId),
              (std::vector<std::uint64_t>{10011002, 88}));
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::SenderId),
              (std::vector<std::uint64_t>{1002, 1002}));
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::ReceiverId),
              (std::vector<std::uint64_t>{1001, 88}));
    EXPECT_EQ(stringFields(response, liteim::TlvType::MessageText),
              (std::vector<std::string>{"hello alice", "group hello"}));
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::TimestampMs),
              (std::vector<std::uint64_t>{1700000000000ULL, 1700000000001ULL}));
}

TEST_F(OfflineMessageServiceFixture, OfflineMessagesLimitIsCappedByServiceOption) {
    liteim::OfflineMessageServiceOptions options;
    options.max_messages_per_pull = 2;
    liteim::OfflineMessageService limited_service(storage, cache, online, options);
    auto session = bindAlice();
    storage.pending[1001] = {
        makeOfflineRecord(1, 1001, liteim::ConversationType::kPrivate, 1, 5001, 1002, 1001,
                          "one", 1700000000000LL),
        makeOfflineRecord(2, 1001, liteim::ConversationType::kPrivate, 1, 5002, 1002, 1001,
                          "two", 1700000000001LL),
        makeOfflineRecord(3, 1001, liteim::ConversationType::kPrivate, 1, 5003, 1002, 1001,
                          "three", 1700000000002LL),
    };

    auto request = requestFor(session, offlineRequestBody(10));
    liteim::Packet response;
    const auto status = limited_service.handleOfflineMessages(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::MessageId),
              (std::vector<std::uint64_t>{5001, 5002}));
    EXPECT_EQ(storage.last_mark_message_ids, (std::vector<std::uint64_t>{5001, 5002}));
    EXPECT_EQ(cache.clear_unread_calls, 1);
}

TEST_F(OfflineMessageServiceFixture, OfflineMessagesRejectZeroLimit) {
    auto session = bindAlice();
    auto request = requestFor(session, offlineRequestBody(0));
    liteim::Packet response;

    const auto status = service.handleOfflineMessages(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.get_offline_calls, 0);
    EXPECT_EQ(storage.mark_delivered_calls, 0);
    EXPECT_EQ(cache.clear_unread_calls, 0);
}

TEST_F(OfflineMessageServiceIntegrationTest, PullMarksDeliveredAndClearsRedisUnread) {
    ASSERT_NE(service, nullptr);
    const auto sender = createUser("sender");
    const auto receiver = createUser("receiver");
    online_user_ids.push_back(receiver.user_id);

    const liteim::ConversationKey conversation{liteim::ConversationType::kPrivate,
                                               900000 + receiver.user_id};
    liteim::MessageRecord message;
    message.conversation = conversation;
    message.sender_id = sender.user_id;
    message.receiver_id = receiver.user_id;
    message.text = "step37_offline_" + uniqueStep37Suffix();
    message.created_at_ms = 1700000000000LL;

    liteim::MessageRecord saved_message;
    const auto save_status =
        storage->saveMessageWithOfflineRecipients(message, {receiver.user_id}, saved_message);
    ASSERT_TRUE(save_status.isOk()) << save_status.message();

    std::uint64_t unread_count = 0;
    ASSERT_TRUE(cache->incrUnread({receiver.user_id, conversation}, 1, unread_count).isOk());
    EXPECT_EQ(unread_count, 1U);

    auto session = makeSession(loop, 8001);
    ASSERT_TRUE(online->bindUser(receiver.user_id, session).isOk());
    auto request = requestFor(session, offlineRequestBody());
    liteim::Packet response;

    const auto pull_status = service->handleOfflineMessages(request, response);
    ASSERT_TRUE(pull_status.isOk()) << pull_status.message();

    EXPECT_EQ(response.header.msg_type, liteim::MessageType::OfflineMessagesResponse);
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::MessageId),
              (std::vector<std::uint64_t>{saved_message.message_id}));
    EXPECT_EQ(stringFields(response, liteim::TlvType::MessageText),
              (std::vector<std::string>{message.text}));

    std::vector<liteim::OfflineMessageRecord> pending;
    ASSERT_TRUE(storage->getOfflineMessages(receiver.user_id, pending).isOk());
    EXPECT_TRUE(pending.empty());

    unread_count = 99;
    ASSERT_TRUE(cache->getUnread({receiver.user_id, conversation}, unread_count).isOk());
    EXPECT_EQ(unread_count, 0U);
}
