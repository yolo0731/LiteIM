#include "liteim/service/AuthService.hpp"
#include "liteim/service/ChatService.hpp"
#include "liteim/service/GroupService.hpp"
#include "liteim/service/HistoryService.hpp"

#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim/service/OnlineService.hpp"
#include "liteim/service/SessionManager.hpp"
#include "mocks/MockCache.hpp"
#include "mocks/MockStorage.hpp"

#include <openssl/evp.h>
#include <sys/socket.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;
using ::testing::_;
using ::testing::AllOf;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::Truly;

constexpr int kPbkdf2Iterations = 10000;
constexpr std::size_t kPasswordHashBytes = 32;

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

std::shared_ptr<liteim::Session> makeSession(liteim::EventLoop& loop,
                                             std::uint64_t session_id) {
    auto sockets = makeSocketPair();
    return std::make_shared<liteim::Session>(&loop, std::move(sockets.server), session_id);
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

liteim::Packet makePacket(liteim::MessageType type, std::uint64_t seq_id,
                          liteim::Bytes body) {
    liteim::Packet packet;
    packet.header.msg_type = type;
    packet.header.seq_id = seq_id;
    packet.body = std::move(body);
    return packet;
}

liteim::Bytes authBody(const std::string& username, const std::string& password) {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendString(liteim::TlvType::Username, username, body).isOk());
    EXPECT_TRUE(liteim::appendString(liteim::TlvType::Password, password, body).isOk());
    return body;
}

liteim::Bytes privateMessageBody(std::uint64_t receiver_id, const std::string& text) {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::ReceiverId, receiver_id, body).isOk());
    EXPECT_TRUE(liteim::appendString(liteim::TlvType::MessageText, text, body).isOk());
    return body;
}

liteim::Bytes groupMessageBody(std::uint64_t group_id, const std::string& text) {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::GroupId, group_id, body).isOk());
    EXPECT_TRUE(liteim::appendString(liteim::TlvType::MessageText, text, body).isOk());
    return body;
}

liteim::Bytes historyBody(liteim::ConversationType type,
                          std::uint64_t conversation_id,
                          std::optional<std::uint64_t> before_message_id,
                          std::optional<std::uint64_t> limit) {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::ConversationType,
                                     static_cast<std::uint64_t>(type), body)
                    .isOk());
    EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::ConversationId, conversation_id, body)
                    .isOk());
    if (before_message_id.has_value()) {
        EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::MessageId, *before_message_id, body)
                        .isOk());
    }
    if (limit.has_value()) {
        EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::Limit, *limit, body).isOk());
    }
    return body;
}

liteim::MessageRouter::RouterRequest requestFor(const liteim::Session::Ptr& session,
                                                liteim::MessageType type,
                                                liteim::Bytes body,
                                                std::uint64_t seq_id = 45) {
    liteim::TlvMap fields;
    EXPECT_TRUE(liteim::parseTlvMap(body, fields).isOk());
    return liteim::MessageRouter::RouterRequest{
        session, makePacket(type, seq_id, std::move(body)), std::move(fields)};
}

std::string toHex(const unsigned char* data, std::size_t len) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string output;
    output.resize(len * 2U);
    for (std::size_t i = 0; i < len; ++i) {
        output[i * 2U] = kHex[(data[i] >> 4U) & 0x0FU];
        output[i * 2U + 1U] = kHex[data[i] & 0x0FU];
    }
    return output;
}

std::string passwordHashForTest(const std::string& password, const std::string& salt) {
    std::array<unsigned char, kPasswordHashBytes> bytes{};
    const auto rc = PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                                      reinterpret_cast<const unsigned char*>(salt.data()),
                                      static_cast<int>(salt.size()), kPbkdf2Iterations,
                                      EVP_sha256(), static_cast<int>(bytes.size()), bytes.data());
    EXPECT_EQ(rc, 1);
    return toHex(bytes.data(), bytes.size());
}

liteim::UserRecord userWithPassword(std::uint64_t user_id, const std::string& username,
                                    const std::string& password) {
    liteim::UserRecord user;
    user.user_id = user_id;
    user.username = username;
    user.nickname = username + "_nick";
    user.password_salt = "test-salt-" + username;
    user.password_hash = passwordHashForTest(password, user.password_salt);
    user.created_at_ms = 1700000000000LL;
    return user;
}

liteim::MessageRecord savedMessageFrom(const liteim::MessageRecord& message,
                                       std::uint64_t message_id) {
    auto saved = message;
    saved.message_id = message_id;
    saved.created_at_ms = 1800000000000LL + static_cast<std::int64_t>(message_id);
    return saved;
}

auto loginKeyIs(const std::string& username, const std::string& remote_ip) {
    return AllOf(Field(&liteim::LoginAttemptKey::username, username),
                 Field(&liteim::LoginAttemptKey::remote_ip, remote_ip));
}

