#include "liteim/service/ChatService.hpp"

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

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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

liteim::Bytes privateMessageBody(std::uint64_t receiver_id, const std::string& text,
                                 const std::string& client_message_id = "") {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::ReceiverId, receiver_id, body).isOk());
    EXPECT_TRUE(liteim::appendString(liteim::TlvType::MessageText, text, body).isOk());
    if (!client_message_id.empty()) {
        EXPECT_TRUE(liteim::appendString(liteim::TlvType::ClientMessageId, client_message_id, body)
                        .isOk());
    }
    return body;
}

liteim::Bytes deliveryAckBody(std::uint64_t message_id) {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::MessageId, message_id, body).isOk());
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

    liteim::Status findMessageByClientMessageId(std::uint64_t sender_id,
                                                const std::string& client_message_id,
                                                liteim::MessageRecord& message) override {
        ++find_client_message_calls;
        last_find_sender_id = sender_id;
        last_find_client_message_id = client_message_id;
        const auto it = client_messages.find(clientKey(sender_id, client_message_id));
        if (it == client_messages.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound,
                                         "client message id was not found");
        }
        message = it->second;
        return liteim::Status::ok();
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

    liteim::Status areFriends(std::uint64_t user_id, std::uint64_t friend_id,
                              bool& are_friends) override {
        ++are_friends_calls;
        last_are_friends_user_id = user_id;
        last_are_friends_friend_id = friend_id;
        are_friends = friendships.count(friendKey(user_id, friend_id)) != 0U;
        return liteim::Status::ok();
    }

    liteim::Status ackPrivateMessageDelivery(std::uint64_t user_id, std::uint64_t message_id,
                                             liteim::MessageRecord& message) override {
        ++ack_private_delivery_calls;
        last_ack_user_id = user_id;
        last_ack_message_id = message_id;
        const auto it = messages_by_id.find(message_id);
        if (it == messages_by_id.end() || it->second.receiver_id != user_id ||
            it->second.conversation.type != liteim::ConversationType::kPrivate) {
            return liteim::Status::error(liteim::ErrorCode::NotFound,
                                         "private message delivery target was not found");
        }
        delivered_messages.insert(message_id);
        message = it->second;
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
    saveMessageWithOfflineRecipients(const liteim::MessageRecord& message,
                                     const std::vector<std::uint64_t>& offline_user_ids,
                                     liteim::MessageRecord& saved_message) override {
        ++save_message_calls;
        last_message = message;
        last_offline_user_ids = offline_user_ids;
        saved_offline_user_ids.push_back(offline_user_ids);

        if (!message.client_msg_id.empty()) {
            const auto key = clientKey(message.sender_id, message.client_msg_id);
            if (client_messages.find(key) != client_messages.end()) {
                return liteim::Status::error(liteim::ErrorCode::AlreadyExists,
                                             "client message id already exists");
            }
        }

        saved_message = message;
        saved_message.message_id = next_message_id++;
        saved_message.created_at_ms = next_created_at_ms++;
        saved_messages.push_back(saved_message);
        messages_by_id[saved_message.message_id] = saved_message;
        if (!saved_message.client_msg_id.empty()) {
            client_messages[clientKey(saved_message.sender_id, saved_message.client_msg_id)] =
                saved_message;
        }
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
        friendships.insert(friendKey(user_id, friend_id));
        friendships.insert(friendKey(friend_id, user_id));
    }

    void addPrivateMessage(std::uint64_t message_id, std::uint64_t sender_id,
                           std::uint64_t receiver_id) {
        liteim::MessageRecord message;
        message.message_id = message_id;
        message.conversation = {liteim::ConversationType::kPrivate, 10011002};
        message.sender_id = sender_id;
        message.receiver_id = receiver_id;
        message.text = "ack target";
        message.created_at_ms = 1700000000999LL;
        messages_by_id[message_id] = message;
    }

    static std::string clientKey(std::uint64_t sender_id, const std::string& client_message_id) {
        return std::to_string(sender_id) + ":" + client_message_id;
    }

    static std::string friendKey(std::uint64_t user_id, std::uint64_t friend_id) {
        return std::to_string(user_id) + ":" + std::to_string(friend_id);
    }

    std::unordered_map<std::uint64_t, liteim::UserRecord> users;
    std::unordered_map<std::uint64_t, liteim::MessageRecord> messages_by_id;
    std::unordered_map<std::string, liteim::MessageRecord> client_messages;
    std::unordered_set<std::string> friendships;
    std::unordered_set<std::uint64_t> delivered_messages;
    liteim::MessageRecord last_message;
    std::vector<std::uint64_t> last_offline_user_ids;
    std::vector<std::vector<std::uint64_t>> saved_offline_user_ids;
    std::vector<liteim::MessageRecord> saved_messages;
    std::string last_find_client_message_id;
    std::uint64_t last_find_sender_id{0};
    std::uint64_t last_are_friends_user_id{0};
    std::uint64_t last_are_friends_friend_id{0};
    std::uint64_t last_ack_user_id{0};
    std::uint64_t last_ack_message_id{0};
    std::uint64_t next_message_id{5001};
    std::int64_t next_created_at_ms{1700000000000LL};
    int save_message_calls{0};
    int find_client_message_calls{0};
    int are_friends_calls{0};
    int ack_private_delivery_calls{0};
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

    liteim::Status incrUnread(const liteim::UnreadKey& key, std::uint64_t delta,
                              std::uint64_t& unread_count) override {
        ++incr_unread_calls;
        last_unread_key = key;
        last_unread_delta = delta;
        unread_keys.push_back(key);
        if (fail_incr_unread) {
            return liteim::Status::error(liteim::ErrorCode::IoError, "redis unread failed");
        }
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
    liteim::UnreadKey last_unread_key;
    std::vector<liteim::UnreadKey> unread_keys;
    std::uint64_t last_unread_delta{0};
    int incr_unread_calls{0};
    bool fail_incr_unread{false};
};

class ChatServiceFixture : public ::testing::Test {
protected:
    ChatServiceFixture() : online(sessions, cache, "server-a", 30s),
                           service(storage, cache, online) {
        storage.addUser(1001, "alice", "Alice");
        storage.addUser(1002, "bob", "Bob");
    }

    liteim::MessageRouter::RouterRequest requestFor(const liteim::Session::Ptr& session,
                                                    liteim::MessageType type,
                                                    liteim::Bytes body) {
        liteim::TlvMap fields;
        EXPECT_TRUE(liteim::parseTlvMap(body, fields).isOk());
        return liteim::MessageRouter::RouterRequest{
            session, makePacket(type, 36, std::move(body)), std::move(fields)};
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
    liteim::ChatService service;
};

}  // namespace

TEST(ChatServiceTest, HeaderIsSelfContained) {
    using Service = liteim::ChatService;

    static_assert(std::is_constructible_v<Service, liteim::IStorage&, liteim::ICache&,
                                          liteim::OnlineService&>);
    static_assert(
        std::is_same_v<decltype(&Service::registerHandlers),
                       liteim::Status (Service::*)(liteim::MessageRouter&)>);
    static_assert(std::is_same_v<decltype(&Service::handlePrivateMessage),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
    static_assert(std::is_same_v<decltype(&Service::handleDeliveryAck),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
}

TEST_F(ChatServiceFixture, PrivateMessageToOfflineUserRecordsOfflineUnread) {
    storage.addExistingFriendship(1001, 1002);
    auto session = bindAlice();
    auto request = requestFor(session, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(1002, "hello bob"));
    liteim::Packet response;

    const auto status = service.handlePrivateMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(storage.save_message_calls, 1);
    ASSERT_EQ(storage.last_offline_user_ids.size(), 1U);
    EXPECT_EQ(storage.last_offline_user_ids.front(), 1002U);
    EXPECT_EQ(cache.incr_unread_calls, 1);
    EXPECT_EQ(cache.last_unread_key.user_id, 1002U);
}

TEST_F(ChatServiceFixture, PrivateMessageRequiresAcceptedFriendship) {
    auto session = bindAlice();
    auto request = requestFor(session, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(1002, "hello stranger"));
    liteim::Packet response;

    const auto status = service.handlePrivateMessage(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.are_friends_calls, 1);
    EXPECT_EQ(storage.last_are_friends_user_id, 1001U);
    EXPECT_EQ(storage.last_are_friends_friend_id, 1002U);
    EXPECT_EQ(storage.save_message_calls, 0);
    EXPECT_EQ(cache.incr_unread_calls, 0);
}

TEST_F(ChatServiceFixture, PrivateMessageRequiresLoggedInSession) {
    auto session = makeSession(loop, 7001);
    auto request = requestFor(session, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(1002, "hello bob"));
    liteim::Packet response;

    const auto status = service.handlePrivateMessage(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.save_message_calls, 0);
    EXPECT_EQ(cache.incr_unread_calls, 0);
}

TEST_F(ChatServiceFixture, PrivateMessageRequiresExistingReceiver) {
    auto session = bindAlice();
    auto request = requestFor(session, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(9999, "hello missing"));
    liteim::Packet response;

    const auto status = service.handlePrivateMessage(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::NotFound);
    EXPECT_EQ(storage.save_message_calls, 0);
    EXPECT_EQ(cache.incr_unread_calls, 0);
}

TEST_F(ChatServiceFixture, PrivateMessageRejectsTextAboveServiceLimit) {
    auto session = bindAlice();
    auto request = requestFor(session, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(1002, std::string(8193, 'x')));
    liteim::Packet response;

    const auto status = service.handlePrivateMessage(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.save_message_calls, 0);
    EXPECT_EQ(cache.incr_unread_calls, 0);
}

TEST_F(ChatServiceFixture, OfflineReceiverSavesMessageAndIncrementsUnread) {
    storage.addExistingFriendship(1001, 1002);
    auto session = bindAlice();
    auto request = requestFor(session, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(1002, "hello bob"));
    liteim::Packet response;

    const auto status = service.handlePrivateMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::PrivateMessageResponse);
    EXPECT_EQ(storage.save_message_calls, 1);
    EXPECT_EQ(storage.last_message.conversation.type, liteim::ConversationType::kPrivate);
    EXPECT_EQ(storage.last_message.conversation.id, 10011002U);
    EXPECT_EQ(storage.last_message.sender_id, 1001U);
    EXPECT_EQ(storage.last_message.receiver_id, 1002U);
    EXPECT_EQ(storage.last_message.text, "hello bob");
    ASSERT_EQ(storage.last_offline_user_ids.size(), 1U);
    EXPECT_EQ(storage.last_offline_user_ids.front(), 1002U);

    EXPECT_EQ(cache.incr_unread_calls, 1);
    EXPECT_EQ(cache.last_unread_key.user_id, 1002U);
    EXPECT_EQ(cache.last_unread_key.conversation.type, liteim::ConversationType::kPrivate);
    EXPECT_EQ(cache.last_unread_key.conversation.id, 10011002U);
    EXPECT_EQ(cache.last_unread_delta, 1U);

    EXPECT_EQ(uint64Field(response, liteim::TlvType::MessageId), 5001U);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::ConversationType),
              static_cast<std::uint64_t>(liteim::ConversationType::kPrivate));
    EXPECT_EQ(uint64Field(response, liteim::TlvType::ConversationId), 10011002U);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::SenderId), 1001U);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::ReceiverId), 1002U);
    EXPECT_EQ(stringField(response, liteim::TlvType::MessageText), "hello bob");
    EXPECT_EQ(uint64Field(response, liteim::TlvType::TimestampMs), 1700000000000ULL);
}

