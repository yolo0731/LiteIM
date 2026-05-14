#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace liteim {

class IStorage {
public:
    virtual ~IStorage() = default;
    // 用户相关接口
    virtual Status createUser(const CreateUserRequest& request, UserRecord& created_user) = 0;
    virtual Status findUserByUsername(const std::string& username, UserRecord& user) = 0;
    virtual Status findUserById(std::uint64_t user_id, UserRecord& user) = 0;

    virtual Status addFriendship(std::uint64_t user_id, std::uint64_t friend_id) = 0;
    virtual Status getFriends(std::uint64_t user_id, std::vector<UserProfileRecord>& friends) = 0;
    // 群组相关接口
    virtual Status createGroup(const CreateGroupRequest& request, GroupRecord& created_group) = 0;
    virtual Status addGroupMember(std::uint64_t group_id, std::uint64_t user_id) = 0;
    virtual Status removeGroupMember(std::uint64_t group_id, std::uint64_t user_id) = 0;
    virtual Status getGroupMembers(std::uint64_t group_id,
                                   std::vector<GroupMemberRecord>& members) = 0;
    // 消息相关接口
    virtual Status saveMessage(const MessageRecord& message, std::uint64_t& message_id) = 0;
    virtual Status
    saveMessageWithOfflineRecipients(const MessageRecord& message,
                                     const std::vector<std::uint64_t>& offline_user_ids,
                                     MessageRecord& saved_message) = 0;
    virtual Status saveOfflineMessage(std::uint64_t user_id, std::uint64_t message_id) = 0;
    virtual Status getOfflineMessages(std::uint64_t user_id,
                                      std::vector<OfflineMessageRecord>& messages) = 0;
    virtual Status markOfflineDelivered(std::uint64_t user_id,
                                        const std::vector<std::uint64_t>& message_ids) = 0;
    virtual Status getHistory(const HistoryQuery& query, std::vector<MessageRecord>& messages) = 0;
};

}  // namespace liteim
