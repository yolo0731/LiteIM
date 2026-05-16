#include "liteim/service/BotService.hpp"

#include "liteim/cache/ICache.hpp"
#include "liteim/service/BotGateway.hpp"
#include "liteim/service/OnlineService.hpp"
#include "liteim/service/SessionManager.hpp"
#include "liteim/storage/IStorage.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;

class FakeStorage final : public liteim::IStorage {
public:
    liteim::Status createUser(const liteim::CreateUserRequest&,
                              liteim::UserRecord&) override {
        return liteim::Status::ok();
    }

    liteim::Status findUserByUsername(const std::string&, liteim::UserRecord&) override {
        return liteim::Status::error(liteim::ErrorCode::NotFound, "user was not found");
    }

    liteim::Status findUserById(std::uint64_t, liteim::UserRecord&) override {
        return liteim::Status::error(liteim::ErrorCode::NotFound, "user was not found");
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

    liteim::Status findGroupById(std::uint64_t, liteim::GroupRecord&) override {
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
        saved_offline_user_ids = offline_user_ids;
        saved_message = message;
        saved_message.message_id = next_message_id++;
        saved_message.created_at_ms = 1800000000000LL;
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

    std::vector<liteim::MessageRecord> saved_messages;
    std::vector<std::uint64_t> saved_offline_user_ids;
    std::uint64_t next_message_id{6001};
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

class BotServiceFixture : public ::testing::Test {
protected:
    BotServiceFixture()
        : online(sessions, cache, "server-a", 30s), service(storage, cache, online, gateway) {}

    liteim::MessageRecord userMessage() const {
        liteim::MessageRecord message;
        message.message_id = 5001;
        message.conversation = {liteim::ConversationType::kPrivate, 10019001};
        message.sender_id = 1001;
        message.receiver_id = 9001;
        message.text = "hello mira";
        message.created_at_ms = 1700000000000LL;
        return message;
    }

    FakeStorage storage;
    FakeCache cache;
    liteim::SessionManager sessions;
    liteim::OnlineService online;
    liteim::EchoBotGateway gateway;
    liteim::BotService service;
};

}  // namespace

TEST_F(BotServiceFixture, PrivateBotReplySaveSucceedsEvenWhenPushTargetIsGone) {
    const auto status = service.handlePrivateMessageToBot(userMessage(), nullptr);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_EQ(storage.save_message_calls, 1);
    ASSERT_EQ(storage.saved_messages.size(), 1U);
    EXPECT_TRUE(storage.saved_offline_user_ids.empty());
    EXPECT_EQ(storage.saved_messages.front().sender_id, 9001U);
    EXPECT_EQ(storage.saved_messages.front().receiver_id, 1001U);
    EXPECT_EQ(storage.saved_messages.front().text, "Echo: hello mira");
}
