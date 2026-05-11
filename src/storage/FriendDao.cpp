#include "liteim/storage/FriendDao.hpp"

#include "liteim/base/Timestamp.hpp"
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace liteim {
namespace {

Status malformedFriendRowStatus() {
    return Status::error(ErrorCode::InternalError, "friendships query returned a malformed row");
}

Status requiredValue(const MySqlRow& row, std::size_t index, const std::string*& value) {
    if (index >= row.values.size() || !row.values[index].has_value()) {
        return malformedFriendRowStatus();
    }
    value = &(*row.values[index]);
    return Status::ok();
}

Status parseUint64(const std::string& text, std::uint64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto result = std::stoull(text, &parsed, 10);
        if (parsed != text.size()) {
            return malformedFriendRowStatus();
        }
        value = static_cast<std::uint64_t>(result);
        return Status::ok();
    } catch (const std::exception&) {
        return malformedFriendRowStatus();
    }
}

Status parseInt64(const std::string& text, std::int64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto result = std::stoll(text, &parsed, 10);
        if (parsed != text.size()) {
            return malformedFriendRowStatus();
        }
        value = static_cast<std::int64_t>(result);
        return Status::ok();
    } catch (const std::exception&) {
        return malformedFriendRowStatus();
    }
}

Status validateUserId(std::uint64_t user_id, const std::string& field_name) {
    if (user_id == 0U) {
        return Status::error(ErrorCode::InvalidArgument, field_name + " must not be zero");
    }
    if (user_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return Status::error(ErrorCode::InvalidArgument,
                             field_name + " exceeds supported MySQL signed bind range");
    }
    return Status::ok();
}

Status bindUserId(PreparedStatement& statement,
                  std::size_t index,
                  std::uint64_t user_id,
                  const std::string& field_name) {
    const auto validate_status = validateUserId(user_id, field_name);
    if (!validate_status.isOk()) {
        return validate_status;
    }
    return statement.bindInt64(index, static_cast<std::int64_t>(user_id));
}

void rollbackSilently(MySqlConnection& connection) noexcept {
    (void)connection.executeSimple("ROLLBACK");
}

Status rowToUserRecord(const MySqlRow& row, UserRecord& user) {
    if (row.values.size() != 6U) {
        return malformedFriendRowStatus();
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
    const auto hash_status = requiredValue(row, 2, password_hash);
    if (!hash_status.isOk()) {
        return hash_status;
    }
    const auto salt_status = requiredValue(row, 3, password_salt);
    if (!salt_status.isOk()) {
        return salt_status;
    }
    const auto nickname_status = requiredValue(row, 4, nickname);
    if (!nickname_status.isOk()) {
        return nickname_status;
    }
    const auto created_status = requiredValue(row, 5, created_at_ms);
    if (!created_status.isOk()) {
        return created_status;
    }

    UserRecord parsed_user;
    const auto id_status = parseUint64(*user_id, parsed_user.user_id);
    if (!id_status.isOk()) {
        return id_status;
    }
    const auto created_parse_status = parseInt64(*created_at_ms, parsed_user.created_at_ms);
    if (!created_parse_status.isOk()) {
        return created_parse_status;
    }

    parsed_user.username = *username;
    parsed_user.password_hash = *password_hash;
    parsed_user.password_salt = *password_salt;
    parsed_user.nickname = *nickname;

    user = std::move(parsed_user);
    return Status::ok();
}

} // namespace

FriendDao::FriendDao(MySqlPool& pool, std::chrono::milliseconds acquire_timeout)
    : pool_(&pool), acquire_timeout_(acquire_timeout) {
}

Status FriendDao::addFriendship(std::uint64_t user_id, std::uint64_t friend_id) {
    const auto user_status = validateUserId(user_id, "user_id");
    if (!user_status.isOk()) {
        return user_status;
    }
    const auto friend_status = validateUserId(friend_id, "friend_id");
    if (!friend_status.isOk()) {
        return friend_status;
    }
    if (user_id == friend_id) {
        return Status::error(ErrorCode::InvalidArgument, "friendship user ids must be different");
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    const auto begin_status = guard->executeSimple("START TRANSACTION");
    if (!begin_status.isOk()) {
        return begin_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("INSERT INTO friendships (user_id, friend_id, created_at_ms) "
                          "VALUES (?, ?, ?), (?, ?, ?) "
                          "ON DUPLICATE KEY UPDATE created_at_ms = friendships.created_at_ms");
    if (!prepare_status.isOk()) {
        rollbackSilently(*guard);
        return prepare_status;
    }

    const auto now_ms = Timestamp::now().millisecondsSinceEpoch();
    const auto first_user_status = bindUserId(statement, 0, user_id, "user_id");
    if (!first_user_status.isOk()) {
        rollbackSilently(*guard);
        return first_user_status;
    }
    const auto first_friend_status = bindUserId(statement, 1, friend_id, "friend_id");
    if (!first_friend_status.isOk()) {
        rollbackSilently(*guard);
        return first_friend_status;
    }
    const auto first_created_status = statement.bindInt64(2, now_ms);
    if (!first_created_status.isOk()) {
        rollbackSilently(*guard);
        return first_created_status;
    }
    const auto second_user_status = bindUserId(statement, 3, friend_id, "friend_id");
    if (!second_user_status.isOk()) {
        rollbackSilently(*guard);
        return second_user_status;
    }
    const auto second_friend_status = bindUserId(statement, 4, user_id, "user_id");
    if (!second_friend_status.isOk()) {
        rollbackSilently(*guard);
        return second_friend_status;
    }
    const auto second_created_status = statement.bindInt64(5, now_ms);
    if (!second_created_status.isOk()) {
        rollbackSilently(*guard);
        return second_created_status;
    }

    std::uint64_t affected_rows = 0;
    const auto insert_status = statement.executeUpdate(affected_rows);
    if (!insert_status.isOk()) {
        rollbackSilently(*guard);
        return insert_status;
    }

    statement.close();
    return guard->executeSimple("COMMIT");
}

Status FriendDao::getFriends(std::uint64_t user_id, std::vector<UserRecord>& friends) {
    friends.clear();

    const auto user_status = validateUserId(user_id, "user_id");
    if (!user_status.isOk()) {
        return user_status;
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("SELECT u.user_id, u.username, u.password_hash, u.password_salt, "
                          "u.nickname, u.created_at_ms "
                          "FROM friendships f "
                          "JOIN users u ON u.user_id = f.friend_id "
                          "WHERE f.user_id = ? "
                          "ORDER BY u.user_id ASC");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto bind_status = bindUserId(statement, 0, user_id, "user_id");
    if (!bind_status.isOk()) {
        return bind_status;
    }

    MySqlQueryResult result;
    const auto query_status = statement.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }

    std::vector<UserRecord> parsed_friends;
    parsed_friends.reserve(result.rows().size());
    for (const auto& row : result.rows()) {
        UserRecord user;
        const auto row_status = rowToUserRecord(row, user);
        if (!row_status.isOk()) {
            return row_status;
        }
        parsed_friends.push_back(std::move(user));
    }

    friends = std::move(parsed_friends);
    return Status::ok();
}

} // namespace liteim
