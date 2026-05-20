#include "liteim/service/HistoryService.hpp"

#include "liteim/cache/ICache.hpp"
#include "liteim/concurrency/ThreadPool.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim/service/MessageRouter.hpp"
#include "liteim/service/OnlineService.hpp"
#include "liteim/service/SessionManager.hpp"
#include "liteim/storage/IStorage.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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

liteim::Bytes historyBody(liteim::ConversationType type,
                          std::uint64_t conversation_id,
                          std::optional<std::uint64_t> before_message_id = std::nullopt,
                          std::optional<std::uint64_t> limit = std::nullopt) {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::ConversationType,
                                     static_cast<std::uint64_t>(type), body)
                    .isOk());
    EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::ConversationId, conversation_id, body).isOk());
    if (before_message_id.has_value()) {
        EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::MessageId, *before_message_id, body)
                        .isOk());
    }
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

liteim::Bytes readExactOrEmpty(int fd, std::size_t len, std::chrono::milliseconds timeout) {
    liteim::Bytes output(len);
    std::size_t read_bytes = 0;
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (read_bytes < len) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            output.resize(read_bytes);
            return output;
        }

        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int rc = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (rc == 0) {
            continue;
        }
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            output.resize(read_bytes);
            return output;
        }

        const auto n = ::read(fd, output.data() + read_bytes, len - read_bytes);
        if (n > 0) {
            read_bytes += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        output.resize(read_bytes);
        return output;
    }

    return output;
}

std::optional<liteim::Packet> readPacket(int fd, std::chrono::milliseconds timeout) {
    const auto header_bytes = readExactOrEmpty(fd, liteim::kPacketHeaderSize, timeout);
    if (header_bytes.size() != liteim::kPacketHeaderSize) {
        return std::nullopt;
    }

    liteim::PacketHeader header;
    const auto header_status = liteim::parseHeader(header_bytes.data(), header_bytes.size(), header);
    EXPECT_TRUE(header_status.isOk()) << header_status.message();
    if (!header_status.isOk()) {
        return std::nullopt;
    }

    liteim::Packet packet;
    packet.header = header;
    packet.body = readExactOrEmpty(fd, header.body_len, timeout);
    if (packet.body.size() != header.body_len) {
        return std::nullopt;
    }
    return packet;
}

class RunningSession {
public:
    explicit RunningSession(std::uint64_t session_id) {
        auto sockets = makeSocketPair();
        peer_ = std::move(sockets.peer);
        thread_ = std::thread([this, session_id, server = std::move(sockets.server)]() mutable {
            liteim::EventLoop loop;
            auto local_session =
                std::make_shared<liteim::Session>(&loop, std::move(server), session_id);
            local_session->start();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                loop_ = &loop;
                session_ = local_session;
            }
            ready_.notify_one();

            loop.loop();
            local_session->close();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                loop_ = nullptr;
            }
        });

        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this]() { return session_ != nullptr; });
    }

    RunningSession(const RunningSession&) = delete;
    RunningSession& operator=(const RunningSession&) = delete;

    ~RunningSession() {
        stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    liteim::Session::Ptr session() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return session_;
    }

    int peerFd() const noexcept {
        return peer_.fd();
    }

    void stop() noexcept {
        liteim::EventLoop* loop = nullptr;
        liteim::Session::Ptr session;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop = loop_;
            session = session_;
        }

        if (loop != nullptr) {
            loop->queueInLoop([loop, session]() {
                if (session != nullptr) {
                    session->close();
                }
                loop->quit();
            });
        }
    }

private:
    liteim::UniqueFd peer_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    liteim::EventLoop* loop_{nullptr};
    liteim::Session::Ptr session_;
    std::thread thread_;
};

liteim::MessageRecord makeMessage(liteim::ConversationType type,
                                  std::uint64_t conversation_id,
                                  std::uint64_t message_id,
                                  std::uint64_t sender_id,
                                  std::uint64_t receiver_id,
                                  const std::string& text) {
    liteim::MessageRecord message;
    message.message_id = message_id;
    message.conversation = {type, conversation_id};
    message.sender_id = sender_id;
    message.receiver_id = receiver_id;
    message.text = text;
    message.created_at_ms = 1800000000000LL + static_cast<std::int64_t>(message_id);
    return message;
}

