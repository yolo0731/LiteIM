#include "liteim/service/GroupService.hpp"

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

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
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

liteim::Bytes createGroupBody(const std::string& group_name) {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendString(liteim::TlvType::GroupName, group_name, body).isOk());
    return body;
}

liteim::Bytes groupIdBody(std::uint64_t group_id) {
    liteim::Bytes body;
    EXPECT_TRUE(liteim::appendUint64(liteim::TlvType::GroupId, group_id, body).isOk());
    return body;
}

liteim::Bytes groupMessageBody(std::uint64_t group_id, const std::string& text) {
    auto body = groupIdBody(group_id);
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

    liteim::Status createGroup(const liteim::CreateGroupRequest& request,
                               liteim::GroupRecord& created_group) override {
        ++create_group_calls;
        last_create_request = request;
        const auto owner = users.find(request.owner_id);
        if (owner == users.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user was not found");
        }
        if (request.group_name.empty()) {
            return liteim::Status::error(liteim::ErrorCode::InvalidArgument,
                                         "group_name must not be empty");
        }

        liteim::GroupRecord group;
        group.group_id = next_group_id++;
        group.owner_id = request.owner_id;
        group.group_name = request.group_name;
        group.created_at_ms = next_created_at_ms++;
        groups[group.group_id] = group;
        addMemberRecord(group.group_id, owner->second);
        created_group = group;
        return liteim::Status::ok();
    }

    liteim::Status addGroupMember(std::uint64_t group_id, std::uint64_t user_id) override {
        ++add_group_member_calls;
        last_add_group_id = group_id;
        last_add_user_id = user_id;
        const auto group = groups.find(group_id);
        if (group == groups.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "group was not found");
        }
        const auto user = users.find(user_id);
        if (user == users.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "user was not found");
        }
        addMemberRecord(group_id, user->second);
        return liteim::Status::ok();
    }

    liteim::Status removeGroupMember(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status getGroupMembers(std::uint64_t group_id,
                                   std::vector<liteim::GroupMemberRecord>& output) override {
        ++get_group_members_calls;
        last_get_members_group_id = group_id;
        if (groups.find(group_id) == groups.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "group was not found");
        }
        output = members[group_id];
        return liteim::Status::ok();
    }

    liteim::Status findGroupById(std::uint64_t group_id,
                                 liteim::GroupRecord& group) override {
        ++find_group_calls;
        last_find_group_id = group_id;
        const auto it = groups.find(group_id);
        if (it == groups.end()) {
            return liteim::Status::error(liteim::ErrorCode::NotFound, "group was not found");
        }
        group = it->second;
        return liteim::Status::ok();
    }

    liteim::Status getGroupsForUser(std::uint64_t user_id,
                                    std::vector<liteim::GroupRecord>& output) override {
        ++get_groups_for_user_calls;
        last_get_groups_user_id = user_id;
        output.clear();
        for (const auto& item : groups) {
            const auto members_it = members.find(item.first);
            if (members_it == members.end()) {
                continue;
            }
            const auto found =
                std::any_of(members_it->second.begin(), members_it->second.end(),
                            [user_id](const liteim::GroupMemberRecord& member) {
                                return member.user_id == user_id;
                            });
            if (found) {
                output.push_back(item.second);
            }
        }
        std::sort(output.begin(), output.end(),
                  [](const liteim::GroupRecord& lhs, const liteim::GroupRecord& rhs) {
                      return lhs.group_id < rhs.group_id;
                  });
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
        saved_message.created_at_ms = next_message_time_ms++;
        saved_messages.push_back(saved_message);
        return liteim::Status::ok();
    }

    liteim::Status saveOfflineMessage(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status getOfflineMessages(std::uint64_t,
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

    liteim::GroupRecord addGroup(std::uint64_t owner_id, const std::string& group_name) {
        liteim::GroupRecord group;
        const auto status = createGroup(liteim::CreateGroupRequest{owner_id, group_name}, group);
        EXPECT_TRUE(status.isOk()) << status.message();
        return group;
    }

    void addMember(std::uint64_t group_id, std::uint64_t user_id) {
        const auto status = addGroupMember(group_id, user_id);
        EXPECT_TRUE(status.isOk()) << status.message();
    }

    std::unordered_map<std::uint64_t, liteim::UserRecord> users;
    std::unordered_map<std::uint64_t, liteim::GroupRecord> groups;
    std::unordered_map<std::uint64_t, std::vector<liteim::GroupMemberRecord>> members;
    liteim::CreateGroupRequest last_create_request;
    liteim::MessageRecord last_message;
    std::vector<std::uint64_t> last_offline_user_ids;
    std::vector<std::vector<std::uint64_t>> saved_offline_user_ids;
    std::vector<liteim::MessageRecord> saved_messages;
    std::uint64_t next_group_id{8801};
    std::uint64_t next_message_id{9001};
    std::int64_t next_created_at_ms{1700000000000LL};
    std::int64_t next_message_time_ms{1800000000000LL};
    std::uint64_t last_add_group_id{0};
    std::uint64_t last_add_user_id{0};
    std::uint64_t last_get_members_group_id{0};
    std::uint64_t last_find_group_id{0};
    std::uint64_t last_get_groups_user_id{0};
    int create_group_calls{0};
    int add_group_member_calls{0};
    int get_group_members_calls{0};
    int find_group_calls{0};
    int get_groups_for_user_calls{0};
    int save_message_calls{0};

private:
    void addMemberRecord(std::uint64_t group_id, const liteim::UserRecord& user) {
        auto& list = members[group_id];
        const auto exists =
            std::any_of(list.begin(), list.end(),
                        [&](const liteim::GroupMemberRecord& member) {
                            return member.user_id == user.user_id;
                        });
        if (exists) {
            return;
        }
        list.push_back(liteim::GroupMemberRecord{
            user.user_id, user.username, user.nickname, next_created_at_ms++});
        std::sort(list.begin(), list.end(),
                  [](const liteim::GroupMemberRecord& lhs,
                     const liteim::GroupMemberRecord& rhs) {
                      return lhs.user_id < rhs.user_id;
                  });
    }
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
        unread_count = unread_counts[key.user_id] + delta;
        unread_counts[key.user_id] = unread_count;
        return liteim::Status::ok();
    }

    liteim::Status getUnread(const liteim::UnreadKey& key,
                             std::uint64_t& unread_count) override {
        unread_count = unread_counts[key.user_id];
        return liteim::Status::ok();
    }

    liteim::Status clearUnread(const liteim::UnreadKey& key) override {
        unread_counts.erase(key.user_id);
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
    std::unordered_map<std::uint64_t, std::uint64_t> unread_counts;
    liteim::UnreadKey last_unread_key;
    std::vector<liteim::UnreadKey> unread_keys;
    std::uint64_t last_unread_delta{0};
    int incr_unread_calls{0};
    bool fail_incr_unread{false};
};

class GroupServiceFixture : public ::testing::Test {
protected:
    GroupServiceFixture() : online(sessions, cache, "server-a", 30s),
                            bot_service(storage, cache, online, bot_gateway),
                            service(storage, cache, online),
                            service_with_bot(storage, cache, online, &bot_service) {
        storage.addUser(1001, "alice", "Alice");
        storage.addUser(1002, "bob", "Bob");
        storage.addUser(1003, "charlie", "Charlie");
        storage.addUser(9001, "mira_bot", "Mira Bot");
    }

    liteim::MessageRouter::RouterRequest requestFor(const liteim::Session::Ptr& session,
                                                    liteim::MessageType type,
                                                    liteim::Bytes body = {}) {
        liteim::TlvMap fields;
        EXPECT_TRUE(liteim::parseTlvMap(body, fields).isOk());
        return liteim::MessageRouter::RouterRequest{
            session, makePacket(type, 38, std::move(body)), std::move(fields)};
    }

    liteim::Session::Ptr bindUser(std::uint64_t user_id, std::uint64_t session_id) {
        auto session = makeSession(loop, session_id);
        EXPECT_TRUE(online.bindUser(user_id, session).isOk());
        return session;
    }

    liteim::Session::Ptr bindAlice(std::uint64_t session_id = 7101) {
        return bindUser(1001, session_id);
    }

    liteim::EventLoop loop;
    FakeStorage storage;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService online;
    liteim::EchoBotGateway bot_gateway;
    liteim::BotService bot_service;
    liteim::GroupService service;
    liteim::GroupService service_with_bot;
};

}  // namespace