TEST_F(ChatServiceFixture, DuplicateClientMessageIdReturnsExistingMessageWithoutSecondUnread) {
    storage.addExistingFriendship(1001, 1002);
    auto session = bindAlice();
    auto first_request =
        requestFor(session, liteim::MessageType::PrivateMessageRequest,
                   privateMessageBody(1002, "hello once", "client-msg-1"));
    liteim::Packet first_response;

    const auto first_status = service.handlePrivateMessage(first_request, first_response);

    ASSERT_TRUE(first_status.isOk()) << first_status.message();
    EXPECT_EQ(uint64Field(first_response, liteim::TlvType::MessageId), 5001U);
    EXPECT_EQ(stringField(first_response, liteim::TlvType::ClientMessageId), "client-msg-1");

    auto duplicate_request =
        requestFor(session, liteim::MessageType::PrivateMessageRequest,
                   privateMessageBody(1002, "hello once", "client-msg-1"));
    liteim::Packet duplicate_response;

    const auto duplicate_status =
        service.handlePrivateMessage(duplicate_request, duplicate_response);

    ASSERT_TRUE(duplicate_status.isOk()) << duplicate_status.message();
    EXPECT_EQ(storage.save_message_calls, 2);
    EXPECT_EQ(storage.find_client_message_calls, 1);
    EXPECT_EQ(storage.last_find_sender_id, 1001U);
    EXPECT_EQ(storage.last_find_client_message_id, "client-msg-1");
    ASSERT_EQ(storage.saved_messages.size(), 1U);
    EXPECT_EQ(cache.incr_unread_calls, 1);
    EXPECT_EQ(uint64Field(duplicate_response, liteim::TlvType::MessageId), 5001U);
    EXPECT_EQ(stringField(duplicate_response, liteim::TlvType::ClientMessageId), "client-msg-1");
}

