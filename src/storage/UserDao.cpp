#include "liteim/storage/UserDao.hpp"

#include "liteim/base/Timestamp.hpp"
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"

#include <stdexcept>
#include <utility>

namespace liteim {
namespace {

constexpr unsigned int kMysqlDuplicateEntry = 1062;

Status malformedUserRowStatus() {
    return Status::error(ErrorCode::InternalError, "users query returned a malformed row");
}

Status requiredValue(const MySqlRow& row, std::size_t index, const std::string*& value) {
    if (index >= row.values.size() || !row.values[index].has_value()) {
        return malformedUserRowStatus();
    }
    value = &(*row.values[index]);
    return Status::ok();
}

Status parseUint64(const std::string& text, std::uint64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto result = std::stoull(text, &parsed, 10);
        if (parsed != text.size()) {
            return malformedUserRowStatus();
        }
        value = static_cast<std::uint64_t>(result);
        return Status::ok();
    } catch (const std::exception&) {
        return malformedUserRowStatus();
    }
}

Status parseInt64(const std::string& text, std::int64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto result = std::stoll(text, &parsed, 10);
        if (parsed != text.size()) {
            return malformedUserRowStatus();
        }
        value = static_cast<std::int64_t>(result);
        return Status::ok();
    } catch (const std::exception&) {
        return malformedUserRowStatus();
    }
}

Status rowToUserRecord(const MySqlRow& row, UserRecord& user) {
    if (row.values.size() != 6U) {
        return malformedUserRowStatus();
    }

    const std::string* user_id = nullptr;
    const std::string* username = nullptr;
    const std::string* password_hash = nullptr;
    const std::string* password_salt = nullptr;
    const std::string* nickname = nullptr;
    const std::string* created_at_ms = nullptr;

    const auto user_id_status = requiredValue(row, 0, user_id);
    if (!user_id_status.isOk()) {
        return user_id_status;
    }
    const auto username_status = requiredValue(row, 1, username);
    if (!username_status.isOk()) {
        return username_status;
    }
    const auto password_hash_status = requiredValue(row, 2, password_hash);
    if (!password_hash_status.isOk()) {
        return password_hash_status;
    }
    const auto password_salt_status = requiredValue(row, 3, password_salt);
    if (!password_salt_status.isOk()) {
        return password_salt_status;
    }
    const auto nickname_status = requiredValue(row, 4, nickname);
    if (!nickname_status.isOk()) {
        return nickname_status;
    }
    const auto created_at_status = requiredValue(row, 5, created_at_ms);
    if (!created_at_status.isOk()) {
        return created_at_status;
    }

    UserRecord parsed_user;
    const auto id_status = parseUint64(*user_id, parsed_user.user_id);
    if (!id_status.isOk()) {
        return id_status;
    }
    const auto created_status = parseInt64(*created_at_ms, parsed_user.created_at_ms);
    if (!created_status.isOk()) {
        return created_status;
    }

    parsed_user.username = *username;
    parsed_user.password_hash = *password_hash;
    parsed_user.password_salt = *password_salt;
    parsed_user.nickname = *nickname;

    user = std::move(parsed_user);
    return Status::ok();
}

Status bindUserId(PreparedStatement& statement, std::uint64_t user_id) {
    return statement.bindUInt64(0, user_id);
}

Status querySingleUser(PreparedStatement& statement, UserRecord& user) {
    MySqlQueryResult result;
    const auto query_status = statement.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }
    if (result.rows().empty()) {
        return Status::error(ErrorCode::NotFound, "user was not found");
    }
    if (result.rows().size() != 1U) {
        return Status::error(ErrorCode::InternalError, "users query returned multiple rows");
    }
    return rowToUserRecord(result.rows().front(), user);
}

} // namespace

UserDao::UserDao(MySqlPool& pool, std::chrono::milliseconds acquire_timeout)
    : pool_(&pool), acquire_timeout_(acquire_timeout) {
}

Status UserDao::createUser(const CreateUserRequest& request, UserRecord& created_user) {
    const auto now_ms = Timestamp::now().millisecondsSinceEpoch();
    {
        ConnectionGuard guard;
        const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
        if (!acquire_status.isOk()) {
            return acquire_status;
        }

        PreparedStatement statement(*guard);
        const auto prepare_status =
            statement.prepare("INSERT INTO users "
                              "(username, password_hash, password_salt, nickname, created_at_ms, updated_at_ms) "
                              "VALUES (?, ?, ?, ?, ?, ?)");
        if (!prepare_status.isOk()) {
            return prepare_status;
        }

        const auto username_status = statement.bindString(0, request.username);
        if (!username_status.isOk()) {
            return username_status;
        }
        const auto hash_status = statement.bindString(1, request.password_hash);
        if (!hash_status.isOk()) {
            return hash_status;
        }
        const auto salt_status = statement.bindString(2, request.password_salt);
        if (!salt_status.isOk()) {
            return salt_status;
        }
        const auto nickname_status = statement.bindString(3, request.nickname);
        if (!nickname_status.isOk()) {
            return nickname_status;
        }
        const auto created_status = statement.bindInt64(4, now_ms);
        if (!created_status.isOk()) {
            return created_status;
        }
        const auto updated_status = statement.bindInt64(5, now_ms);
        if (!updated_status.isOk()) {
            return updated_status;
        }

        std::uint64_t affected_rows = 0;
        const auto insert_status = statement.executeUpdate(affected_rows);
        if (!insert_status.isOk()) {
            if (statement.lastErrorNumber() == kMysqlDuplicateEntry) {
                return Status::error(ErrorCode::AlreadyExists, "username already exists");
            }
            return insert_status;
        }
        if (affected_rows != 1U) {
            return Status::error(ErrorCode::InternalError, "create user affected unexpected row count");
        }
    }

    return findUserByUsername(request.username, created_user);
}

Status UserDao::findUserByUsername(const std::string& username, UserRecord& user) {
    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("SELECT user_id, username, password_hash, password_salt, nickname, created_at_ms "
                          "FROM users WHERE username = ? LIMIT 1");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto bind_status = statement.bindString(0, username);
    if (!bind_status.isOk()) {
        return bind_status;
    }

    return querySingleUser(statement, user);
}

Status UserDao::findUserById(std::uint64_t user_id, UserRecord& user) {
    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("SELECT user_id, username, password_hash, password_salt, nickname, created_at_ms "
                          "FROM users WHERE user_id = ? LIMIT 1");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto bind_status = bindUserId(statement, user_id);
    if (!bind_status.isOk()) {
        return bind_status;
    }

    return querySingleUser(statement, user);
}

} // namespace liteim