TEST(GroupServiceTest, HeaderIsSelfContained) {
    using Service = liteim::GroupService;

    static_assert(std::is_constructible_v<Service, liteim::IStorage&, liteim::ICache&,
                                          liteim::OnlineService&>);
    static_assert(std::is_constructible_v<Service, liteim::IStorage&, liteim::ICache&,
                                          liteim::OnlineService&, liteim::BotService*>);
    static_assert(
        std::is_same_v<decltype(&Service::registerHandlers),
                       liteim::Status (Service::*)(liteim::MessageRouter&)>);
    static_assert(std::is_same_v<decltype(&Service::handleCreateGroup),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
    static_assert(std::is_same_v<decltype(&Service::handleJoinGroup),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
    static_assert(std::is_same_v<decltype(&Service::handleListGroups),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
    static_assert(std::is_same_v<decltype(&Service::handleGroupMessage),
                                 liteim::Status (Service::*)(
                                     const liteim::MessageRouter::RouterRequest&,
                                     liteim::Packet&)>);
}

TEST_F(GroupServiceFixture, GroupMentionTriggersBotReplyWhenBotIsMemberAndFiltersBotUnread) {
    const auto group = storage.addGroup(1001, "team");
    storage.addMember(group.group_id, 1002);
    storage.addMember(group.group_id, 1003);
    storage.addMember(group.group_id, 9001);

    RunningSession alice(7101);
    ASSERT_TRUE(online.bindUser(1001, alice.session()).isOk());
    RunningSession bob(7102);
    ASSERT_TRUE(online.bindUser(1002, bob.session()).isOk());

    auto request = requestFor(alice.session(), liteim::MessageType::GroupMessageRequest,
                              groupMessageBody(group.group_id, "hi @mira_bot"));
    liteim::Packet response;

    const auto status = service_with_bot.handleGroupMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::GroupMessageResponse);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::MessageId), 9001U);
    EXPECT_EQ(stringField(response, liteim::TlvType::MessageText), "hi @mira_bot");

    ASSERT_EQ(storage.save_message_calls, 2);
    ASSERT_EQ(storage.saved_messages.size(), 2U);
    ASSERT_EQ(storage.saved_offline_user_ids.size(), 2U);
    EXPECT_EQ(storage.saved_messages[0].sender_id, 1001U);
    EXPECT_EQ(storage.saved_messages[0].receiver_id, group.group_id);
    EXPECT_EQ(storage.saved_messages[0].text, "hi @mira_bot");
    ASSERT_EQ(storage.saved_offline_user_ids[0].size(), 1U);
    EXPECT_EQ(storage.saved_offline_user_ids[0].front(), 1003U);

    EXPECT_EQ(storage.saved_messages[1].sender_id, 9001U);
    EXPECT_EQ(storage.saved_messages[1].receiver_id, group.group_id);
    EXPECT_EQ(storage.saved_messages[1].text, "Echo: hi @mira_bot");
    ASSERT_EQ(storage.saved_offline_user_ids[1].size(), 1U);
    EXPECT_EQ(storage.saved_offline_user_ids[1].front(), 1003U);

    ASSERT_EQ(cache.incr_unread_calls, 2);
    ASSERT_EQ(cache.unread_keys.size(), 2U);
    EXPECT_EQ(cache.unread_keys[0].user_id, 1003U);
    EXPECT_EQ(cache.unread_keys[1].user_id, 1003U);

    const auto bob_original = readPacket(bob.peerFd(), 2s);
    ASSERT_TRUE(bob_original.has_value());
    EXPECT_EQ(bob_original->header.msg_type, liteim::MessageType::GroupMessagePush);
    EXPECT_EQ(uint64Field(*bob_original, liteim::TlvType::MessageId), 9001U);
    EXPECT_EQ(uint64Field(*bob_original, liteim::TlvType::SenderId), 1001U);

    const auto bob_reply = readPacket(bob.peerFd(), 2s);
    ASSERT_TRUE(bob_reply.has_value());
    EXPECT_EQ(bob_reply->header.msg_type, liteim::MessageType::GroupMessagePush);
    EXPECT_EQ(uint64Field(*bob_reply, liteim::TlvType::MessageId), 9002U);
    EXPECT_EQ(uint64Field(*bob_reply, liteim::TlvType::SenderId), 9001U);
    EXPECT_EQ(stringField(*bob_reply, liteim::TlvType::MessageText), "Echo: hi @mira_bot");

    const auto alice_reply = readPacket(alice.peerFd(), 2s);
    ASSERT_TRUE(alice_reply.has_value());
    EXPECT_EQ(alice_reply->header.msg_type, liteim::MessageType::GroupMessagePush);
    EXPECT_EQ(uint64Field(*alice_reply, liteim::TlvType::MessageId), 9002U);
    EXPECT_EQ(uint64Field(*alice_reply, liteim::TlvType::SenderId), 9001U);
}