TEST_F(ChatServiceFixture, OfflineUnreadFailureStillReturnsSenderSuccess) {
    storage.addExistingFriendship(1001, 1002);
    cache.fail_incr_unread = true;
    auto session = bindAlice();
    auto request = requestFor(session, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(1002, "hello despite redis"));
    liteim::Packet response;

    const auto status = service.handlePrivateMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::PrivateMessageResponse);
    EXPECT_EQ(storage.save_message_calls, 1);
    ASSERT_EQ(storage.last_offline_user_ids.size(), 1U);
    EXPECT_EQ(storage.last_offline_user_ids.front(), 1002U);
    EXPECT_EQ(cache.incr_unread_calls, 1);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::MessageId), 5001U);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::ReceiverId), 1002U);
    EXPECT_EQ(stringField(response, liteim::TlvType::MessageText), "hello despite redis");
}

TEST_F(ChatServiceFixture, OnlineReceiverGetsPushWithoutOfflineUnread) {
    storage.addExistingFriendship(1001, 1002);
    auto sender = bindAlice();
    RunningSession receiver(7002);
    ASSERT_TRUE(online.bindUser(1002, receiver.session()).isOk());

    auto request = requestFor(sender, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(1002, "online hello"));
    liteim::Packet response;

    const auto status = service.handlePrivateMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::PrivateMessageResponse);
    EXPECT_EQ(storage.save_message_calls, 1);
    EXPECT_TRUE(storage.last_offline_user_ids.empty());
    EXPECT_EQ(cache.incr_unread_calls, 0);

    const auto push = readPacket(receiver.peerFd(), 2s);
    ASSERT_TRUE(push.has_value());
    EXPECT_EQ(push->header.msg_type, liteim::MessageType::PrivateMessagePush);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::MessageId), 5001U);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::ConversationType),
              static_cast<std::uint64_t>(liteim::ConversationType::kPrivate));
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::ConversationId), 10011002U);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::SenderId), 1001U);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::ReceiverId), 1002U);
    EXPECT_EQ(stringField(*push, liteim::TlvType::MessageText), "online hello");
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::TimestampMs), 1700000000000ULL);
}

