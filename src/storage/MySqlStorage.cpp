#include "liteim/storage/MySqlStorage.hpp"

#include "liteim/base/Timestamp.hpp"
#include "liteim/storage/MySqlConnection.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace liteim {
namespace {

void rollbackSilently(MySqlConnection& connection) noexcept {
    (void)connection.executeSimple("ROLLBACK");
}

Status rollbackClearAndReturn(MySqlConnection& connection, MessageRecord& saved_message,
                              const Status& status) {
    rollbackSilently(connection);
    saved_message = {};
    return status;
}
// 验证离线消息的接收者列表是否合法，要求列表中的 user_id 都不为零，并且去重。
Status validateOfflineRecipients(const std::vector<std::uint64_t>& offline_user_ids,
                                 std::vector<std::uint64_t>& unique_user_ids) {
    unique_user_ids.clear();
    unique_user_ids.reserve(offline_user_ids.size());

    for (const auto user_id : offline_user_ids) {
        if (user_id == 0U) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "offline recipient user_id must not be zero");
        }
        if (std::find(unique_user_ids.begin(), unique_user_ids.end(), user_id) ==
            unique_user_ids.end()) {
            unique_user_ids.push_back(user_id);
        }
    }
    return Status::ok();
}
// 往 offline_messages 表插入一条记录
Status insertOfflineMessage(MySqlConnection& connection, std::uint64_t user_id,
                            std::uint64_t message_id, std::int64_t created_at_ms) {
    PreparedStatement statement(connection);
    const auto prepare_status =
        statement.prepare("INSERT INTO offline_messages "
                          "(user_id, message_id, delivered, created_at_ms, delivered_at_ms) "
                          "VALUES (?, ?, 0, ?, NULL)");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto user_status = statement.bindUInt64(0, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }
    const auto message_status = statement.bindUInt64(1, message_id);
    if (!message_status.isOk()) {
        return message_status;
    }
    const auto created_status = statement.bindInt64(2, created_at_ms);
    if (!created_status.isOk()) {
        return created_status;
    }

    std::uint64_t affected_rows = 0;
    const auto insert_status = statement.executeUpdate(affected_rows);
    if (!insert_status.isOk()) {
        return insert_status;
    }
    if (affected_rows != 1U) {
        return Status::error(ErrorCode::InternalError,
                             "save offline message affected unexpected row count");
    }
    return Status::ok();
}

Status upsertDeliveryPending(MySqlConnection& connection, std::uint64_t user_id,
                             std::uint64_t message_id) {
    PreparedStatement statement(connection);
    const auto prepare_status = statement.prepare(
        "INSERT INTO message_deliveries "
        "(message_id, user_id, status, pushed_at_ms, delivered_at_ms, read_at_ms) "
        "VALUES (?, ?, 0, NULL, NULL, NULL) "
        "ON DUPLICATE KEY UPDATE status = status");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto message_status = statement.bindUInt64(0, message_id);
    if (!message_status.isOk()) {
        return message_status;
    }
    const auto user_status = statement.bindUInt64(1, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    std::uint64_t affected_rows = 0;
    const auto insert_status = statement.executeUpdate(affected_rows);
    if (!insert_status.isOk()) {
        return insert_status;
    }
    if (affected_rows > 2U) {
        return Status::error(ErrorCode::InternalError,
                             "save delivery state affected unexpected row count");
    }
    return Status::ok();
}

Status upsertDeliveryDelivered(MySqlConnection& connection, std::uint64_t user_id,
                               std::uint64_t message_id, std::int64_t delivered_at_ms) {
    PreparedStatement statement(connection);
    const auto prepare_status = statement.prepare(
        "INSERT INTO message_deliveries "
        "(message_id, user_id, status, pushed_at_ms, delivered_at_ms, read_at_ms) "
        "VALUES (?, ?, 2, NULL, ?, NULL) "
        "ON DUPLICATE KEY UPDATE "
        "status = IF(status > VALUES(status), status, VALUES(status)), "
        "delivered_at_ms = COALESCE(delivered_at_ms, VALUES(delivered_at_ms))");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto message_status = statement.bindUInt64(0, message_id);
    if (!message_status.isOk()) {
        return message_status;
    }
    const auto user_status = statement.bindUInt64(1, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }
    const auto delivered_status = statement.bindInt64(2, delivered_at_ms);
    if (!delivered_status.isOk()) {
        return delivered_status;
    }

    std::uint64_t affected_rows = 0;
    const auto update_status = statement.executeUpdate(affected_rows);
    if (!update_status.isOk()) {
        return update_status;
    }
    if (affected_rows > 2U) {
        return Status::error(ErrorCode::InternalError,
                             "ack delivery state affected unexpected row count");
    }
    return Status::ok();
}

Status parseDeliveryStatus(const std::string& text, DeliveryStatus& status) {
    try {
        std::size_t parsed = 0;
        const auto raw_status = std::stoull(text, &parsed, 10);
        if (parsed != text.size()) {
            return Status::error(ErrorCode::InternalError,
                                 "delivery status query returned malformed value");
        }
        switch (raw_status) {
        case static_cast<std::uint64_t>(DeliveryStatus::kPending):
            status = DeliveryStatus::kPending;
            return Status::ok();
        case static_cast<std::uint64_t>(DeliveryStatus::kPushed):
            status = DeliveryStatus::kPushed;
            return Status::ok();
        case static_cast<std::uint64_t>(DeliveryStatus::kDelivered):
            status = DeliveryStatus::kDelivered;
            return Status::ok();
        case static_cast<std::uint64_t>(DeliveryStatus::kReadReserved):
            status = DeliveryStatus::kReadReserved;
            return Status::ok();
        default:
            return Status::error(ErrorCode::InternalError,
                                 "delivery status query returned unknown value");
        }
    } catch (const std::exception&) {
        return Status::error(ErrorCode::InternalError,
                             "delivery status query returned malformed value");
    }
}

}  // namespace

