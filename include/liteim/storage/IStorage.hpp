#pragma once

#include "liteim/storage/StorageTypes.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace liteim::storage {

class IStorage {
public:
    virtual ~IStorage() = default;

    virtual std::optional<User> createUser(const CreateUserRequest& request) = 0;
    virtual std::optional<User> findUserByUsername(const std::string& username) const = 0;
    virtual std::optional<User> findUserById(UserId user_id) const = 0;

    virtual bool addFriendship(UserId user_id, UserId friend_id) = 0;
    virtual std::vector<User> getFriends(UserId user_id) const = 0;

    virtual std::optional<Group> createGroup(const CreateGroupRequest& request) = 0;
    virtual bool addGroupMember(GroupId group_id, UserId user_id) = 0;
    virtual bool removeGroupMember(GroupId group_id, UserId user_id) = 0;
    virtual std::vector<UserId> getGroupMembers(GroupId group_id) const = 0;
    virtual std::optional<Group> findGroupById(GroupId group_id) const = 0;

    virtual std::optional<PrivateMessage> savePrivateMessage(
        const SavePrivateMessageRequest& request) = 0;
    virtual std::optional<GroupMessage> saveGroupMessage(
        const SaveGroupMessageRequest& request) = 0;

    virtual std::vector<PrivateMessage> getPrivateHistory(
        UserId first_user_id,
        UserId second_user_id,
        std::size_t limit,
        std::size_t offset) const = 0;
    virtual std::vector<GroupMessage> getGroupHistory(
        GroupId group_id,
        std::size_t limit,
        std::size_t offset) const = 0;
    virtual std::vector<PrivateMessage> getOfflineMessages(
        UserId user_id,
        std::size_t limit) const = 0;
};

}  // namespace liteim::storage