TEST_F(GroupServiceFixture, GroupMentionDoesNotTriggerWhenBotIsNotGroupMember) {
    const auto group = storage.addGroup(1001, "team");
    storage.addMember(group.group_id, 1003);

    auto sender = bindAlice();
    auto request = requestFor(sender, liteim::MessageType::GroupMessageRequest,
                              groupMessageBody(group.group_id, "hi @mira_bot"));
    liteim::Packet response;

    const auto status = service_with_bot.handleGroupMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(storage.save_message_calls, 1);
    ASSERT_EQ(storage.last_offline_user_ids.size(), 1U);
    EXPECT_EQ(storage.last_offline_user_ids.front(), 1003U);
    EXPECT_EQ(cache.incr_unread_calls, 1);
}

TEST_F(GroupServiceFixture, GroupMessageWithoutMentionDoesNotTriggerBotButFiltersBotOffline) {
    const auto group = storage.addGroup(1001, "team");
    storage.addMember(group.group_id, 1003);
    storage.addMember(group.group_id, 9001);

    auto sender = bindAlice();
    auto request = requestFor(sender, liteim::MessageType::GroupMessageRequest,
                              groupMessageBody(group.group_id, "plain group message"));
    liteim::Packet response;

    const auto status = service_with_bot.handleGroupMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(storage.save_message_calls, 1);
    ASSERT_EQ(storage.last_offline_user_ids.size(), 1U);
    EXPECT_EQ(storage.last_offline_user_ids.front(), 1003U);
    EXPECT_EQ(cache.incr_unread_calls, 1);
    EXPECT_EQ(cache.last_unread_key.user_id, 1003U);
}