TEST_F(ChatServiceFixture, DeliveryAckMarksPrivateMessageDeliveredForReceiver) {
    storage.addPrivateMessage(5001, 1001, 1002);
    auto bob = bindBob();
    auto request = requestFor(bob, liteim::MessageType::DeliveryAckRequest, deliveryAckBody(5001));
    liteim::Packet response;

    const auto status = service.handleDeliveryAck(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(storage.ack_private_delivery_calls, 1);
    EXPECT_EQ(storage.last_ack_user_id, 1002U);
    EXPECT_EQ(storage.last_ack_message_id, 5001U);
    EXPECT_EQ(storage.delivered_messages.count(5001), 1U);
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::DeliveryAckResponse);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::MessageId), 5001U);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::DeliveryStatus),
              static_cast<std::uint64_t>(liteim::DeliveryStatus::kDelivered));
}

TEST_F(ChatServiceFixture, DeliveryAckRejectsNonReceiver) {
    storage.addPrivateMessage(5001, 1001, 1002);
    auto alice = bindAlice();
    auto request =
        requestFor(alice, liteim::MessageType::DeliveryAckRequest, deliveryAckBody(5001));
    liteim::Packet response;

    const auto status = service.handleDeliveryAck(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::NotFound);
    EXPECT_EQ(storage.ack_private_delivery_calls, 1);
    EXPECT_TRUE(storage.delivered_messages.empty());
}

