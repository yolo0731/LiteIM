#include "liteim/storage/FriendDao.hpp"

#include "liteim/base/Timestamp.hpp"
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"

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

Status parseFriendRequestStatus(const std::string& text, FriendRequestStatus& status) {
    std::uint64_t raw = 0;
    const auto parse_status = parseUint64(text, raw);
    if (!parse_status.isOk()) {
        return parse_status;
    }
    if (raw == static_cast<std::uint64_t>(FriendRequestStatus::kPending)) {
        status = FriendRequestStatus::kPending;
        return Status::ok();
    }
    if (raw == static_cast<std::uint64_t>(FriendRequestStatus::kAccepted)) {
        status = FriendRequestStatus::kAccepted;
        return Status::ok();
    }
    if (raw == static_cast<std::uint64_t>(FriendRequestStatus::kRejected)) {
        status = FriendRequestStatus::kRejected;
        return Status::ok();
    }
    return malformedFriendRowStatus();
}

Status validateUserId(std::uint64_t user_id, const std::string& field_name) {
    if (user_id == 0U) {
        return Status::error(ErrorCode::InvalidArgument, field_name + " must not be zero");
    }
    return Status::ok();
}

Status bindUserId(PreparedStatement& statement, std::size_t index, std::uint64_t user_id,
                  const std::string& field_name) {
    const auto validate_status = validateUserId(user_id, field_name);
    if (!validate_status.isOk()) {
        return validate_status;
    }
    return statement.bindUInt64(index, user_id);
}

void rollbackSilently(MySqlConnection& connection) noexcept {
    (void)connection.executeSimple("ROLLBACK");
}

Status rollbackAndReturn(MySqlConnection& connection, const Status& status) {
    rollbackSilently(connection);
    return status;
}

Status validateFriendPair(std::uint64_t user_id, std::uint64_t friend_id) {
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
    return Status::ok();
}

Status rowToFriendRequestRecord(const MySqlRow& row, FriendRequestRecord& request) {
    if (row.values.size() != 5U) {
        return malformedFriendRowStatus();
    }

    const std::string* requester_id = nullptr;
    const std::string* target_user_id = nullptr;
    const std::string* status = nullptr;
    const std::string* created_at_ms = nullptr;
    const std::string* updated_at_ms = nullptr;

    const auto requester_status = requiredValue(row, 0, requester_id);
    if (!requester_status.isOk()) {
        return requester_status;
    }
    const auto target_status = requiredValue(row, 1, target_user_id);
    if (!target_status.isOk()) {
        return target_status;
    }
    const auto status_status = requiredValue(row, 2, status);
    if (!status_status.isOk()) {
        return status_status;
    }
    const auto created_status = requiredValue(row, 3, created_at_ms);
    if (!created_status.isOk()) {
        return created_status;
    }
    const auto updated_status = requiredValue(row, 4, updated_at_ms);
    if (!updated_status.isOk()) {
        return updated_status;
    }

    FriendRequestRecord parsed_request;
    const auto requester_parse = parseUint64(*requester_id, parsed_request.requester_id);
    if (!requester_parse.isOk()) {
        return requester_parse;
    }
    const auto target_parse = parseUint64(*target_user_id, parsed_request.target_user_id);
    if (!target_parse.isOk()) {
        return target_parse;
    }
    const auto request_status_parse = parseFriendRequestStatus(*status, parsed_request.status);
    if (!request_status_parse.isOk()) {
        return request_status_parse;
    }
    const auto created_parse = parseInt64(*created_at_ms, parsed_request.created_at_ms);
    if (!created_parse.isOk()) {
        return created_parse;
    }
    const auto updated_parse = parseInt64(*updated_at_ms, parsed_request.updated_at_ms);
    if (!updated_parse.isOk()) {
        return updated_parse;
    }

    request = parsed_request;
    return Status::ok();
}

Status queryFriendRequest(MySqlConnection& connection, std::uint64_t requester_id,
                          std::uint64_t target_user_id, FriendRequestRecord& request) {
    request = {};
    PreparedStatement statement(connection);
    const auto prepare_status =
        statement.prepare("SELECT requester_id, target_user_id, status, created_at_ms, "
                          "updated_at_ms "
                          "FROM friend_requests "
                          "WHERE requester_id = ? AND target_user_id = ? LIMIT 1");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto requester_status = statement.bindUInt64(0, requester_id);
    if (!requester_status.isOk()) {
        return requester_status;
    }
    const auto target_status = statement.bindUInt64(1, target_user_id);
    if (!target_status.isOk()) {
        return target_status;
    }

    MySqlQueryResult result;
    const auto query_status = statement.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }
    if (result.rows().empty()) {
        return Status::error(ErrorCode::NotFound, "friend request was not found");
    }
    if (result.rows().size() != 1U) {
        return Status::error(ErrorCode::InternalError,
                             "friend request query returned multiple rows");
    }
    return rowToFriendRequestRecord(result.rows().front(), request);
}