TEST_F(GroupServiceFixture, BotSenderDoesNotTriggerAnotherBotReply) {
    const auto group = storage.addGroup(1001, "team");
    storage.addMember(group.group_id, 9001);

    auto bot_session = bindUser(9001, 7191);
    auto request = requestFor(bot_session, liteim::MessageType::GroupMessageRequest,
                              groupMessageBody(group.group_id, "@mira_bot loop"));
    liteim::Packet response;

    const auto status = service_with_bot.handleGroupMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(storage.save_message_calls, 1);
    ASSERT_EQ(storage.last_offline_user_ids.size(), 1U);
    EXPECT_EQ(storage.last_offline_user_ids.front(), 1001U);
    EXPECT_EQ(cache.incr_unread_calls, 1);
}

TEST_F(GroupServiceFixture, CreateGroupUsesLoggedInUserAndReturnsGroupFields) {
    auto session = bindAlice();
    auto request = requestFor(session, liteim::MessageType::CreateGroupRequest,
                              createGroupBody("project room"));
    liteim::Packet response;

    const auto status = service.handleCreateGroup(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::CreateGroupResponse);
    EXPECT_EQ(response.header.seq_id, 38U);
    EXPECT_EQ(storage.create_group_calls, 1);
    EXPECT_EQ(storage.last_create_request.owner_id, 1001U);
    EXPECT_EQ(storage.last_create_request.group_name, "project room");
    EXPECT_EQ(uint64Field(response, liteim::TlvType::GroupId), 8801U);
    EXPECT_EQ(stringField(response, liteim::TlvType::GroupName), "project room");
}