TEST_F(ChatServiceFixture, DeliveryAckIsIdempotentForReceiver) {
    storage.addPrivateMessage(5001, 1001, 1002);
    auto bob = bindBob();
    auto first_request =
        requestFor(bob, liteim::MessageType::DeliveryAckRequest, deliveryAckBody(5001));
    liteim::Packet first_response;
    ASSERT_TRUE(service.handleDeliveryAck(first_request, first_response).isOk());

    auto duplicate_request =
        requestFor(bob, liteim::MessageType::DeliveryAckRequest, deliveryAckBody(5001));
    liteim::Packet duplicate_response;
    const auto duplicate_status = service.handleDeliveryAck(duplicate_request, duplicate_response);

    ASSERT_TRUE(duplicate_status.isOk()) << duplicate_status.message();
    EXPECT_EQ(storage.ack_private_delivery_calls, 2);
    EXPECT_EQ(storage.delivered_messages.count(5001), 1U);
    EXPECT_EQ(uint64Field(duplicate_response, liteim::TlvType::MessageId), 5001U);
    EXPECT_EQ(uint64Field(duplicate_response, liteim::TlvType::DeliveryStatus),
              static_cast<std::uint64_t>(liteim::DeliveryStatus::kDelivered));
}

TEST_F(ChatServiceFixture, RegisteredHandlerSendsSenderResponseThroughRouter) {
    storage.addExistingFriendship(1001, 1002);
    RunningSession sender(7001);
    ASSERT_TRUE(online.bindUser(1001, sender.session()).isOk());

    liteim::ThreadPool pool(1);
    ASSERT_TRUE(pool.start().isOk());
    liteim::MessageRouter router(pool);
    ASSERT_TRUE(service.registerHandlers(router).isOk());

    router.route(sender.session(),
                 makePacket(liteim::MessageType::PrivateMessageRequest, 3601,
                            privateMessageBody(1002, "through router")));

    const auto response = readPacket(sender.peerFd(), 2s);
    pool.stop();

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->header.msg_type, liteim::MessageType::PrivateMessageResponse);
    EXPECT_EQ(response->header.seq_id, 3601U);
    EXPECT_EQ(uint64Field(*response, liteim::TlvType::MessageId), 5001U);
    EXPECT_EQ(uint64Field(*response, liteim::TlvType::SenderId), 1001U);
    EXPECT_EQ(uint64Field(*response, liteim::TlvType::ReceiverId), 1002U);
    EXPECT_EQ(stringField(*response, liteim::TlvType::MessageText), "through router");
}
