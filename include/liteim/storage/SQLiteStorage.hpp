#pragma once

#include "liteim/storage/IStorage.hpp"

#include <string>

struct sqlite3;

namespace liteim::storage {

class SQLiteStorage final : public IStorage {
public:
    explicit SQLiteStorage(
        std::string db_path = "liteim.db",
        std::string schema_path = "sql/init.sql");
    ~SQLiteStorage() override;

    SQLiteStorage(const SQLiteStorage&) = delete;
    SQLiteStorage& operator=(const SQLiteStorage&) = delete;

    std::optional<User> createUser(const CreateUserRequest& request) override;
    std::optional<User> findUserByUsername(const std::string& username) const override;
    std::optional<User> findUserById(UserId user_id) const override;

    bool addFriendship(UserId user_id, UserId friend_id) override;
    std::vector<User> getFriends(UserId user_id) const override;

    std::optional<Group> createGroup(const CreateGroupRequest& request) override;
    bool addGroupMember(GroupId group_id, UserId user_id) override;
    bool removeGroupMember(GroupId group_id, UserId user_id) override;
    std::vector<UserId> getGroupMembers(GroupId group_id) const override;
    std::optional<Group> findGroupById(GroupId group_id) const override;

    std::optional<PrivateMessage> savePrivateMessage(
        const SavePrivateMessageRequest& request) override;
    std::optional<GroupMessage> saveGroupMessage(
        const SaveGroupMessageRequest& request) override;

    std::vector<PrivateMessage> getPrivateHistory(
        UserId first_user_id,
        UserId second_user_id,
        std::size_t limit,
        std::size_t offset) const override;
    std::vector<GroupMessage> getGroupHistory(
        GroupId group_id,
        std::size_t limit,
        std::size_t offset) const override;
    std::vector<PrivateMessage> getOfflineMessages(
        UserId user_id,
        std::size_t limit) const override;

private:
    void executeSql(const std::string& sql) const;
    void executeSchemaFile(const std::string& schema_path) const;

    sqlite3* db_ = nullptr;
};

}  // namespace liteim::storage