TEST_F(GroupServiceFixture, JoinGroupAddsCurrentUserAndReturnsGroupFields) {
    const auto group = storage.addGroup(1001, "backend");
    auto session = bindUser(1002, 7102);
    auto request =
        requestFor(session, liteim::MessageType::JoinGroupRequest, groupIdBody(group.group_id));
    liteim::Packet response;

    const auto status = service.handleJoinGroup(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::JoinGroupResponse);
    EXPECT_EQ(storage.find_group_calls, 1);
    EXPECT_EQ(storage.last_find_group_id, group.group_id);
    EXPECT_EQ(storage.add_group_member_calls, 1);
    EXPECT_EQ(storage.last_add_group_id, group.group_id);
    EXPECT_EQ(storage.last_add_user_id, 1002U);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::GroupId), group.group_id);
    EXPECT_EQ(stringField(response, liteim::TlvType::GroupName), "backend");
}

TEST_F(GroupServiceFixture, ListGroupsReturnsCurrentUserGroups) {
    const auto alice_group = storage.addGroup(1001, "alice-only");
    const auto bob_group = storage.addGroup(1002, "bob-owned");
    const auto shared_group = storage.addGroup(1001, "shared");
    storage.addMember(shared_group.group_id, 1002);

    auto session = bindUser(1002, 7102);
    auto request = requestFor(session, liteim::MessageType::ListGroupsRequest);
    liteim::Packet response;

    const auto status = service.handleListGroups(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::ListGroupsResponse);
    EXPECT_EQ(storage.get_groups_for_user_calls, 1);
    EXPECT_EQ(storage.last_get_groups_user_id, 1002U);
    const auto group_ids = uint64Fields(response, liteim::TlvType::GroupId);
    const auto group_names = stringFields(response, liteim::TlvType::GroupName);
    ASSERT_EQ(group_ids.size(), 2U);
    ASSERT_EQ(group_names.size(), 2U);
    EXPECT_EQ(group_ids[0], bob_group.group_id);
    EXPECT_EQ(group_names[0], "bob-owned");
    EXPECT_EQ(group_ids[1], shared_group.group_id);
    EXPECT_EQ(group_names[1], "shared");
    EXPECT_NE(group_ids[0], alice_group.group_id);
}

TEST_F(GroupServiceFixture, GroupMessageRejectsNonMember) {
    const auto group = storage.addGroup(1001, "alice-only");
    auto session = bindUser(1002, 7102);
    auto request = requestFor(session, liteim::MessageType::GroupMessageRequest,
                              groupMessageBody(group.group_id, "hello group"));
    liteim::Packet response;

    const auto status = service.handleGroupMessage(request, response);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_EQ(storage.save_message_calls, 0);
    EXPECT_EQ(cache.incr_unread_calls, 0);
}

