#include "liteim/storage/MessageDao.hpp"

#include "liteim/base/Timestamp.hpp"
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace liteim {
namespace {

constexpr std::uint32_t kMaxHistoryLimit = 50;  // 限制一次查询历史消息的最大条数

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
// 将字符串解析为 ConversationType 枚举值
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

Status bindUint64(PreparedStatement& statement, std::size_t index, std::uint64_t value,
                  const std::string& field_name) {
    (void)field_name;
    return statement.bindUInt64(index, value);
}

Status bindConversationType(PreparedStatement& statement, std::size_t index,
                            ConversationType type) {
    return statement.bindInt64(index, static_cast<std::int64_t>(type));
}

// 把 MySQL 查出来的一行消息数据，转换成 MessageRecord。
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

// 验证 ConversationKey 的合法性，比如 type 是否是已知的枚举值，id 是否非零等。
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

// 规范化消息记录以便插入数据库，比如根据 conversation type 确定 receiver_id，设置 created_at_ms 等。
Status normalizeMessageForInsert(const MessageRecord& input, ConversationType expected_type,
                                 std::uint64_t& receiver_id, std::int64_t& created_at_ms) {
    const auto conversation_status = validateConversationKey(input.conversation);
    if (!conversation_status.isOk()) {
        return conversation_status;
    }
    if (input.conversation.type != expected_type) {
        return Status::error(ErrorCode::InvalidArgument,
                             "message conversation type does not match DAO method");
    }
    if (input.sender_id == 0U) {
        return Status::error(ErrorCode::InvalidArgument, "sender id must not be zero");
    }

    receiver_id = input.receiver_id;
    if (expected_type == ConversationType::kPrivate && receiver_id == 0U) {
        return Status::error(ErrorCode::InvalidArgument,
                             "private message receiver id must not be zero");
    }
    if (expected_type == ConversationType::kGroup && receiver_id == 0U) {
        receiver_id = input.conversation.id;
    }

    created_at_ms =
        input.created_at_ms > 0 ? input.created_at_ms : Timestamp::now().millisecondsSinceEpoch();
    return Status::ok();
}

// 执行一个查询消息的 SQL，并要求结果必须刚好一行。
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

// 查出当前连接刚刚 INSERT 的那条 messages 记录。
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

// 回滚事务，事务中间失败时，先 ROLLBACK，再返回原来的错误。
Status rollbackAndReturn(MySqlConnection& connection, const Status& status) {
    (void)connection.executeSimple("ROLLBACK");
    return status;
}

// 整个保存消息的流程：先验证和规范化输入，开始事务，执行 INSERT，查询刚插入的消息记录，最后提交事务。如果中间任何一步失败，都回滚事务并返回错误。
Status saveMessageWithType(MySqlPool& pool, std::chrono::milliseconds acquire_timeout,
                           const MessageRecord& message, ConversationType expected_type,
                           MessageRecord& saved_message) {
    std::uint64_t receiver_id = 0;
    std::int64_t created_at_ms = 0;
    const auto normalize_status =
        normalizeMessageForInsert(message, expected_type, receiver_id, created_at_ms);
    if (!normalize_status.isOk()) {
        return normalize_status;
    }

    ConnectionGuard guard;
    const auto acquire_status = pool.acquire(acquire_timeout, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    const auto begin_status = guard->executeSimple("START TRANSACTION");  // 开始事务
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

    const auto type_status = bindConversationType(statement, 0, expected_type);
    if (!type_status.isOk()) {
        return rollbackAndReturn(*guard, type_status);
    }
    const auto conversation_status =
        bindUint64(statement, 1, message.conversation.id, "conversation_id");
    if (!conversation_status.isOk()) {
        return rollbackAndReturn(*guard, conversation_status);
    }
    const auto sender_status = bindUint64(statement, 2, message.sender_id, "sender_id");
    if (!sender_status.isOk()) {
        return rollbackAndReturn(*guard, sender_status);
    }
    const auto receiver_status = bindUint64(statement, 3, receiver_id, "receiver_id");
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
    // 执行 INSERT 语句，affected_rows 应该是 1，如果不是，说明插入失败，回滚事务并返回错误。
    const auto insert_status = statement.executeUpdate(affected_rows);
    if (!insert_status.isOk()) {
        return rollbackAndReturn(*guard, insert_status);
    }
    if (affected_rows != 1U) {
        return rollbackAndReturn(
            *guard,
            Status::error(ErrorCode::InternalError, "save message affected unexpected row count"));
    }
    // 查询刚插入的消息记录，MySQL 提供了 LAST_INSERT_ID() 函数可以获取当前连接最后一次插入的 AUTO_INCREMENT 主键值，这里利用它来查询刚插入的消息。
    const auto query_status = queryLastInsertedMessage(*guard, saved_message);
    if (!query_status.isOk()) {
        return rollbackAndReturn(*guard, query_status);
    }

    return guard->executeSimple("COMMIT");  // 提交事务
}

}  // namespace

MessageDao::MessageDao(MySqlPool& pool, std::chrono::milliseconds acquire_timeout)
    : pool_(&pool), acquire_timeout_(acquire_timeout) {}

Status MessageDao::savePrivateMessage(const MessageRecord& message, MessageRecord& saved_message) {
    return saveMessageWithType(*pool_, acquire_timeout_, message, ConversationType::kPrivate,
                               saved_message);
}

Status MessageDao::saveGroupMessage(const MessageRecord& message, MessageRecord& saved_message) {
    // message是调用方传进来的“待保存消息”，saved_message是MySQL 保存后查回来的“完整消息”
    return saveMessageWithType(*pool_, acquire_timeout_, message, ConversationType::kGroup,
                               saved_message);
}

Status MessageDao::getHistoryByConversation(const HistoryQuery& query,
                                            std::vector<MessageRecord>& messages) {
    messages.clear();

    const auto conversation_status = validateConversationKey(query.conversation);
    if (!conversation_status.isOk()) {
        return conversation_status;
    }
    if (query.limit == 0U) {
        return Status::error(ErrorCode::InvalidArgument, "history query limit must not be zero");
    }
    const auto limit = std::min(query.limit, kMaxHistoryLimit);

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    PreparedStatement statement(*guard);
    // 根据是否有 before_message_id 来选择不同的 SQL，查询消息历史记录，结果按照 message_id 降序排列，如果 before_message_id 不为零，还要加一个 message_id < before_message_id 的条件来实现分页查询。
    // 分页查询: 第一次请求不传 before_message_id，查询最新的 limit 条消息；第二次请求传上次返回的最后一条消息的 message_id 作为 before_message_id，就能查询到更早的 limit 条消息
    const auto sql = query.before_message_id == 0U
                         ? "SELECT message_id, conversation_type, conversation_id, sender_id, "
                           "receiver_id, message_text, "
                           "created_at_ms FROM messages "
                           "WHERE conversation_type = ? AND conversation_id = ? "
                           "ORDER BY message_id DESC LIMIT ?"
                         : "SELECT message_id, conversation_type, conversation_id, sender_id, "
                           "receiver_id, message_text, "
                           "created_at_ms FROM messages "
                           "WHERE conversation_type = ? AND conversation_id = ? AND message_id < ? "
                           "ORDER BY message_id DESC LIMIT ?";

    const auto prepare_status = statement.prepare(sql);
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto type_status = bindConversationType(statement, 0, query.conversation.type);
    if (!type_status.isOk()) {
        return type_status;
    }
    const auto conversation_bind_status =
        bindUint64(statement, 1, query.conversation.id, "conversation_id");
    if (!conversation_bind_status.isOk()) {
        return conversation_bind_status;
    }

    std::size_t limit_index = 2;
    if (query.before_message_id != 0U) {
        const auto before_status =
            bindUint64(statement, 2, query.before_message_id, "before_message_id");
        if (!before_status.isOk()) {
            return before_status;
        }
        limit_index = 3;
    }
    const auto limit_status = statement.bindInt64(limit_index, static_cast<std::int64_t>(limit));
    if (!limit_status.isOk()) {
        return limit_status;
    }

    MySqlQueryResult result;
    const auto query_status = statement.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }

    std::vector<MessageRecord> parsed_messages;
    parsed_messages.reserve(result.rows().size());
    for (const auto& row : result.rows()) {
        MessageRecord message;
        const auto row_status = rowToMessageRecord(row, message);
        if (!row_status.isOk()) {
            return row_status;
        }
        parsed_messages.push_back(std::move(message));
    }

    messages = std::move(parsed_messages);
    return Status::ok();
}

}  // namespace liteim
