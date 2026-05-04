#include "liteim/storage/SQLiteStorage.hpp"

#include <sqlite3.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace liteim::storage {
namespace {

constexpr int kPrivateMessageType = 0;
constexpr int kGroupMessageType = 1;

[[noreturn]] void throwSqlite(sqlite3* db, const std::string& operation) {
    const char* message = db == nullptr ? "sqlite db is null" : sqlite3_errmsg(db);
    throw std::runtime_error(operation + " failed: " + message);
}

void checkSqlite(sqlite3* db, int rc, const std::string& operation) {
    if (rc != SQLITE_OK) {
        throwSqlite(db, operation);
    }
}

std::string readFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("open schema file failed: " + path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string columnText(sqlite3_stmt* statement, int column) {
    const auto* text = sqlite3_column_text(statement, column);
    if (text == nullptr) {
        return "";
    }
    return reinterpret_cast<const char*>(text);
}

std::int64_t sqlLimit(std::size_t limit) {
    return limit == 0 ? -1 : static_cast<std::int64_t>(limit);
}

std::int64_t sqlOffset(std::size_t offset) {
    return static_cast<std::int64_t>(offset);
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        const int rc = sqlite3_prepare_v2(db_, sql, -1, &statement_, nullptr);
        if (rc != SQLITE_OK) {
            throwSqlite(db_, "sqlite3_prepare_v2");
        }
    }

    ~Statement() {
        sqlite3_finalize(statement_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bindInt64(int index, std::int64_t value) {
        checkSqlite(db_, sqlite3_bind_int64(statement_, index, value), "sqlite3_bind_int64");
    }

    void bindInt(int index, int value) {
        checkSqlite(db_, sqlite3_bind_int(statement_, index, value), "sqlite3_bind_int");
    }

    void bindText(int index, const std::string& value) {
        checkSqlite(
            db_,
            sqlite3_bind_text(statement_, index, value.c_str(), -1, SQLITE_TRANSIENT),
            "sqlite3_bind_text");
    }

    int step() {
        return sqlite3_step(statement_);
    }

    sqlite3_stmt* get() const {
        return statement_;
    }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* statement_ = nullptr;
};

User readUser(sqlite3_stmt* statement) {
    User user;
    user.id = static_cast<UserId>(sqlite3_column_int64(statement, 0));
    user.username = columnText(statement, 1);
    user.nickname = columnText(statement, 2);
    user.password_salt = columnText(statement, 3);
    user.password_hash = columnText(statement, 4);
    user.type = static_cast<UserType>(sqlite3_column_int(statement, 5));
    user.created_at = static_cast<UnixTimestamp>(sqlite3_column_int64(statement, 6));
    return user;
}

Group readGroup(sqlite3_stmt* statement) {
    Group group;
    group.id = static_cast<GroupId>(sqlite3_column_int64(statement, 0));
    group.name = columnText(statement, 1);
    group.owner_id = static_cast<UserId>(sqlite3_column_int64(statement, 2));
    group.created_at = static_cast<UnixTimestamp>(sqlite3_column_int64(statement, 3));
    return group;
}

PrivateMessage readPrivateMessage(sqlite3_stmt* statement) {
    PrivateMessage message;
    message.id = static_cast<MessageId>(sqlite3_column_int64(statement, 0));
    message.sender_id = static_cast<UserId>(sqlite3_column_int64(statement, 1));
    message.receiver_id = static_cast<UserId>(sqlite3_column_int64(statement, 2));
    message.body = columnText(statement, 3);
    message.created_at = static_cast<UnixTimestamp>(sqlite3_column_int64(statement, 4));
    message.delivered = sqlite3_column_int(statement, 5) != 0;
    return message;
}

GroupMessage readGroupMessage(sqlite3_stmt* statement) {
    GroupMessage message;
    message.id = static_cast<MessageId>(sqlite3_column_int64(statement, 0));
    message.group_id = static_cast<GroupId>(sqlite3_column_int64(statement, 1));
    message.sender_id = static_cast<UserId>(sqlite3_column_int64(statement, 2));
    message.body = columnText(statement, 3);
    message.created_at = static_cast<UnixTimestamp>(sqlite3_column_int64(statement, 4));
    return message;
}

bool isConstraintFailure(int rc) {
    return rc == SQLITE_CONSTRAINT || rc == SQLITE_CONSTRAINT_PRIMARYKEY ||
           rc == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT_FOREIGNKEY ||
           rc == SQLITE_CONSTRAINT_CHECK;
}

}  // namespace

SQLiteStorage::SQLiteStorage(std::string db_path, std::string schema_path) {
    const int rc = sqlite3_open_v2(
        db_path.c_str(),
        &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (rc != SQLITE_OK) {
        sqlite3* failed_db = db_;
        db_ = nullptr;
        const std::string message =
            failed_db == nullptr ? "sqlite db is null" : sqlite3_errmsg(failed_db);
        sqlite3_close(failed_db);
        throw std::runtime_error("sqlite3_open_v2 failed: " + message);
    }

    try {
        executeSql("PRAGMA foreign_keys = ON;");
        executeSchemaFile(schema_path);
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}

SQLiteStorage::~SQLiteStorage() {
    sqlite3_close(db_);
    db_ = nullptr;
}

std::optional<User> SQLiteStorage::createUser(const CreateUserRequest& request) {
    if (request.username.empty()) {
        return std::nullopt;
    }

    Statement statement(
        db_,
        "INSERT INTO users (username, nickname, password_salt, password_hash, user_type) "
        "VALUES (?, ?, ?, ?, ?);");
    statement.bindText(1, request.username);
    statement.bindText(2, request.nickname);
    statement.bindText(3, request.password_salt);
    statement.bindText(4, request.password_hash);
    statement.bindInt(5, static_cast<int>(request.type));

    const int rc = statement.step();
    if (rc == SQLITE_DONE) {
        return findUserById(static_cast<UserId>(sqlite3_last_insert_rowid(db_)));
    }
    if (isConstraintFailure(rc)) {
        return std::nullopt;
    }
    throwSqlite(db_, "createUser");
}

std::optional<User> SQLiteStorage::findUserByUsername(const std::string& username) const {
    Statement statement(
        db_,
        "SELECT id, username, nickname, password_salt, password_hash, user_type, created_at "
        "FROM users WHERE username = ?;");
    statement.bindText(1, username);

    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return readUser(statement.get());
    }
    if (rc == SQLITE_DONE) {
        return std::nullopt;
    }
    throwSqlite(db_, "findUserByUsername");
}

std::optional<User> SQLiteStorage::findUserById(UserId user_id) const {
    Statement statement(
        db_,
        "SELECT id, username, nickname, password_salt, password_hash, user_type, created_at "
        "FROM users WHERE id = ?;");
    statement.bindInt64(1, static_cast<std::int64_t>(user_id));

    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return readUser(statement.get());
    }
    if (rc == SQLITE_DONE) {
        return std::nullopt;
    }
    throwSqlite(db_, "findUserById");
}

bool SQLiteStorage::addFriendship(UserId user_id, UserId friend_id) {
    if (user_id == friend_id || !findUserById(user_id) || !findUserById(friend_id)) {
        return false;
    }

    executeSql("BEGIN IMMEDIATE;");
    try {
        Statement statement(
            db_,
            "INSERT OR IGNORE INTO friendships (user_id, friend_id) "
            "VALUES (?, ?), (?, ?);");
        statement.bindInt64(1, static_cast<std::int64_t>(user_id));
        statement.bindInt64(2, static_cast<std::int64_t>(friend_id));
        statement.bindInt64(3, static_cast<std::int64_t>(friend_id));
        statement.bindInt64(4, static_cast<std::int64_t>(user_id));
        const int rc = statement.step();
        if (rc != SQLITE_DONE) {
            throwSqlite(db_, "addFriendship");
        }
        executeSql("COMMIT;");
        return true;
    } catch (...) {
        try {
            executeSql("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

std::vector<User> SQLiteStorage::getFriends(UserId user_id) const {
    Statement statement(
        db_,
        "SELECT u.id, u.username, u.nickname, u.password_salt, u.password_hash, "
        "u.user_type, u.created_at "
        "FROM friendships f "
        "JOIN users u ON u.id = f.friend_id "
        "WHERE f.user_id = ? "
        "ORDER BY u.id ASC;");
    statement.bindInt64(1, static_cast<std::int64_t>(user_id));

    std::vector<User> friends;
    while (true) {
        const int rc = statement.step();
        if (rc == SQLITE_ROW) {
            friends.push_back(readUser(statement.get()));
            continue;
        }
        if (rc == SQLITE_DONE) {
            return friends;
        }
        throwSqlite(db_, "getFriends");
    }
}

std::optional<Group> SQLiteStorage::createGroup(const CreateGroupRequest& request) {
    if (request.name.empty() || !findUserById(request.owner_id)) {
        return std::nullopt;
    }

    executeSql("BEGIN IMMEDIATE;");
    try {
        Statement group_statement(
            db_,
            "INSERT INTO groups (name, owner_id) VALUES (?, ?);");
        group_statement.bindText(1, request.name);
        group_statement.bindInt64(2, static_cast<std::int64_t>(request.owner_id));
        int rc = group_statement.step();
        if (rc != SQLITE_DONE) {
            throwSqlite(db_, "createGroup");
        }

        const auto group_id = static_cast<GroupId>(sqlite3_last_insert_rowid(db_));

        Statement member_statement(
            db_,
            "INSERT INTO group_members (group_id, user_id) VALUES (?, ?);");
        member_statement.bindInt64(1, static_cast<std::int64_t>(group_id));
        member_statement.bindInt64(2, static_cast<std::int64_t>(request.owner_id));
        rc = member_statement.step();
        if (rc != SQLITE_DONE) {
            throwSqlite(db_, "createGroup owner member");
        }

        executeSql("COMMIT;");
        return findGroupById(group_id);
    } catch (...) {
        try {
            executeSql("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

bool SQLiteStorage::addGroupMember(GroupId group_id, UserId user_id) {
    if (!findGroupById(group_id) || !findUserById(user_id)) {
        return false;
    }

    Statement statement(
        db_,
        "INSERT OR IGNORE INTO group_members (group_id, user_id) VALUES (?, ?);");
    statement.bindInt64(1, static_cast<std::int64_t>(group_id));
    statement.bindInt64(2, static_cast<std::int64_t>(user_id));

    const int rc = statement.step();
    if (rc == SQLITE_DONE) {
        return true;
    }
    if (isConstraintFailure(rc)) {
        return false;
    }
    throwSqlite(db_, "addGroupMember");
}

bool SQLiteStorage::removeGroupMember(GroupId group_id, UserId user_id) {
    Statement statement(
        db_,
        "DELETE FROM group_members WHERE group_id = ? AND user_id = ?;");
    statement.bindInt64(1, static_cast<std::int64_t>(group_id));
    statement.bindInt64(2, static_cast<std::int64_t>(user_id));

    const int rc = statement.step();
    if (rc != SQLITE_DONE) {
        throwSqlite(db_, "removeGroupMember");
    }
    return sqlite3_changes(db_) > 0;
}

std::vector<UserId> SQLiteStorage::getGroupMembers(GroupId group_id) const {
    Statement statement(
        db_,
        "SELECT user_id FROM group_members WHERE group_id = ? ORDER BY user_id ASC;");
    statement.bindInt64(1, static_cast<std::int64_t>(group_id));

    std::vector<UserId> members;
    while (true) {
        const int rc = statement.step();
        if (rc == SQLITE_ROW) {
            members.push_back(static_cast<UserId>(sqlite3_column_int64(statement.get(), 0)));
            continue;
        }
        if (rc == SQLITE_DONE) {
            return members;
        }
        throwSqlite(db_, "getGroupMembers");
    }
}

std::optional<Group> SQLiteStorage::findGroupById(GroupId group_id) const {
    Statement statement(
        db_,
        "SELECT id, name, owner_id, created_at FROM groups WHERE id = ?;");
    statement.bindInt64(1, static_cast<std::int64_t>(group_id));

    const int rc = statement.step();
    if (rc == SQLITE_ROW) {
        return readGroup(statement.get());
    }
    if (rc == SQLITE_DONE) {
        return std::nullopt;
    }
    throwSqlite(db_, "findGroupById");
}

std::optional<PrivateMessage> SQLiteStorage::savePrivateMessage(
    const SavePrivateMessageRequest& request) {
    Statement statement(
        db_,
        "INSERT INTO messages "
        "(message_type, sender_id, receiver_id, group_id, body, created_at, delivered) "
        "VALUES (?, ?, ?, NULL, ?, ?, ?);");
    statement.bindInt(1, kPrivateMessageType);
    statement.bindInt64(2, static_cast<std::int64_t>(request.sender_id));
    statement.bindInt64(3, static_cast<std::int64_t>(request.receiver_id));
    statement.bindText(4, request.body);
    statement.bindInt64(5, request.created_at);
    statement.bindInt(6, request.delivered ? 1 : 0);

    const int rc = statement.step();
    if (rc == SQLITE_DONE) {
        const auto message_id = static_cast<MessageId>(sqlite3_last_insert_rowid(db_));
        Statement select_statement(
            db_,
            "SELECT id, sender_id, receiver_id, body, created_at, delivered "
            "FROM messages WHERE id = ?;");
        select_statement.bindInt64(1, static_cast<std::int64_t>(message_id));
        if (select_statement.step() == SQLITE_ROW) {
            return readPrivateMessage(select_statement.get());
        }
        return std::nullopt;
    }
    if (isConstraintFailure(rc)) {
        return std::nullopt;
    }
    throwSqlite(db_, "savePrivateMessage");
}

std::optional<GroupMessage> SQLiteStorage::saveGroupMessage(
    const SaveGroupMessageRequest& request) {
    Statement statement(
        db_,
        "INSERT INTO messages "
        "(message_type, sender_id, receiver_id, group_id, body, created_at, delivered) "
        "VALUES (?, ?, NULL, ?, ?, ?, 1);");
    statement.bindInt(1, kGroupMessageType);
    statement.bindInt64(2, static_cast<std::int64_t>(request.sender_id));
    statement.bindInt64(3, static_cast<std::int64_t>(request.group_id));
    statement.bindText(4, request.body);
    statement.bindInt64(5, request.created_at);

    const int rc = statement.step();
    if (rc == SQLITE_DONE) {
        const auto message_id = static_cast<MessageId>(sqlite3_last_insert_rowid(db_));
        Statement select_statement(
            db_,
            "SELECT id, group_id, sender_id, body, created_at "
            "FROM messages WHERE id = ?;");
        select_statement.bindInt64(1, static_cast<std::int64_t>(message_id));
        if (select_statement.step() == SQLITE_ROW) {
            return readGroupMessage(select_statement.get());
        }
        return std::nullopt;
    }
    if (isConstraintFailure(rc)) {
        return std::nullopt;
    }
    throwSqlite(db_, "saveGroupMessage");
}

std::vector<PrivateMessage> SQLiteStorage::getPrivateHistory(
    UserId first_user_id,
    UserId second_user_id,
    std::size_t limit,
    std::size_t offset) const {
    Statement statement(
        db_,
        "SELECT id, sender_id, receiver_id, body, created_at, delivered "
        "FROM messages "
        "WHERE message_type = ? "
        "AND ((sender_id = ? AND receiver_id = ?) OR (sender_id = ? AND receiver_id = ?)) "
        "ORDER BY created_at ASC, id ASC "
        "LIMIT ? OFFSET ?;");
    statement.bindInt(1, kPrivateMessageType);
    statement.bindInt64(2, static_cast<std::int64_t>(first_user_id));
    statement.bindInt64(3, static_cast<std::int64_t>(second_user_id));
    statement.bindInt64(4, static_cast<std::int64_t>(second_user_id));
    statement.bindInt64(5, static_cast<std::int64_t>(first_user_id));
    statement.bindInt64(6, sqlLimit(limit));
    statement.bindInt64(7, sqlOffset(offset));

    std::vector<PrivateMessage> messages;
    while (true) {
        const int rc = statement.step();
        if (rc == SQLITE_ROW) {
            messages.push_back(readPrivateMessage(statement.get()));
            continue;
        }
        if (rc == SQLITE_DONE) {
            return messages;
        }
        throwSqlite(db_, "getPrivateHistory");
    }
}

std::vector<GroupMessage> SQLiteStorage::getGroupHistory(
    GroupId group_id,
    std::size_t limit,
    std::size_t offset) const {
    Statement statement(
        db_,
        "SELECT id, group_id, sender_id, body, created_at "
        "FROM messages "
        "WHERE message_type = ? AND group_id = ? "
        "ORDER BY created_at ASC, id ASC "
        "LIMIT ? OFFSET ?;");
    statement.bindInt(1, kGroupMessageType);
    statement.bindInt64(2, static_cast<std::int64_t>(group_id));
    statement.bindInt64(3, sqlLimit(limit));
    statement.bindInt64(4, sqlOffset(offset));

    std::vector<GroupMessage> messages;
    while (true) {
        const int rc = statement.step();
        if (rc == SQLITE_ROW) {
            messages.push_back(readGroupMessage(statement.get()));
            continue;
        }
        if (rc == SQLITE_DONE) {
            return messages;
        }
        throwSqlite(db_, "getGroupHistory");
    }
}

std::vector<PrivateMessage> SQLiteStorage::getOfflineMessages(
    UserId user_id,
    std::size_t limit) const {
    Statement statement(
        db_,
        "SELECT id, sender_id, receiver_id, body, created_at, delivered "
        "FROM messages "
        "WHERE message_type = ? AND receiver_id = ? AND delivered = 0 "
        "ORDER BY created_at ASC, id ASC "
        "LIMIT ?;");
    statement.bindInt(1, kPrivateMessageType);
    statement.bindInt64(2, static_cast<std::int64_t>(user_id));
    statement.bindInt64(3, sqlLimit(limit));

    std::vector<PrivateMessage> messages;
    while (true) {
        const int rc = statement.step();
        if (rc == SQLITE_ROW) {
            messages.push_back(readPrivateMessage(statement.get()));
            continue;
        }
        if (rc == SQLITE_DONE) {
            return messages;
        }
        throwSqlite(db_, "getOfflineMessages");
    }
}

void SQLiteStorage::executeSql(const std::string& sql) const {
    char* error_message = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_message);
    if (rc != SQLITE_OK) {
        std::string message = error_message == nullptr ? sqlite3_errmsg(db_) : error_message;
        sqlite3_free(error_message);
        throw std::runtime_error("sqlite3_exec failed: " + message);
    }
}

void SQLiteStorage::executeSchemaFile(const std::string& schema_path) const {
    executeSql(readFile(schema_path));
}

}  // namespace liteim::storage
