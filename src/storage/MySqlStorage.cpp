#include "liteim/storage/MySqlStorage.hpp"

#include "liteim/base/Timestamp.hpp"
#include "liteim/storage/MySqlConnection.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace liteim {
namespace {

Status malformedMessageRowStatus() {
    return Status::error(ErrorCode::InternalError, "messages query returned a malformed row");
}

Status requiredValue(const MySqlRow& row, std::size_t index, const std::string*& value) {
    if (index >= row.values.size() || !row.values[index].has_value()) {
        return malformedMessageRowStatus();
    }
    value = &(*row.values[index]);
    return Status::ok();
}

Status parseUint64(const std::string& text, std::uint64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto result = std::stoull(text, &parsed, 10);
        if (parsed != text.size()) {
            return malformedMessageRowStatus();
        }
        value = static_cast<std::uint64_t>(result);
        return Status::ok();
    } catch (const std::exception&) {
        return malformedMessageRowStatus();
    }
}

Status parseInt64(const std::string& text, std::int64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto result = std::stoll(text, &parsed, 10);
        if (parsed != text.size()) {
            return malformedMessageRowStatus();
        }
        value = static_cast<std::int64_t>(result);
        return Status::ok();
    } catch (const std::exception&) {
        return malformedMessageRowStatus();
    }
}

Status parseConversationType(const std::string& text, ConversationType& type) {
    std::uint64_t raw_type = 0;
    const auto parse_status = parseUint64(text, raw_type);
    if (!parse_status.isOk()) {
        return parse_status;
    }
    if (raw_type == static_cast<std::uint64_t>(ConversationType::kPrivate)) {
        type = ConversationType::kPrivate;
        return Status::ok();
    }
    if (raw_type == static_cast<std::uint64_t>(ConversationType::kGroup)) {
        type = ConversationType::kGroup;
        return Status::ok();
    }
    return malformedMessageRowStatus();
}

Status bindConversationType(PreparedStatement& statement, std::size_t index,
                            ConversationType type) {
    return statement.bindInt64(index, static_cast<std::int64_t>(type));
}

Status rowToMessageRecord(const MySqlRow& row, MessageRecord& message) {
    if (row.values.size() != 7U) {
        return malformedMessageRowStatus();
    }

    const std::string* message_id = nullptr;
    const std::string* conversation_type = nullptr;
    const std::string* conversation_id = nullptr;
    const std::string* sender_id = nullptr;
    const std::string* receiver_id = nullptr;
    const std::string* message_text = nullptr;
    const std::string* created_at_ms = nullptr;

    const auto message_id_status = requiredValue(row, 0, message_id);
    if (!message_id_status.isOk()) {
        return message_id_status;
    }
    const auto conversation_type_status = requiredValue(row, 1, conversation_type);
    if (!conversation_type_status.isOk()) {
        return conversation_type_status;
    }
    const auto conversation_id_status = requiredValue(row, 2, conversation_id);
    if (!conversation_id_status.isOk()) {
        return conversation_id_status;
    }
    const auto sender_status = requiredValue(row, 3, sender_id);
    if (!sender_status.isOk()) {
        return sender_status;
    }
    const auto receiver_status = requiredValue(row, 4, receiver_id);
    if (!receiver_status.isOk()) {
        return receiver_status;
    }
    const auto text_status = requiredValue(row, 5, message_text);
    if (!text_status.isOk()) {
        return text_status;
    }
    const auto created_status = requiredValue(row, 6, created_at_ms);
    if (!created_status.isOk()) {
        return created_status;
    }

    MessageRecord parsed_message;
    const auto id_status = parseUint64(*message_id, parsed_message.message_id);
    if (!id_status.isOk()) {
        return id_status;
    }
    const auto type_status =
        parseConversationType(*conversation_type, parsed_message.conversation.type);
    if (!type_status.isOk()) {
        return type_status;
    }
    const auto conversation_status = parseUint64(*conversation_id, parsed_message.conversation.id);
    if (!conversation_status.isOk()) {
        return conversation_status;
    }
    const auto sender_parse_status = parseUint64(*sender_id, parsed_message.sender_id);
    if (!sender_parse_status.isOk()) {
        return sender_parse_status;
    }
    const auto receiver_parse_status = parseUint64(*receiver_id, parsed_message.receiver_id);
    if (!receiver_parse_status.isOk()) {
        return receiver_parse_status;
    }
    const auto created_parse_status = parseInt64(*created_at_ms, parsed_message.created_at_ms);
    if (!created_parse_status.isOk()) {
        return created_parse_status;
    }
    parsed_message.text = *message_text;

    message = std::move(parsed_message);
    return Status::ok();
}

Status validateConversationKey(const ConversationKey& conversation) {
    if (conversation.type != ConversationType::kPrivate &&
        conversation.type != ConversationType::kGroup) {
        return Status::error(ErrorCode::InvalidArgument, "conversation type is invalid");
    }
    if (conversation.id == 0U) {
        return Status::error(ErrorCode::InvalidArgument, "conversation id must not be zero");
    }
    return Status::ok();
}