Status queryAreFriends(MySqlConnection& connection, std::uint64_t user_id,
                       std::uint64_t friend_id, bool& are_friends) {
    are_friends = false;
    PreparedStatement statement(connection);
    const auto prepare_status = statement.prepare(
        "SELECT 1 FROM friendships WHERE user_id = ? AND friend_id = ? LIMIT 1");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto user_status = statement.bindUInt64(0, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }
    const auto friend_status = statement.bindUInt64(1, friend_id);
    if (!friend_status.isOk()) {
        return friend_status;
    }

    MySqlQueryResult result;
    const auto query_status = statement.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }
    are_friends = !result.rows().empty();
    return Status::ok();
}

Status insertFriendshipRows(MySqlConnection& connection, std::uint64_t user_id,
                            std::uint64_t friend_id, std::int64_t created_at_ms) {
    PreparedStatement statement(connection);
    const auto prepare_status =
        statement.prepare("INSERT INTO friendships (user_id, friend_id, created_at_ms) "
                          "VALUES (?, ?, ?), (?, ?, ?) "
                          "ON DUPLICATE KEY UPDATE created_at_ms = friendships.created_at_ms");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto first_user_status = bindUserId(statement, 0, user_id, "user_id");
    if (!first_user_status.isOk()) {
        return first_user_status;
    }
    const auto first_friend_status = bindUserId(statement, 1, friend_id, "friend_id");
    if (!first_friend_status.isOk()) {
        return first_friend_status;
    }
    const auto first_created_status = statement.bindInt64(2, created_at_ms);
    if (!first_created_status.isOk()) {
        return first_created_status;
    }
    const auto second_user_status = bindUserId(statement, 3, friend_id, "friend_id");
    if (!second_user_status.isOk()) {
        return second_user_status;
    }
    const auto second_friend_status = bindUserId(statement, 4, user_id, "user_id");
    if (!second_friend_status.isOk()) {
        return second_friend_status;
    }
    const auto second_created_status = statement.bindInt64(5, created_at_ms);
    if (!second_created_status.isOk()) {
        return second_created_status;
    }

    std::uint64_t affected_rows = 0;
    const auto insert_status = statement.executeUpdate(affected_rows);
    if (!insert_status.isOk()) {
        return insert_status;
    }
    if (affected_rows > 2U) {
        return Status::error(ErrorCode::InternalError,
                             "add friendship affected unexpected row count");
    }
    return Status::ok();
}

Status rowToUserProfileRecord(const MySqlRow& row, UserProfileRecord& user) {
    if (row.values.size() != 4U) {
        return malformedFriendRowStatus();
    }

    const std::string* user_id = nullptr;
    const std::string* username = nullptr;
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
    const auto nickname_status = requiredValue(row, 2, nickname);
    if (!nickname_status.isOk()) {
        return nickname_status;
    }
    const auto created_status = requiredValue(row, 3, created_at_ms);
    if (!created_status.isOk()) {
        return created_status;
    }

    UserProfileRecord parsed_user;
    const auto id_status = parseUint64(*user_id, parsed_user.user_id);
    if (!id_status.isOk()) {
        return id_status;
    }
    const auto created_parse_status = parseInt64(*created_at_ms, parsed_user.created_at_ms);
    if (!created_parse_status.isOk()) {
        return created_parse_status;
    }

    parsed_user.username = *username;
    parsed_user.nickname = *nickname;

    user = std::move(parsed_user);
    return Status::ok();
}

}  // namespace

FriendDao::FriendDao(MySqlPool& pool, std::chrono::milliseconds acquire_timeout)
    : pool_(&pool), acquire_timeout_(acquire_timeout) {}

