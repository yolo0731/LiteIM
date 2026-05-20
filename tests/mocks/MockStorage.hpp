#pragma once

#include "liteim/storage/IStorage.hpp"

#include <gmock/gmock.h>

namespace liteim::test {

class MockStorage : public IStorage {
public:
    MOCK_METHOD(Status, createUser, (const CreateUserRequest& request, UserRecord& created_user),
                (override));
    MOCK_METHOD(Status, findUserByUsername, (const std::string& username, UserRecord& user),
                (override));
    MOCK_METHOD(Status, findUserById, (std::uint64_t user_id, UserRecord& user), (override));
    MOCK_METHOD(Status, findMessageByClientMessageId,
                (std::uint64_t sender_id, const std::string& client_msg_id,
                 MessageRecord& message),
                (override));
    MOCK_METHOD(Status, addFriendship, (std::uint64_t user_id, std::uint64_t friend_id),
                (override));
    MOCK_METHOD(Status, getFriends,
                (std::uint64_t user_id, std::vector<UserProfileRecord>& friends), (override));
    MOCK_METHOD(Status, createGroup,
                (const CreateGroupRequest& request, GroupRecord& created_group), (override));
    MOCK_METHOD(Status, addGroupMember, (std::uint64_t group_id, std::uint64_t user_id),
                (override));
    MOCK_METHOD(Status, removeGroupMember, (std::uint64_t group_id, std::uint64_t user_id),
                (override));
    MOCK_METHOD(Status, getGroupMembers,
                (std::uint64_t group_id, std::vector<GroupMemberRecord>& members), (override));
    MOCK_METHOD(Status, findGroupById, (std::uint64_t group_id, GroupRecord& group), (override));
    MOCK_METHOD(Status, getGroupsForUser,
                (std::uint64_t user_id, std::vector<GroupRecord>& groups), (override));
    MOCK_METHOD(Status, saveMessage, (const MessageRecord& message, std::uint64_t& message_id),
                (override));
    MOCK_METHOD(Status, saveMessageWithOfflineRecipients,
                (const MessageRecord& message,
                 const std::vector<std::uint64_t>& offline_user_ids,
                 MessageRecord& saved_message),
                (override));
    MOCK_METHOD(Status, saveOfflineMessage, (std::uint64_t user_id, std::uint64_t message_id),
                (override));
    MOCK_METHOD(Status, getOfflineMessages,
                (std::uint64_t user_id, std::uint32_t limit,
                 std::vector<OfflineMessageRecord>& messages),
                (override));
    MOCK_METHOD(Status, markOfflineDelivered,
                (std::uint64_t user_id, const std::vector<std::uint64_t>& message_ids),
                (override));
    MOCK_METHOD(Status, ackOfflineMessages,
                (std::uint64_t user_id, const std::vector<std::uint64_t>& message_ids,
                 std::vector<OfflineMessageRecord>& acked_messages),
                (override));
    MOCK_METHOD(Status, ackPrivateMessageDelivery,
                (std::uint64_t user_id, std::uint64_t message_id, MessageRecord& message),
                (override));
    MOCK_METHOD(Status, getHistory,
                (const HistoryQuery& query, std::vector<MessageRecord>& messages), (override));
};

}  // namespace liteim::test