MySqlStorage::MySqlStorage(MySqlPool& pool, std::chrono::milliseconds acquire_timeout)
    : pool_(pool), acquire_timeout_(acquire_timeout), user_dao_(pool, acquire_timeout),
      friend_dao_(pool, acquire_timeout), group_dao_(pool, acquire_timeout),
      message_dao_(pool, acquire_timeout), offline_message_dao_(pool, acquire_timeout) {}

Status MySqlStorage::createUser(const CreateUserRequest& request, UserRecord& created_user) {
    return user_dao_.createUser(request, created_user);
}

Status MySqlStorage::findUserByUsername(const std::string& username, UserRecord& user) {
    return user_dao_.findUserByUsername(username, user);
}

Status MySqlStorage::findUserById(std::uint64_t user_id, UserRecord& user) {
    return user_dao_.findUserById(user_id, user);
}

Status MySqlStorage::findMessageByClientMessageId(std::uint64_t sender_id,
                                                  const std::string& client_msg_id,
                                                  MessageRecord& message) {
    return message_dao_.findByClientMessageId(sender_id, client_msg_id, message);
}

Status MySqlStorage::createFriendRequest(std::uint64_t requester_id,
                                         std::uint64_t target_user_id,
                                         FriendRequestRecord& request) {
    return friend_dao_.createFriendRequest(requester_id, target_user_id, request);
}

Status MySqlStorage::acceptFriendRequest(std::uint64_t requester_id,
                                         std::uint64_t target_user_id) {
    return friend_dao_.acceptFriendRequest(requester_id, target_user_id);
}

Status MySqlStorage::rejectFriendRequest(std::uint64_t requester_id,
                                         std::uint64_t target_user_id) {
    return friend_dao_.rejectFriendRequest(requester_id, target_user_id);
}

Status MySqlStorage::areFriends(std::uint64_t user_id, std::uint64_t friend_id,
                                bool& are_friends) {
    return friend_dao_.areFriends(user_id, friend_id, are_friends);
}

Status MySqlStorage::addFriendship(std::uint64_t user_id, std::uint64_t friend_id) {
    return friend_dao_.addFriendship(user_id, friend_id);
}

Status MySqlStorage::getFriends(std::uint64_t user_id, std::vector<UserProfileRecord>& friends) {
    return friend_dao_.getFriends(user_id, friends);
}

Status MySqlStorage::createGroup(const CreateGroupRequest& request, GroupRecord& created_group) {
    return group_dao_.createGroup(request, created_group);
}

Status MySqlStorage::addGroupMember(std::uint64_t group_id, std::uint64_t user_id) {
    return group_dao_.addGroupMember(group_id, user_id);
}

Status MySqlStorage::removeGroupMember(std::uint64_t group_id, std::uint64_t user_id) {
    return group_dao_.removeGroupMember(group_id, user_id);
}

Status MySqlStorage::getGroupMembers(std::uint64_t group_id,
                                     std::vector<GroupMemberRecord>& members) {
    return group_dao_.getGroupMembers(group_id, members);
}

Status MySqlStorage::findGroupById(std::uint64_t group_id, GroupRecord& group) {
    return group_dao_.findGroupById(group_id, group);
}

Status MySqlStorage::getGroupsForUser(std::uint64_t user_id, std::vector<GroupRecord>& groups) {
    return group_dao_.getGroupsForUser(user_id, groups);
}

// 保存一条普通消息，但没有离线接收者
Status MySqlStorage::saveMessage(const MessageRecord& message, std::uint64_t& message_id) {
    message_id = 0;

    MessageRecord saved_message;
    const auto status = saveMessageWithOfflineRecipients(message, {}, saved_message);
    if (!status.isOk()) {
        return status;
    }
    message_id = saved_message.message_id;
    return Status::ok();
}