Status FriendDao::createFriendRequest(std::uint64_t requester_id, std::uint64_t target_user_id,
                                      FriendRequestRecord& request) {
    request = {};
    const auto pair_status = validateFriendPair(requester_id, target_user_id);
    if (!pair_status.isOk()) {
        return pair_status;
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    bool are_friends = false;
    const auto friends_status =
        queryAreFriends(*guard, requester_id, target_user_id, are_friends);
    if (!friends_status.isOk()) {
        return friends_status;
    }
    if (are_friends) {
        return Status::error(ErrorCode::AlreadyExists, "friendship already exists");
    }

    FriendRequestRecord existing;
    const auto existing_status = queryFriendRequest(*guard, requester_id, target_user_id, existing);
    if (existing_status.isOk()) {
        return Status::error(ErrorCode::AlreadyExists, "friend request already exists");
    }
    if (existing_status.code() != ErrorCode::NotFound) {
        return existing_status;
    }

    FriendRequestRecord reverse_existing;
    const auto reverse_status =
        queryFriendRequest(*guard, target_user_id, requester_id, reverse_existing);
    if (reverse_status.isOk()) {
        if (reverse_existing.status == FriendRequestStatus::kPending) {
            return Status::error(ErrorCode::AlreadyExists,
                                 "reverse friend request already exists");
        }
        if (reverse_existing.status == FriendRequestStatus::kAccepted) {
            return Status::error(ErrorCode::AlreadyExists, "friend request already accepted");
        }
    } else if (reverse_status.code() != ErrorCode::NotFound) {
        return reverse_status;
    }

    const auto now_ms = Timestamp::now().millisecondsSinceEpoch();
    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("INSERT INTO friend_requests "
                          "(requester_id, target_user_id, status, created_at_ms, updated_at_ms) "
                          "VALUES (?, ?, 0, ?, ?)");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto requester_status = statement.bindUInt64(0, requester_id);
    if (!requester_status.isOk()) {
        return requester_status;
    }
    const auto target_status = statement.bindUInt64(1, target_user_id);
    if (!target_status.isOk()) {
        return target_status;
    }
    const auto created_status = statement.bindInt64(2, now_ms);
    if (!created_status.isOk()) {
        return created_status;
    }
    const auto updated_status = statement.bindInt64(3, now_ms);
    if (!updated_status.isOk()) {
        return updated_status;
    }

    std::uint64_t affected_rows = 0;
    const auto insert_status = statement.executeUpdate(affected_rows);
    if (!insert_status.isOk()) {
        return insert_status;
    }
    if (affected_rows != 1U) {
        return Status::error(ErrorCode::InternalError,
                             "create friend request affected unexpected row count");
    }

    request = FriendRequestRecord{requester_id, target_user_id, FriendRequestStatus::kPending,
                                  now_ms, now_ms};
    return Status::ok();
}

Status FriendDao::acceptFriendRequest(std::uint64_t requester_id,
                                      std::uint64_t target_user_id) {
    const auto pair_status = validateFriendPair(requester_id, target_user_id);
    if (!pair_status.isOk()) {
        return pair_status;
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

    FriendRequestRecord existing;
    const auto existing_status = queryFriendRequest(*guard, requester_id, target_user_id, existing);
    if (!existing_status.isOk()) {
        if (existing_status.code() == ErrorCode::NotFound) {
            bool are_friends = false;
            const auto friends_status =
                queryAreFriends(*guard, requester_id, target_user_id, are_friends);
            if (!friends_status.isOk()) {
                return rollbackAndReturn(*guard, friends_status);
            }
            if (are_friends) {
                return rollbackAndReturn(
                    *guard,
                    Status::error(ErrorCode::AlreadyExists, "friend request already accepted"));
            }
        }
        return rollbackAndReturn(*guard, existing_status);
    }
    if (existing.status == FriendRequestStatus::kAccepted) {
        return rollbackAndReturn(
            *guard, Status::error(ErrorCode::AlreadyExists, "friend request already accepted"));
    }
    if (existing.status == FriendRequestStatus::kRejected) {
        return rollbackAndReturn(
            *guard, Status::error(ErrorCode::AlreadyExists, "friend request is not pending"));
    }

    const auto now_ms = Timestamp::now().millisecondsSinceEpoch();
    PreparedStatement update(*guard);
    const auto prepare_status = update.prepare(
        "UPDATE friend_requests SET status = 1, updated_at_ms = ? "
        "WHERE requester_id = ? AND target_user_id = ? AND status = 0");
    if (!prepare_status.isOk()) {
        return rollbackAndReturn(*guard, prepare_status);
    }
    const auto updated_status = update.bindInt64(0, now_ms);
    if (!updated_status.isOk()) {
        return rollbackAndReturn(*guard, updated_status);
    }
    const auto requester_status = update.bindUInt64(1, requester_id);
    if (!requester_status.isOk()) {
        return rollbackAndReturn(*guard, requester_status);
    }
    const auto target_status = update.bindUInt64(2, target_user_id);
    if (!target_status.isOk()) {
        return rollbackAndReturn(*guard, target_status);
    }

    std::uint64_t affected_rows = 0;
    const auto update_status = update.executeUpdate(affected_rows);
    if (!update_status.isOk()) {
        return rollbackAndReturn(*guard, update_status);
    }
    if (affected_rows != 1U) {
        return rollbackAndReturn(
            *guard,
            Status::error(ErrorCode::InternalError,
                          "accept friend request affected unexpected row count"));
    }
    update.close();

    const auto friendship_status =
        insertFriendshipRows(*guard, requester_id, target_user_id, now_ms);
    if (!friendship_status.isOk()) {
        return rollbackAndReturn(*guard, friendship_status);
    }

    return guard->executeSimple("COMMIT");
}

Status FriendDao::rejectFriendRequest(std::uint64_t requester_id,
                                      std::uint64_t target_user_id) {
    const auto pair_status = validateFriendPair(requester_id, target_user_id);
    if (!pair_status.isOk()) {
        return pair_status;
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    FriendRequestRecord existing;
    const auto existing_status = queryFriendRequest(*guard, requester_id, target_user_id, existing);
    if (!existing_status.isOk()) {
        return existing_status;
    }
    if (existing.status == FriendRequestStatus::kAccepted) {
        return Status::error(ErrorCode::AlreadyExists, "friend request already accepted");
    }
    if (existing.status == FriendRequestStatus::kRejected) {
        return Status::error(ErrorCode::AlreadyExists, "friend request already rejected");
    }

    PreparedStatement update(*guard);
    const auto prepare_status = update.prepare(
        "UPDATE friend_requests SET status = 2, updated_at_ms = ? "
        "WHERE requester_id = ? AND target_user_id = ? AND status = 0");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto updated_status =
        update.bindInt64(0, Timestamp::now().millisecondsSinceEpoch());
    if (!updated_status.isOk()) {
        return updated_status;
    }
    const auto requester_status = update.bindUInt64(1, requester_id);
    if (!requester_status.isOk()) {
        return requester_status;
    }
    const auto target_status = update.bindUInt64(2, target_user_id);
    if (!target_status.isOk()) {
        return target_status;
    }

    std::uint64_t affected_rows = 0;
    const auto update_status = update.executeUpdate(affected_rows);
    if (!update_status.isOk()) {
        return update_status;
    }
    if (affected_rows != 1U) {
        return Status::error(ErrorCode::InternalError,
                             "reject friend request affected unexpected row count");
    }
    return Status::ok();
}

Status FriendDao::areFriends(std::uint64_t user_id, std::uint64_t friend_id,
                             bool& are_friends) {
    are_friends = false;
    const auto pair_status = validateFriendPair(user_id, friend_id);
    if (!pair_status.isOk()) {
        return pair_status;
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }
    return queryAreFriends(*guard, user_id, friend_id, are_friends);
}

Status FriendDao::addFriendship(std::uint64_t user_id, std::uint64_t friend_id) {
    const auto pair_status = validateFriendPair(user_id, friend_id);
    if (!pair_status.isOk()) {
        return pair_status;
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
    const auto now_ms = Timestamp::now().millisecondsSinceEpoch();
    const auto insert_status = insertFriendshipRows(*guard, user_id, friend_id, now_ms);
    if (!insert_status.isOk()) {
        return rollbackAndReturn(*guard, insert_status);
    }
    return guard->executeSimple("COMMIT");
}

Status FriendDao::getFriends(std::uint64_t user_id, std::vector<UserProfileRecord>& friends) {
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
        statement.prepare("SELECT u.user_id, u.username, u.nickname, u.created_at_ms "
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

    std::vector<UserProfileRecord> parsed_friends;
    parsed_friends.reserve(result.rows().size());
    for (const auto& row : result.rows()) {
        UserProfileRecord user;
        const auto row_status = rowToUserProfileRecord(row, user);
        if (!row_status.isOk()) {
            return row_status;
        }
        parsed_friends.push_back(std::move(user));
    }

    friends = std::move(parsed_friends);
    return Status::ok();
}

}  // namespace liteim
