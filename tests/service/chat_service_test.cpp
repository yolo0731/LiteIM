#include "liteim/service/ChatService.hpp"

#include "liteim/cache/ICache.hpp"
#include "liteim/concurrency/ThreadPool.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim/service/BotGateway.hpp"
#include "liteim/service/BotService.hpp"
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

liteim::Bytes privateMessageBody(std::uint64_t receiver_id, const std::string& text) {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::ReceiverId, receiver_id, body).isOk());
    EXPECT_TRUE(liteim::appendString(liteim::TlvType::MessageText, text, body).isOk());
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

        saved_message = message;
        saved_message.message_id = next_message_id++;
        saved_message.created_at_ms = next_created_at_ms++;
        saved_messages.push_back(saved_message);
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

    std::unordered_map<std::uint64_t, liteim::UserRecord> users;
    liteim::MessageRecord last_message;
    std::vector<std::uint64_t> last_offline_user_ids;
    std::vector<std::vector<std::uint64_t>> saved_offline_user_ids;
    std::vector<liteim::MessageRecord> saved_messages;
    std::uint64_t next_message_id{5001};
    std::int64_t next_created_at_ms{1700000000000LL};
    int save_message_calls{0};
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
                           bot_service(storage, cache, online, bot_gateway),
                           service(storage, cache, online),
                           service_with_bot(storage, cache, online, &bot_service) {
        storage.addUser(1001, "alice", "Alice");
        storage.addUser(1002, "bob", "Bob");
        storage.addUser(9001, "mira_bot", "Mira Bot");
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

    liteim::EventLoop loop;
    FakeStorage storage;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService online;
    liteim::EchoBotGateway bot_gateway;
    liteim::BotService bot_service;
    liteim::ChatService service;
    liteim::ChatService service_with_bot;
};

}  // namespace

TEST(ChatServiceTest, HeaderIsSelfContained) {
    using Service = liteim::ChatService;

    static_assert(std::is_constructible_v<Service, liteim::IStorage&, liteim::ICache&,
                                          liteim::OnlineService&>);
    static_assert(std::is_constructible_v<Service, liteim::IStorage&, liteim::ICache&,
                                          liteim::OnlineService&, liteim::BotService*>);
    static_assert(
        std::is_same_v<decltype(&Service::registerHandlers),
                       liteim::Status (Service::*)(liteim::MessageRouter&)>);
    static_assert(std::is_same_v<decltype(&Service::handlePrivateMessage),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
}

TEST_F(ChatServiceFixture, PrivateMessageToBotSavesOriginalWithoutBotOfflineUnreadAndPushesReply) {
    RunningSession sender(7001);
    ASSERT_TRUE(online.bindUser(1001, sender.session()).isOk());
    auto request = requestFor(sender.session(), liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(9001, "hello mira"));
    liteim::Packet response;

    const auto status = service_with_bot.handlePrivateMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::PrivateMessageResponse);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::MessageId), 5001U);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::SenderId), 1001U);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::ReceiverId), 9001U);
    EXPECT_EQ(stringField(response, liteim::TlvType::MessageText), "hello mira");

    ASSERT_EQ(storage.save_message_calls, 2);
    ASSERT_EQ(storage.saved_messages.size(), 2U);
    ASSERT_EQ(storage.saved_offline_user_ids.size(), 2U);
    EXPECT_TRUE(storage.saved_offline_user_ids[0].empty());
    EXPECT_TRUE(storage.saved_offline_user_ids[1].empty());
    EXPECT_EQ(cache.incr_unread_calls, 0);

    const auto& reply = storage.saved_messages[1];
    EXPECT_EQ(reply.conversation.type, liteim::ConversationType::kPrivate);
    EXPECT_EQ(reply.conversation.id, 10019001U);
    EXPECT_EQ(reply.sender_id, 9001U);
    EXPECT_EQ(reply.receiver_id, 1001U);
    EXPECT_EQ(reply.text, "Echo: hello mira");

    const auto push = readPacket(sender.peerFd(), 2s);
    ASSERT_TRUE(push.has_value());
    EXPECT_EQ(push->header.msg_type, liteim::MessageType::PrivateMessagePush);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::MessageId), 5002U);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::ConversationId), 10019001U);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::SenderId), 9001U);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::ReceiverId), 1001U);
    EXPECT_EQ(stringField(*push, liteim::TlvType::MessageText), "Echo: hello mira");
}

TEST_F(ChatServiceFixture, PrivateMessageToOnlineBotIsDeliveredAsNormalUserWithoutEchoFallback) {
    auto sender = bindAlice();
    RunningSession bot(7901);
    ASSERT_TRUE(online.bindUser(9001, bot.session()).isOk());

    auto request = requestFor(sender, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(9001, "hello online bot"));
    liteim::Packet response;

    const auto status = service_with_bot.handlePrivateMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::PrivateMessageResponse);
    EXPECT_EQ(storage.save_message_calls, 1);
    ASSERT_EQ(storage.saved_messages.size(), 1U);
    EXPECT_TRUE(storage.last_offline_user_ids.empty());
    EXPECT_EQ(cache.incr_unread_calls, 0);

    const auto push = readPacket(bot.peerFd(), 2s);
    ASSERT_TRUE(push.has_value());
    EXPECT_EQ(push->header.msg_type, liteim::MessageType::PrivateMessagePush);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::MessageId), 5001U);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::SenderId), 1001U);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::ReceiverId), 9001U);
    EXPECT_EQ(stringField(*push, liteim::TlvType::MessageText), "hello online bot");
}

TEST_F(ChatServiceFixture, PrivateMessageToNormalUserKeepsOfflineUnreadWhenBotEnabled) {
    auto session = bindAlice();
    auto request = requestFor(session, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(1002, "hello bob"));
    liteim::Packet response;

    const auto status = service_with_bot.handlePrivateMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(storage.save_message_calls, 1);
    ASSERT_EQ(storage.last_offline_user_ids.size(), 1U);
    EXPECT_EQ(storage.last_offline_user_ids.front(), 1002U);
    EXPECT_EQ(cache.incr_unread_calls, 1);
    EXPECT_EQ(cache.last_unread_key.user_id, 1002U);
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

TEST_F(ChatServiceFixture, OfflineUnreadFailureStillReturnsSenderSuccess) {
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

TEST_F(ChatServiceFixture, RegisteredHandlerSendsSenderResponseThroughRouter) {
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
