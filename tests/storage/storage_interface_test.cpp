#include "liteim/storage/IStorage.hpp"

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

namespace {

class FakeStorage final : public liteim::IStorage {
public:
    liteim::Status createUser(const liteim::CreateUserRequest& request,
                              liteim::UserRecord& created_user) override {
        created_user.user_id = 42;
        created_user.username = request.username;
        created_user.password_hash = request.password_hash;
        created_user.password_salt = request.password_salt;
        created_user.nickname = request.nickname;
        return liteim::Status::ok();
    }

    liteim::Status findUserByUsername(const std::string&, liteim::UserRecord& user) override {
        user.user_id = 42;
        return liteim::Status::ok();
    }

    liteim::Status findUserById(std::uint64_t user_id, liteim::UserRecord& user) override {
        user.user_id = user_id;
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
                              std::vector<liteim::UserProfileRecord>& friends) override {
        friends.push_back(liteim::UserProfileRecord{7, "friend", "Friend", 100});
        return liteim::Status::ok();
    }

    liteim::Status createGroup(const liteim::CreateGroupRequest& request,
                               liteim::GroupRecord& created_group) override {
        created_group.group_id = 9;
        created_group.owner_id = request.owner_id;
        created_group.group_name = request.group_name;
        return liteim::Status::ok();
    }

    liteim::Status addGroupMember(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status removeGroupMember(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status getGroupMembers(std::uint64_t,
                                   std::vector<liteim::GroupMemberRecord>& members) override {
        members.push_back(liteim::GroupMemberRecord{42, "user", "User", 200});
        return liteim::Status::ok();
    }

    liteim::Status findGroupById(std::uint64_t group_id,
                                 liteim::GroupRecord& group) override {
        group.group_id = group_id;
        group.owner_id = 42;
        group.group_name = "demo";
        group.created_at_ms = 300;
        return liteim::Status::ok();
    }

    liteim::Status getGroupsForUser(std::uint64_t,
                                    std::vector<liteim::GroupRecord>& groups) override {
        groups.push_back(liteim::GroupRecord{9, 42, "demo", 300});
        return liteim::Status::ok();
    }

    liteim::Status saveMessage(const liteim::MessageRecord&, std::uint64_t& message_id) override {
        message_id = 1001;
        return liteim::Status::ok();
    }

    liteim::Status
    saveMessageWithOfflineRecipients(const liteim::MessageRecord&,
                                     const std::vector<std::uint64_t>& offline_user_ids,
                                     liteim::MessageRecord& saved_message) override {
        saved_message.message_id = 1001;
        saved_message.receiver_id = offline_user_ids.empty() ? 7 : offline_user_ids.front();
        return liteim::Status::ok();
    }

    liteim::Status saveOfflineMessage(std::uint64_t, std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status
    getOfflineMessages(std::uint64_t, std::uint32_t,
                       std::vector<liteim::OfflineMessageRecord>& messages) override {
        messages.push_back(liteim::OfflineMessageRecord{1, 42, liteim::MessageRecord{}, 300});
        return liteim::Status::ok();
    }

    liteim::Status markOfflineDelivered(std::uint64_t, const std::vector<std::uint64_t>&) override {
        return liteim::Status::ok();
    }

    liteim::Status
    ackOfflineMessages(std::uint64_t, const std::vector<std::uint64_t>&,
                       std::vector<liteim::OfflineMessageRecord>& acked_messages) override {
        acked_messages.clear();
        return liteim::Status::ok();
    }

    liteim::Status getHistory(const liteim::HistoryQuery&,
                              std::vector<liteim::MessageRecord>& messages) override {
        messages.push_back(liteim::MessageRecord{
            1001, {liteim::ConversationType::kPrivate, 7}, 42, 7, "hello", 400, ""});
        return liteim::Status::ok();
    }
};

}  // namespace

TEST(StorageInterfaceTest, HeaderIsSelfContained) {
    static_assert(std::is_abstract_v<liteim::IStorage>);
    static_assert(std::has_virtual_destructor_v<liteim::IStorage>);

    liteim::ConversationKey key;
    key.type = liteim::ConversationType::kGroup;
    key.id = 11;

    EXPECT_EQ(key.id, 11U);
    EXPECT_EQ(key.type, liteim::ConversationType::kGroup);
}

TEST(StorageInterfaceTest, CanBeImplementedByFakeStorage) {
    FakeStorage storage;
    liteim::IStorage& interface = storage;

    liteim::UserRecord user;
    const auto create_status = interface.createUser({"alice", "hash", "salt", "Alice"}, user);
    ASSERT_TRUE(create_status.isOk()) << create_status.message();
    EXPECT_EQ(user.user_id, 42U);
    EXPECT_EQ(user.username, "alice");

    std::vector<liteim::UserProfileRecord> friends;
    const auto friends_status = interface.getFriends(user.user_id, friends);
    ASSERT_TRUE(friends_status.isOk()) << friends_status.message();
    ASSERT_EQ(friends.size(), 1U);
    EXPECT_EQ(friends.front().user_id, 7U);

    liteim::GroupRecord group;
    const auto group_status = interface.findGroupById(9, group);
    ASSERT_TRUE(group_status.isOk()) << group_status.message();
    EXPECT_EQ(group.group_id, 9U);
    EXPECT_EQ(group.group_name, "demo");

    std::vector<liteim::GroupRecord> groups;
    const auto groups_status = interface.getGroupsForUser(user.user_id, groups);
    ASSERT_TRUE(groups_status.isOk()) << groups_status.message();
    ASSERT_EQ(groups.size(), 1U);
    EXPECT_EQ(groups.front().group_id, 9U);
    EXPECT_EQ(groups.front().group_name, "demo");

    std::uint64_t message_id = 0;
    const auto save_status = interface.saveMessage(
        liteim::MessageRecord{0, {liteim::ConversationType::kPrivate, 7}, 42, 7, "hello", 400,
                              ""},
        message_id);
    ASSERT_TRUE(save_status.isOk()) << save_status.message();
    EXPECT_EQ(message_id, 1001U);

    liteim::MessageRecord saved_message;
    const auto combined_status = interface.saveMessageWithOfflineRecipients(
        liteim::MessageRecord{0, {liteim::ConversationType::kPrivate, 7}, 42, 7, "offline", 500,
                              ""},
        {7}, saved_message);
    ASSERT_TRUE(combined_status.isOk()) << combined_status.message();
    EXPECT_EQ(saved_message.message_id, 1001U);
}