auto onlineSessionIs(std::uint64_t user_id, std::uint64_t session_id,
                     const std::string& server_id) {
    return AllOf(Field(&liteim::OnlineSession::user_id, user_id),
                 Field(&liteim::OnlineSession::session_id, session_id),
                 Field(&liteim::OnlineSession::server_id, server_id));
}

auto unreadKeyIs(std::uint64_t user_id, liteim::ConversationType type,
                 std::uint64_t conversation_id) {
    return Truly([=](const liteim::UnreadKey& key) {
        return key.user_id == user_id && key.conversation.type == type &&
               key.conversation.id == conversation_id;
    });
}

class ServiceMockBoundaryTest : public ::testing::Test {
protected:
    ServiceMockBoundaryTest() : online(sessions, cache, "server-test", 30s) {}

    void SetUp() override {
        ON_CALL(cache, setUserOnline(_, _)).WillByDefault(Return(liteim::Status::ok()));
        ON_CALL(cache, setUserOffline(_)).WillByDefault(Return(liteim::Status::ok()));
        ON_CALL(cache, refreshUserOnline(_, _)).WillByDefault(Return(liteim::Status::ok()));
        ON_CALL(cache, isUserOnline(_, _))
            .WillByDefault(Invoke([](std::uint64_t, bool& online_flag) {
                online_flag = false;
                return liteim::Status::ok();
            }));
        ON_CALL(cache, getOnlineSession(_, _))
            .WillByDefault(Return(liteim::Status::error(liteim::ErrorCode::NotFound,
                                                        "user is offline")));
        ON_CALL(cache, incrUnread(_, _, _))
            .WillByDefault(Invoke([](const liteim::UnreadKey&, std::uint64_t delta,
                                     std::uint64_t& unread_count) {
                unread_count += delta;
                return liteim::Status::ok();
            }));
        ON_CALL(cache, getUnread(_, _))
            .WillByDefault(Invoke([](const liteim::UnreadKey&, std::uint64_t& unread_count) {
                unread_count = 0;
                return liteim::Status::ok();
            }));
        ON_CALL(cache, clearUnread(_)).WillByDefault(Return(liteim::Status::ok()));
        ON_CALL(cache, allowLoginAttempt(_, _, _))
            .WillByDefault(Invoke([](const liteim::LoginAttemptKey&, std::uint32_t,
                                     bool& allowed) {
                allowed = true;
                return liteim::Status::ok();
            }));
        ON_CALL(cache, recordLoginFailure(_, _)).WillByDefault(Return(liteim::Status::ok()));
        ON_CALL(cache, clearLoginFailure(_)).WillByDefault(Return(liteim::Status::ok()));
    }

    liteim::Session::Ptr bindUser(std::uint64_t user_id, std::uint64_t session_id) {
        auto session = makeSession(loop, session_id);
        EXPECT_TRUE(online.bindUser(user_id, session).isOk());
        return session;
    }

    liteim::EventLoop loop;
    NiceMock<liteim::test::MockStorage> storage;
    NiceMock<liteim::test::MockCache> cache;
    liteim::SessionManager sessions;
    liteim::OnlineService online;
};

}  // namespace