class FakeStorage final : public liteim::IStorage {
public:
    liteim::Status createUser(const liteim::CreateUserRequest&, liteim::UserRecord&) override {
        return liteim::Status::ok();
    }

    liteim::Status findUserByUsername(const std::string&, liteim::UserRecord&) override {
        return liteim::Status::error(liteim::ErrorCode::NotFound, "user was not found");
    }

    liteim::Status findUserById(std::uint64_t, liteim::UserRecord&) override {
        return liteim::Status::ok();
    }

    liteim::Status findMessageByClientMessageId(std::uint64_t, const std::string&,
                                                liteim::MessageRecord&) override {
        return liteim::Status::error(liteim::ErrorCode::NotFound, "message was not found");
    }

    liteim::Status createFriendRequest(std::uint64_t, std::uint64_t,
                                       liteim::FriendRequestRecord& request) override {
        request = {};
        return liteim::Status::ok();
    }

    liteim::Status acceptFriendRequest(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status rejectFriendRequest(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status areFriends(std::uint64_t, std::uint64_t, bool& are_friends) override {
        are_friends = true;
        return liteim::Status::ok();
    }

    liteim::Status addFriendship(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status getFriends(std::uint64_t, std::vector<liteim::UserProfileRecord>&) override {
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

    liteim::Status getGroupMembers(std::uint64_t group_id,
                                   std::vector<liteim::GroupMemberRecord>& members) override {
        ++get_group_members_calls;
        last_get_group_members_id = group_id;
        const auto it = group_members.find(group_id);
        if (it == group_members.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "group was not found");
        }
        members = it->second;
        return liteim::Status::ok();
    }

    liteim::Status findGroupById(std::uint64_t group_id, liteim::GroupRecord& group) override {
        ++find_group_calls;
        last_find_group_id = group_id;
        const auto it = groups.find(group_id);
        if (it == groups.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "group was not found");
        }
        group = it->second;
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

    liteim::Status getHistory(const liteim::HistoryQuery& query,
                              std::vector<liteim::MessageRecord>& messages) override {
        ++get_history_calls;
        last_history_query = query;
        messages.clear();
        for (const auto& message : history_messages) {
            if (message.conversation.type != query.conversation.type ||
                message.conversation.id != query.conversation.id) {
                continue;
            }
            if (query.before_message_id != 0U &&
                message.message_id >= query.before_message_id) {
                continue;
            }
            messages.push_back(message);
        }
        std::sort(messages.begin(), messages.end(), [](const auto& left, const auto& right) {
            return left.message_id > right.message_id;
        });
        if (messages.size() > query.limit) {
            messages.resize(query.limit);
        }
        return liteim::Status::ok();
    }

    void addGroup(std::uint64_t group_id, std::uint64_t owner_id, const std::string& name) {
        groups[group_id] = liteim::GroupRecord{group_id, owner_id, name, 1800000000000LL};
        group_members[group_id].push_back(
            liteim::GroupMemberRecord{owner_id, "owner", "Owner", 1800000000001LL});
    }

    void addMember(std::uint64_t group_id, std::uint64_t user_id) {
        group_members[group_id].push_back(
            liteim::GroupMemberRecord{user_id, "user", "User", 1800000000002LL});
    }

    std::unordered_map<std::uint64_t, liteim::GroupRecord> groups;
    std::unordered_map<std::uint64_t, std::vector<liteim::GroupMemberRecord>> group_members;
    std::vector<liteim::MessageRecord> history_messages;
    liteim::HistoryQuery last_history_query;
    std::uint64_t last_find_group_id{0};
    std::uint64_t last_get_group_members_id{0};
    int find_group_calls{0};
    int get_group_members_calls{0};
    int get_history_calls{0};
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

    std::unordered_map<std::uint64_t, liteim::OnlineSession> online_users;
};

class HistoryServiceFixture : public ::testing::Test {
protected:
    HistoryServiceFixture() : online(sessions, cache, "server-a", 30s),
                              service(storage, online) {}

    liteim::MessageRouter::RouterRequest requestFor(const liteim::Session::Ptr& session,
                                                    liteim::Bytes body) {
        liteim::TlvMap fields;
        EXPECT_TRUE(liteim::parseTlvMap(body, fields).isOk());
        return liteim::MessageRouter::RouterRequest{
            session, makePacket(liteim::MessageType::HistoryRequest, 39, std::move(body)),
            std::move(fields)};
    }

    liteim::Session::Ptr bindUser(std::uint64_t user_id, std::uint64_t session_id) {
        auto session = makeSession(loop, session_id);
        EXPECT_TRUE(online.bindUser(user_id, session).isOk());
        return session;
    }

    liteim::Session::Ptr bindAlice() {
        return bindUser(1001, 7001);
    }

    liteim::EventLoop loop;
    FakeStorage storage;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService online;
    liteim::HistoryService service;
};

}  // namespace

TEST(HistoryServiceTest, HeaderIsSelfContained) {
    using Service = liteim::HistoryService;
    using Options = liteim::HistoryServiceOptions;

    static_assert(std::is_constructible_v<Service, liteim::IStorage&, liteim::OnlineService&>);
    static_assert(std::is_constructible_v<Service, liteim::IStorage&, liteim::OnlineService&,
                                          Options>);
    static_assert(
        std::is_same_v<decltype(&Service::registerHandlers),
                       liteim::Status (Service::*)(liteim::MessageRouter&)>);
    static_assert(std::is_same_v<decltype(&Service::handleHistory),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
    static_assert(std::is_same_v<decltype(&Service::options),
                                 const Options& (Service::*)() const noexcept>);
}

TEST_F(HistoryServiceFixture, HistoryRequiresLoggedInSession) {
    auto session = makeSession(loop, 7001);
    auto request =
        requestFor(session, historyBody(liteim::ConversationType::kPrivate, 10011002));
    liteim::Packet response;

    const auto status = service.handleHistory(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.get_history_calls, 0);
}

TEST_F(HistoryServiceFixture, PrivateHistoryUsesDefaultLimitAndReturnsRecentMessages) {
    storage.history_messages.push_back(
        makeMessage(liteim::ConversationType::kPrivate, 10011002, 5001, 1001, 1002, "one"));
    storage.history_messages.push_back(
        makeMessage(liteim::ConversationType::kPrivate, 10011002, 5002, 1002, 1001, "two"));
    auto session = bindAlice();
    auto request =
        requestFor(session, historyBody(liteim::ConversationType::kPrivate, 10011002));
    liteim::Packet response;

    const auto status = service.handleHistory(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::HistoryResponse);
    EXPECT_EQ(response.header.seq_id, 39U);
    EXPECT_EQ(storage.get_history_calls, 1);
    EXPECT_EQ(storage.last_history_query.conversation.type, liteim::ConversationType::kPrivate);
    EXPECT_EQ(storage.last_history_query.conversation.id, 10011002U);
    EXPECT_EQ(storage.last_history_query.before_message_id, 0U);
    EXPECT_EQ(storage.last_history_query.limit, 20U);

    EXPECT_EQ(uint64Fields(response, liteim::TlvType::MessageId),
              (std::vector<std::uint64_t>{5002, 5001}));
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::SenderId),
              (std::vector<std::uint64_t>{1002, 1001}));
    EXPECT_EQ(stringFields(response, liteim::TlvType::MessageText),
              (std::vector<std::string>{"two", "one"}));
}

TEST_F(HistoryServiceFixture, LimitAboveMaxIsCappedAndBeforeCursorIsForwarded) {
    storage.history_messages.push_back(
        makeMessage(liteim::ConversationType::kPrivate, 10011002, 5001, 1001, 1002, "old"));
    storage.history_messages.push_back(
        makeMessage(liteim::ConversationType::kPrivate, 10011002, 5002, 1002, 1001, "middle"));
    storage.history_messages.push_back(
        makeMessage(liteim::ConversationType::kPrivate, 10011002, 5003, 1001, 1002, "new"));
    auto session = bindAlice();
    auto request =
        requestFor(session, historyBody(liteim::ConversationType::kPrivate, 10011002, 5003, 99));
    liteim::Packet response;

    const auto status = service.handleHistory(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(storage.last_history_query.before_message_id, 5003U);
    EXPECT_EQ(storage.last_history_query.limit, 50U);
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::MessageId),
              (std::vector<std::uint64_t>{5002, 5001}));
}