Status
MySqlStorage::saveMessageWithOfflineRecipients(const MessageRecord& message,
                                               const std::vector<std::uint64_t>& offline_user_ids,
                                               MessageRecord& saved_message) {
    saved_message = {};

    std::vector<std::uint64_t> unique_offline_user_ids;
    const auto recipients_status =
        validateOfflineRecipients(offline_user_ids, unique_offline_user_ids);
    if (!recipients_status.isOk()) {
        return recipients_status;
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_.acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    const auto begin_status = guard->executeSimple("START TRANSACTION");
    if (!begin_status.isOk()) {
        return begin_status;
    }

    const auto insert_status =
        message_dao_.insertMessageInTransaction(*guard, message, saved_message);
    if (!insert_status.isOk()) {
        return rollbackClearAndReturn(*guard, saved_message, insert_status);
    }

    const auto offline_created_at_ms = Timestamp::now().millisecondsSinceEpoch();
    for (const auto user_id : unique_offline_user_ids) {
        const auto offline_status =
            insertOfflineMessage(*guard, user_id, saved_message.message_id, offline_created_at_ms);
        if (!offline_status.isOk()) {
            return rollbackClearAndReturn(*guard, saved_message, offline_status);
        }
        const auto delivery_status = upsertDeliveryPending(*guard, user_id, saved_message.message_id);
        if (!delivery_status.isOk()) {
            return rollbackClearAndReturn(*guard, saved_message, delivery_status);
        }
    }

    const auto commit_status = guard->executeSimple("COMMIT");
    if (!commit_status.isOk()) {
        saved_message = {};
        return commit_status;
    }
    return Status::ok();
}

Status MySqlStorage::saveOfflineMessage(std::uint64_t user_id, std::uint64_t message_id) {
    return offline_message_dao_.saveOfflineMessage(user_id, message_id);
}

Status MySqlStorage::findDeliveryStatus(std::uint64_t user_id, std::uint64_t message_id,
                                        DeliveryStatus& status) {
    status = DeliveryStatus::kPending;
    if (user_id == 0U) {
        return Status::error(ErrorCode::InvalidArgument, "user id must not be zero");
    }
    if (message_id == 0U) {
        return Status::error(ErrorCode::InvalidArgument, "message id must not be zero");
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_.acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("SELECT status FROM message_deliveries "
                          "WHERE message_id = ? AND user_id = ? LIMIT 1");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto message_status = statement.bindUInt64(0, message_id);
    if (!message_status.isOk()) {
        return message_status;
    }
    const auto user_status = statement.bindUInt64(1, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    MySqlQueryResult result;
    const auto query_status = statement.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }
    if (result.rows().empty()) {
        return Status::error(ErrorCode::NotFound, "delivery status was not found");
    }
    if (result.rows().size() != 1U || result.rows().front().values.size() != 1U ||
        !result.rows().front().values.front().has_value()) {
        return Status::error(ErrorCode::InternalError,
                             "delivery status query returned malformed row");
    }
    return parseDeliveryStatus(*result.rows().front().values.front(), status);
}

Status MySqlStorage::getOfflineMessages(std::uint64_t user_id, std::uint32_t limit,
                                        std::vector<OfflineMessageRecord>& messages) {
    return offline_message_dao_.getOfflineMessages(user_id, limit, messages);
}

Status MySqlStorage::markOfflineDelivered(std::uint64_t user_id,
                                          const std::vector<std::uint64_t>& message_ids) {
    return offline_message_dao_.markOfflineDelivered(user_id, message_ids);
}

Status MySqlStorage::ackOfflineMessages(std::uint64_t user_id,
                                        const std::vector<std::uint64_t>& message_ids,
                                        std::vector<OfflineMessageRecord>& acked_messages) {
    return offline_message_dao_.ackOfflineMessages(user_id, message_ids, acked_messages);
}

Status MySqlStorage::ackPrivateMessageDelivery(std::uint64_t user_id, std::uint64_t message_id,
                                               MessageRecord& message) {
    message = {};
    if (user_id == 0U) {
        return Status::error(ErrorCode::InvalidArgument, "user id must not be zero");
    }
    if (message_id == 0U) {
        return Status::error(ErrorCode::InvalidArgument, "message id must not be zero");
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_.acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    const auto begin_status = guard->executeSimple("START TRANSACTION");
    if (!begin_status.isOk()) {
        return begin_status;
    }

    MessageRecord found;
    const auto find_status = message_dao_.findByIdInTransaction(*guard, message_id, found);
    if (!find_status.isOk()) {
        return rollbackClearAndReturn(*guard, message, find_status);
    }
    if (found.conversation.type != ConversationType::kPrivate || found.receiver_id != user_id) {
        return rollbackClearAndReturn(
            *guard, message,
            Status::error(ErrorCode::NotFound,
                          "private message delivery target was not found"));
    }

    const auto delivered_at_ms = Timestamp::now().millisecondsSinceEpoch();
    const auto delivery_status =
        upsertDeliveryDelivered(*guard, user_id, message_id, delivered_at_ms);
    if (!delivery_status.isOk()) {
        return rollbackClearAndReturn(*guard, message, delivery_status);
    }

    const auto commit_status = guard->executeSimple("COMMIT");
    if (!commit_status.isOk()) {
        message = {};
        return commit_status;
    }
    message = std::move(found);
    return Status::ok();
}

Status MySqlStorage::getHistory(const HistoryQuery& query, std::vector<MessageRecord>& messages) {
    return message_dao_.getHistoryByConversation(query, messages);
}

}  // namespace liteim
