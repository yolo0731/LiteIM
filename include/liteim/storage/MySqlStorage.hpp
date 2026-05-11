#pragma once

#include "liteim/storage/FriendDao.hpp"
#include "liteim/storage/GroupDao.hpp"
#include "liteim/storage/IStorage.hpp"
#include "liteim/storage/MessageDao.hpp"
#include "liteim/storage/MySqlPool.hpp"
#include "liteim/storage/OfflineMessageDao.hpp"
#include "liteim/storage/UserDao.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace liteim {

class MySqlStorage final : public IStorage {
public:
    explicit MySqlStorage(MySqlPool& pool,
                          std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds{500});

    Status createUser(const CreateUserRequest& request, UserRecord& created_user) override;
    Status findUserByUsername(const std::string& username, UserRecord& user) override;
    Status findUserById(std::uint64_t user_id, UserRecord& user) override;

    Status addFriendship(std::uint64_t user_id, std::uint64_t friend_id) override;
    Status getFriends(std::uint64_t user_id, std::vector<UserProfileRecord>& friends) override;

    Status createGroup(const CreateGroupRequest& request, GroupRecord& created_group) override;
    Status addGroupMember(std::uint64_t group_id, std::uint64_t user_id) override;
    Status removeGroupMember(std::uint64_t group_id, std::uint64_t user_id) override;
    Status getGroupMembers(std::uint64_t group_id, std::vector<GroupMemberRecord>& members) override;

    Status saveMessage(const MessageRecord& message, std::uint64_t& message_id) override;
    Status saveMessageWithOfflineRecipients(const MessageRecord& message,
                                            const std::vector<std::uint64_t>& offline_user_ids,
                                            MessageRecord& saved_message) override;
    Status saveOfflineMessage(std::uint64_t user_id, std::uint64_t message_id) override;
    Status getOfflineMessages(std::uint64_t user_id, std::vector<OfflineMessageRecord>& messages) override;
    Status markOfflineDelivered(std::uint64_t user_id, const std::vector<std::uint64_t>& message_ids) override;
    Status getHistory(const HistoryQuery& query, std::vector<MessageRecord>& messages) override;

private:
    MySqlPool& pool_;
    std::chrono::milliseconds acquire_timeout_;
    UserDao user_dao_;
    FriendDao friend_dao_;
    GroupDao group_dao_;
    MessageDao message_dao_;
    OfflineMessageDao offline_message_dao_;
};

} // namespace liteim