TEST_F(HistoryServiceFixture, LimitZeroIsRejected) {
    auto session = bindAlice();
    auto request =
        requestFor(session, historyBody(liteim::ConversationType::kPrivate, 10011002, std::nullopt,
                                        0));
    liteim::Packet response;

    const auto status = service.handleHistory(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.get_history_calls, 0);
}

TEST_F(HistoryServiceFixture, PrivateHistoryRejectsNonParticipant) {
    auto session = bindAlice();
    auto request =
        requestFor(session, historyBody(liteim::ConversationType::kPrivate, 10021003));
    liteim::Packet response;

    const auto status = service.handleHistory(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.get_history_calls, 0);
}

TEST_F(HistoryServiceFixture, GroupHistoryRejectsNonMember) {
    storage.addGroup(88, 1001, "team");
    auto session = bindUser(1002, 7002);
    auto request = requestFor(session, historyBody(liteim::ConversationType::kGroup, 88));
    liteim::Packet response;

    const auto status = service.handleHistory(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.find_group_calls, 1);
    EXPECT_EQ(storage.get_group_members_calls, 1);
    EXPECT_EQ(storage.get_history_calls, 0);
}

TEST_F(HistoryServiceFixture, GroupHistoryReturnsMessagesForMember) {
    storage.addGroup(88, 1001, "team");
    storage.addMember(88, 1002);
    storage.history_messages.push_back(
        makeMessage(liteim::ConversationType::kGroup, 88, 9001, 1001, 88, "hello"));
    storage.history_messages.push_back(
        makeMessage(liteim::ConversationType::kGroup, 88, 9002, 1002, 88, "reply"));
    auto session = bindUser(1002, 7002);
    auto request = requestFor(session, historyBody(liteim::ConversationType::kGroup, 88));
    liteim::Packet response;

    const auto status = service.handleHistory(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::HistoryResponse);
    EXPECT_EQ(storage.find_group_calls, 1);
    EXPECT_EQ(storage.last_find_group_id, 88U);
    EXPECT_EQ(storage.get_group_members_calls, 1);
    EXPECT_EQ(storage.last_get_group_members_id, 88U);
    EXPECT_EQ(storage.get_history_calls, 1);
    EXPECT_EQ(storage.last_history_query.conversation.type, liteim::ConversationType::kGroup);
    EXPECT_EQ(storage.last_history_query.conversation.id, 88U);
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::MessageId),
              (std::vector<std::uint64_t>{9002, 9001}));
    EXPECT_EQ(uint64Fields(response, liteim::TlvType::ReceiverId),
              (std::vector<std::uint64_t>{88, 88}));
    EXPECT_EQ(stringFields(response, liteim::TlvType::MessageText),
              (std::vector<std::string>{"reply", "hello"}));
}

TEST_F(HistoryServiceFixture, RegisteredHandlerSendsHistoryResponseThroughRouter) {
    storage.history_messages.push_back(
        makeMessage(liteim::ConversationType::kPrivate, 10011002, 5001, 1001, 1002, "router"));
    RunningSession sender(7001);
    ASSERT_TRUE(online.bindUser(1001, sender.session()).isOk());

    liteim::ThreadPool pool(1);
    ASSERT_TRUE(pool.start().isOk());
    liteim::MessageRouter router(pool);
    ASSERT_TRUE(service.registerHandlers(router).isOk());

    router.route(sender.session(),
                 makePacket(liteim::MessageType::HistoryRequest, 3901,
                            historyBody(liteim::ConversationType::kPrivate, 10011002)));

    const auto response = readPacket(sender.peerFd(), 2s);
    pool.stop();

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->header.msg_type, liteim::MessageType::HistoryResponse);
    EXPECT_EQ(response->header.seq_id, 3901U);
    EXPECT_EQ(uint64Fields(*response, liteim::TlvType::MessageId),
              (std::vector<std::uint64_t>{5001}));
    EXPECT_EQ(stringFields(*response, liteim::TlvType::MessageText),
              (std::vector<std::string>{"router"}));
}