TEST_F(GroupServiceFixture, GroupMessagePushesOnlineMemberAndStoresOfflineRecipient) {
    const auto group = storage.addGroup(1001, "team");
    storage.addMember(group.group_id, 1002);
    storage.addMember(group.group_id, 1003);

    auto sender = bindAlice();
    RunningSession bob(7102);
    ASSERT_TRUE(online.bindUser(1002, bob.session()).isOk());

    auto request = requestFor(sender, liteim::MessageType::GroupMessageRequest,
                              groupMessageBody(group.group_id, "team hello"));
    liteim::Packet response;

    const auto status = service.handleGroupMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::GroupMessageResponse);
    EXPECT_EQ(storage.save_message_calls, 1);
    EXPECT_EQ(storage.last_message.conversation.type, liteim::ConversationType::kGroup);
    EXPECT_EQ(storage.last_message.conversation.id, group.group_id);
    EXPECT_EQ(storage.last_message.sender_id, 1001U);
    EXPECT_EQ(storage.last_message.receiver_id, group.group_id);
    EXPECT_EQ(storage.last_message.text, "team hello");
    ASSERT_EQ(storage.last_offline_user_ids.size(), 1U);
    EXPECT_EQ(storage.last_offline_user_ids.front(), 1003U);

    EXPECT_EQ(cache.incr_unread_calls, 1);
    EXPECT_EQ(cache.last_unread_key.user_id, 1003U);
    EXPECT_EQ(cache.last_unread_key.conversation.type, liteim::ConversationType::kGroup);
    EXPECT_EQ(cache.last_unread_key.conversation.id, group.group_id);
    EXPECT_EQ(cache.last_unread_delta, 1U);

    EXPECT_EQ(uint64Field(response, liteim::TlvType::MessageId), 9001U);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::ConversationType),
              static_cast<std::uint64_t>(liteim::ConversationType::kGroup));
    EXPECT_EQ(uint64Field(response, liteim::TlvType::ConversationId), group.group_id);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::SenderId), 1001U);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::ReceiverId), group.group_id);
    EXPECT_EQ(stringField(response, liteim::TlvType::MessageText), "team hello");
    EXPECT_EQ(uint64Field(response, liteim::TlvType::TimestampMs), 1800000000000ULL);

    const auto push = readPacket(bob.peerFd(), 2s);
    ASSERT_TRUE(push.has_value());
    EXPECT_EQ(push->header.msg_type, liteim::MessageType::GroupMessagePush);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::MessageId), 9001U);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::ConversationType),
              static_cast<std::uint64_t>(liteim::ConversationType::kGroup));
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::ConversationId), group.group_id);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::SenderId), 1001U);
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::ReceiverId), group.group_id);
    EXPECT_EQ(stringField(*push, liteim::TlvType::MessageText), "team hello");
    EXPECT_EQ(uint64Field(*push, liteim::TlvType::TimestampMs), 1800000000000ULL);
}

TEST_F(GroupServiceFixture, OfflineUnreadFailureStillReturnsSenderSuccess) {
    cache.fail_incr_unread = true;
    const auto group = storage.addGroup(1001, "team");
    storage.addMember(group.group_id, 1003);
    auto sender = bindAlice();
    auto request = requestFor(sender, liteim::MessageType::GroupMessageRequest,
                              groupMessageBody(group.group_id, "saved despite redis"));
    liteim::Packet response;

    const auto status = service.handleGroupMessage(request, response);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(response.header.msg_type, liteim::MessageType::GroupMessageResponse);
    EXPECT_EQ(storage.save_message_calls, 1);
    ASSERT_EQ(storage.last_offline_user_ids.size(), 1U);
    EXPECT_EQ(storage.last_offline_user_ids.front(), 1003U);
    EXPECT_EQ(cache.incr_unread_calls, 1);
    EXPECT_EQ(uint64Field(response, liteim::TlvType::MessageId), 9001U);
    EXPECT_EQ(stringField(response, liteim::TlvType::MessageText), "saved despite redis");
}

TEST_F(GroupServiceFixture, RegisteredHandlerSendsCreateGroupResponseThroughRouter) {
    RunningSession sender(7101);
    ASSERT_TRUE(online.bindUser(1001, sender.session()).isOk());

    liteim::ThreadPool pool(1);
    ASSERT_TRUE(pool.start().isOk());
    liteim::MessageRouter router(pool);
    ASSERT_TRUE(service.registerHandlers(router).isOk());

    router.route(sender.session(),
                 makePacket(liteim::MessageType::CreateGroupRequest, 3801,
                            createGroupBody("through router")));

    const auto response = readPacket(sender.peerFd(), 2s);
    pool.stop();

    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->header.msg_type, liteim::MessageType::CreateGroupResponse);
    EXPECT_EQ(response->header.seq_id, 3801U);
    EXPECT_EQ(uint64Field(*response, liteim::TlvType::GroupId), 8801U);
    EXPECT_EQ(stringField(*response, liteim::TlvType::GroupName), "through router");
}