// 把准备插入 messages 表的数据补齐，根据 conversation type 确定 receiver_id，设置 created_at_ms 。
Status normalizeMessageForInsert(const MessageRecord& input, std::uint64_t& receiver_id,
                                 std::int64_t& created_at_ms) {
    const auto conversation_status = validateConversationKey(input.conversation);
    if (!conversation_status.isOk()) {
        return conversation_status;
    }
    if (input.sender_id == 0U) {
        return Status::error(ErrorCode::InvalidArgument, "sender id must not be zero");
    }

    receiver_id = input.receiver_id;
    if (input.conversation.type == ConversationType::kPrivate && receiver_id == 0U) {
        return Status::error(ErrorCode::InvalidArgument,
                             "private message receiver id must not be zero");
    }
    if (input.conversation.type == ConversationType::kGroup && receiver_id == 0U) {
        receiver_id = input.conversation.id;
    }

    created_at_ms =
        input.created_at_ms > 0 ? input.created_at_ms : Timestamp::now().millisecondsSinceEpoch();
    return Status::ok();
}

Status querySingleMessage(PreparedStatement& statement, MessageRecord& message) {
    MySqlQueryResult result;
    const auto query_status = statement.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }
    if (result.rows().empty()) {
        return Status::error(ErrorCode::NotFound, "message was not found");
    }
    if (result.rows().size() != 1U) {
        return Status::error(ErrorCode::InternalError, "messages query returned multiple rows");
    }
    return rowToMessageRecord(result.rows().front(), message);
}

Status queryLastInsertedMessage(MySqlConnection& connection, MessageRecord& message) {
    PreparedStatement query(connection);
    const auto prepare_status = query.prepare(
        "SELECT message_id, conversation_type, conversation_id, sender_id, receiver_id, "
        "message_text, created_at_ms "
        "FROM messages WHERE message_id = LAST_INSERT_ID() LIMIT 1");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    return querySingleMessage(query, message);
}

void rollbackSilently(MySqlConnection& connection) noexcept {
    (void)connection.executeSimple("ROLLBACK");
}

Status rollbackAndReturn(MySqlConnection& connection, const Status& status) {
    rollbackSilently(connection);
    return status;
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

    std::uint64_t receiver_id = 0;
    std::int64_t created_at_ms = 0;
    const auto normalize_status = normalizeMessageForInsert(message, receiver_id, created_at_ms);
    if (!normalize_status.isOk()) {
        return normalize_status;
    }

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

    PreparedStatement statement(*guard);
    const auto prepare_status = statement.prepare(
        "INSERT INTO messages "
        "(conversation_type, conversation_id, sender_id, receiver_id, message_text, created_at_ms) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    if (!prepare_status.isOk()) {
        return rollbackAndReturn(*guard, prepare_status);
    }

    const auto type_status = bindConversationType(statement, 0, message.conversation.type);
    if (!type_status.isOk()) {
        return rollbackAndReturn(*guard, type_status);
    }
    const auto conversation_status = statement.bindUInt64(1, message.conversation.id);
    if (!conversation_status.isOk()) {
        return rollbackAndReturn(*guard, conversation_status);
    }
    const auto sender_status = statement.bindUInt64(2, message.sender_id);
    if (!sender_status.isOk()) {
        return rollbackAndReturn(*guard, sender_status);
    }
    const auto receiver_status = statement.bindUInt64(3, receiver_id);
    if (!receiver_status.isOk()) {
        return rollbackAndReturn(*guard, receiver_status);
    }
    const auto text_status = statement.bindString(4, message.text);
    if (!text_status.isOk()) {
        return rollbackAndReturn(*guard, text_status);
    }
    const auto created_status = statement.bindInt64(5, created_at_ms);
    if (!created_status.isOk()) {
        return rollbackAndReturn(*guard, created_status);
    }

    std::uint64_t affected_rows = 0;
    const auto insert_status = statement.executeUpdate(affected_rows);
    if (!insert_status.isOk()) {
        return rollbackAndReturn(*guard, insert_status);
    }
    if (affected_rows != 1U) {
        return rollbackAndReturn(
            *guard,
            Status::error(ErrorCode::InternalError, "save message affected unexpected row count"));
    }

    const auto query_status = queryLastInsertedMessage(*guard, saved_message);
    if (!query_status.isOk()) {
        return rollbackClearAndReturn(*guard, saved_message, query_status);
    }

    const auto offline_created_at_ms = Timestamp::now().millisecondsSinceEpoch();
    for (const auto user_id : unique_offline_user_ids) {
        const auto offline_status =
            insertOfflineMessage(*guard, user_id, saved_message.message_id, offline_created_at_ms);
        if (!offline_status.isOk()) {
            return rollbackClearAndReturn(*guard, saved_message, offline_status);
        }
    }

    statement.close();
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

Status MySqlStorage::getOfflineMessages(std::uint64_t user_id,
                                        std::vector<OfflineMessageRecord>& messages) {
    return offline_message_dao_.getOfflineMessages(user_id, messages);
}

Status MySqlStorage::markOfflineDelivered(std::uint64_t user_id,
                                          const std::vector<std::uint64_t>& message_ids) {
    return offline_message_dao_.markOfflineDelivered(user_id, message_ids);
}

Status MySqlStorage::getHistory(const HistoryQuery& query, std::vector<MessageRecord>& messages) {
    return message_dao_.getHistoryByConversation(query, messages);
}

}  // namespace liteim