TEST_F(ServiceMockBoundaryTest, AuthLoginChecksLimiterBeforeStorageAndRecordsPasswordFailure) {
    const auto alice = userWithPassword(1001, "alice", "secret");
    auto session = makeSession(loop, 8001);
    liteim::AuthService service(storage, cache, online,
                                liteim::AuthServiceOptions{2, 30s, "127.0.0.1"});

    testing::InSequence sequence;
    EXPECT_CALL(cache, allowLoginAttempt(loginKeyIs("alice", "127.0.0.1"), 2, _))
        .WillOnce(DoAll(SetArgReferee<2>(true), Return(liteim::Status::ok())));
    EXPECT_CALL(storage, findUserByUsername("alice", _))
        .WillOnce(DoAll(SetArgReferee<1>(alice), Return(liteim::Status::ok())));
    EXPECT_CALL(cache, recordLoginFailure(loginKeyIs("alice", "127.0.0.1"), 30s))
        .WillOnce(Return(liteim::Status::ok()));
    EXPECT_CALL(cache, clearLoginFailure(_)).Times(0);
    EXPECT_CALL(cache, setUserOnline(_, _)).Times(0);

    auto request = requestFor(session, liteim::MessageType::LoginRequest,
                              authBody("alice", "wrong"));
    liteim::Packet response;
    const auto status = service.handleLogin(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    std::uint64_t user_id = 0;
    EXPECT_EQ(sessions.getUserBySession(session->id(), user_id).code(),
              liteim::ErrorCode::NotFound);
}

TEST_F(ServiceMockBoundaryTest, AuthLoginSuccessClearsFailuresAndBindsOnlineState) {
    const auto alice = userWithPassword(1001, "alice", "secret");
    auto session = makeSession(loop, 8001);
    liteim::AuthService service(storage, cache, online,
                                liteim::AuthServiceOptions{2, 30s, "127.0.0.1"});

    testing::InSequence sequence;
    EXPECT_CALL(cache, allowLoginAttempt(loginKeyIs("alice", "127.0.0.1"), 2, _))
        .WillOnce(DoAll(SetArgReferee<2>(true), Return(liteim::Status::ok())));
    EXPECT_CALL(storage, findUserByUsername("alice", _))
        .WillOnce(DoAll(SetArgReferee<1>(alice), Return(liteim::Status::ok())));
    EXPECT_CALL(cache, clearLoginFailure(loginKeyIs("alice", "127.0.0.1")))
        .WillOnce(Return(liteim::Status::ok()));
    EXPECT_CALL(cache, setUserOnline(onlineSessionIs(1001, session->id(), "server-test"), 30s))
        .WillOnce(Return(liteim::Status::ok()));

    auto request = requestFor(session, liteim::MessageType::LoginRequest,
                              authBody("alice", "secret"));
    liteim::Packet response;
    const auto status = service.handleLogin(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::LoginResponse);
    std::uint64_t user_id = 0;
    ASSERT_TRUE(sessions.getUserBySession(session->id(), user_id).isOk());
    EXPECT_EQ(user_id, 1001U);
}

TEST_F(ServiceMockBoundaryTest, ChatOnlineReceiverSkipsOfflineRowsAndUnreadCounter) {
    auto sender = bindUser(1001, 8101);
    RunningSession receiver(8102);
    ASSERT_TRUE(online.bindUser(1002, receiver.session()).isOk());
    liteim::ChatService service(storage, cache, online);

    liteim::UserRecord receiver_user;
    receiver_user.user_id = 1002;
    receiver_user.username = "bob";
    receiver_user.nickname = "Bob";

    EXPECT_CALL(storage, findUserById(1002, _))
        .WillOnce(DoAll(SetArgReferee<1>(receiver_user), Return(liteim::Status::ok())));
    EXPECT_CALL(storage, saveMessageWithOfflineRecipients(
                             Truly([](const liteim::MessageRecord& message) {
                                 return message.conversation.type ==
                                            liteim::ConversationType::kPrivate &&
                                        message.conversation.id == 10011002U &&
                                        message.sender_id == 1001U &&
                                        message.receiver_id == 1002U &&
                                        message.text == "online";
                             }),
                             IsEmpty(), _))
        .WillOnce(Invoke([](const liteim::MessageRecord& message,
                            const std::vector<std::uint64_t>&,
                            liteim::MessageRecord& saved_message) {
            saved_message = savedMessageFrom(message, 5001);
            return liteim::Status::ok();
        }));
    EXPECT_CALL(cache, incrUnread(_, _, _)).Times(0);

    auto request = requestFor(sender, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(1002, "online"));
    liteim::Packet response;
    const auto status = service.handlePrivateMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::PrivateMessageResponse);
}

TEST_F(ServiceMockBoundaryTest, ChatOfflineReceiverWritesOfflineRecipientAndUnread) {
    auto sender = bindUser(1001, 8101);
    liteim::ChatService service(storage, cache, online);
    liteim::UserRecord receiver_user;
    receiver_user.user_id = 1002;
    receiver_user.username = "bob";
    receiver_user.nickname = "Bob";

    EXPECT_CALL(storage, findUserById(1002, _))
        .WillOnce(DoAll(SetArgReferee<1>(receiver_user), Return(liteim::Status::ok())));
    EXPECT_CALL(storage, saveMessageWithOfflineRecipients(_, ElementsAre(1002), _))
        .WillOnce(Invoke([](const liteim::MessageRecord& message,
                            const std::vector<std::uint64_t>&,
                            liteim::MessageRecord& saved_message) {
            saved_message = savedMessageFrom(message, 5002);
            return liteim::Status::ok();
        }));
    EXPECT_CALL(cache, incrUnread(unreadKeyIs(1002, liteim::ConversationType::kPrivate, 10011002),
                                  1, _))
        .WillOnce(DoAll(SetArgReferee<2>(1), Return(liteim::Status::ok())));

    auto request = requestFor(sender, liteim::MessageType::PrivateMessageRequest,
                              privateMessageBody(1002, "offline"));
    liteim::Packet response;
    const auto status = service.handlePrivateMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::PrivateMessageResponse);
}

TEST_F(ServiceMockBoundaryTest, GroupMessageAuthorizesBeforeSaveAndUnreadForOfflineMember) {
    auto sender = bindUser(1001, 8201);
    RunningSession online_member(8202);
    ASSERT_TRUE(online.bindUser(1002, online_member.session()).isOk());
    liteim::GroupService service(storage, cache, online);

    liteim::GroupRecord group{88, 1001, "team", 1700000000000LL};
    std::vector<liteim::GroupMemberRecord> members{
        {1001, "alice", "Alice", 1},
        {1002, "bob", "Bob", 2},
        {1003, "charlie", "Charlie", 3},
    };

    testing::InSequence sequence;
    EXPECT_CALL(storage, findGroupById(88, _))
        .WillOnce(DoAll(SetArgReferee<1>(group), Return(liteim::Status::ok())));
    EXPECT_CALL(storage, getGroupMembers(88, _))
        .WillOnce(DoAll(SetArgReferee<1>(members), Return(liteim::Status::ok())));
    EXPECT_CALL(storage, saveMessageWithOfflineRecipients(
                             Truly([](const liteim::MessageRecord& message) {
                                 return message.conversation.type ==
                                            liteim::ConversationType::kGroup &&
                                        message.conversation.id == 88U &&
                                        message.sender_id == 1001U &&
                                        message.receiver_id == 88U &&
                                        message.text == "team message";
                             }),
                             ElementsAre(1003), _))
        .WillOnce(Invoke([](const liteim::MessageRecord& message,
                            const std::vector<std::uint64_t>&,
                            liteim::MessageRecord& saved_message) {
            saved_message = savedMessageFrom(message, 9001);
            return liteim::Status::ok();
        }));
    EXPECT_CALL(cache, incrUnread(unreadKeyIs(1003, liteim::ConversationType::kGroup, 88), 1, _))
        .WillOnce(DoAll(SetArgReferee<2>(1), Return(liteim::Status::ok())));

    auto request = requestFor(sender, liteim::MessageType::GroupMessageRequest,
                              groupMessageBody(88, "team message"));
    liteim::Packet response;
    const auto status = service.handleGroupMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::GroupMessageResponse);
}

TEST_F(ServiceMockBoundaryTest, HistoryPrivateQueryChecksMembershipBeforeStorageQuery) {
    auto session = bindUser(1001, 8301);
    liteim::HistoryService service(storage, online, liteim::HistoryServiceOptions{20, 50});

    EXPECT_CALL(storage, findGroupById(_, _)).Times(0);
    EXPECT_CALL(storage, getGroupMembers(_, _)).Times(0);
    EXPECT_CALL(storage, getHistory(Truly([](const liteim::HistoryQuery& query) {
                                 return query.conversation.type ==
                                            liteim::ConversationType::kPrivate &&
                                        query.conversation.id == 10011002U &&
                                        query.before_message_id == 5009U &&
                                        query.limit == 10U;
                             }),
                             _))
        .WillOnce(DoAll(SetArgReferee<1>(std::vector<liteim::MessageRecord>{}),
                        Return(liteim::Status::ok())));

    auto request = requestFor(session, liteim::MessageType::HistoryRequest,
                              historyBody(liteim::ConversationType::kPrivate, 10011002, 5009, 10));
    liteim::Packet response;
    const auto status = service.handleHistory(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::HistoryResponse);
}

TEST_F(ServiceMockBoundaryTest, HistoryGroupQueryAuthorizesGroupMemberBeforeStorageQuery) {
    auto session = bindUser(1002, 8302);
    liteim::HistoryService service(storage, online, liteim::HistoryServiceOptions{20, 50});

    liteim::GroupRecord group{88, 1001, "team", 1700000000000LL};
    std::vector<liteim::GroupMemberRecord> members{
        {1001, "alice", "Alice", 1},
        {1002, "bob", "Bob", 2},
    };

    testing::InSequence sequence;
    EXPECT_CALL(storage, findGroupById(88, _))
        .WillOnce(DoAll(SetArgReferee<1>(group), Return(liteim::Status::ok())));
    EXPECT_CALL(storage, getGroupMembers(88, _))
        .WillOnce(DoAll(SetArgReferee<1>(members), Return(liteim::Status::ok())));
    EXPECT_CALL(storage, getHistory(Truly([](const liteim::HistoryQuery& query) {
                                 return query.conversation.type == liteim::ConversationType::kGroup &&
                                        query.conversation.id == 88U &&
                                        query.before_message_id == 0U &&
                                        query.limit == 20U;
                             }),
                             _))
        .WillOnce(DoAll(SetArgReferee<1>(std::vector<liteim::MessageRecord>{}),
                        Return(liteim::Status::ok())));

    auto request = requestFor(session, liteim::MessageType::HistoryRequest,
                              historyBody(liteim::ConversationType::kGroup, 88, std::nullopt,
                                          std::nullopt));
    liteim::Packet response;
    const auto status = service.handleHistory(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::HistoryResponse);
}
