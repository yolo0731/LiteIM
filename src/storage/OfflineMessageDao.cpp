#include "liteim/storage/OfflineMessageDao.hpp"

#include "liteim/base/Timestamp.hpp"
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace liteim {
namespace {

constexpr unsigned int kMysqlDuplicateEntry = 1062;

Status malformedOfflineMessageRowStatus() {
    return Status::error(ErrorCode::InternalError, "offline_messages query returned a malformed row");
}

Status requiredValue(const MySqlRow& row, std::size_t index, const std::string*& value) {
    if (index >= row.values.size() || !row.values[index].has_value()) {
        return malformedOfflineMessageRowStatus();
    }
    value = &(*row.values[index]);
    return Status::ok();
}

Status parseUint64(const std::string& text, std::uint64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto result = std::stoull(text, &parsed, 10);
        if (parsed != text.size()) {
            return malformedOfflineMessageRowStatus();
        }
        value = static_cast<std::uint64_t>(result);
        return Status::ok();
    } catch (const std::exception&) {
        return malformedOfflineMessageRowStatus();
    }
}

Status parseInt64(const std::string& text, std::int64_t& value) {
    try {
        std::size_t parsed = 0;
        const auto result = std::stoll(text, &parsed, 10);
        if (parsed != text.size()) {
            return malformedOfflineMessageRowStatus();
        }
        value = static_cast<std::int64_t>(result);
        return Status::ok();
    } catch (const std::exception&) {
        return malformedOfflineMessageRowStatus();
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
    return malformedOfflineMessageRowStatus();
}

Status bindUint64(PreparedStatement& statement,
                  std::size_t index,
                  std::uint64_t value,
                  const std::string& field_name) {
    if (value == 0U) {
        return Status::error(ErrorCode::InvalidArgument, field_name + " must not be zero");
    }
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return Status::error(ErrorCode::InvalidArgument,
                             field_name + " exceeds supported MySQL signed bind range");
    }
    return statement.bindInt64(index, static_cast<std::int64_t>(value));
}

Status executeSimple(MySqlConnection& connection, const std::string& sql) {
    return connection.executeSimple(sql);
}

void rollbackSilently(MySqlConnection& connection) noexcept {
    (void)executeSimple(connection, "ROLLBACK");
}

Status rowToOfflineMessageRecord(const MySqlRow& row, OfflineMessageRecord& record) {
    if (row.values.size() != 10U) {
        return malformedOfflineMessageRowStatus();
    }

    const std::string* offline_message_id = nullptr;
    const std::string* user_id = nullptr;
    const std::string* offline_created_at_ms = nullptr;
    const std::string* message_id = nullptr;
    const std::string* conversation_type = nullptr;
    const std::string* conversation_id = nullptr;
    const std::string* sender_id = nullptr;
    const std::string* receiver_id = nullptr;
    const std::string* message_text = nullptr;
    const std::string* message_created_at_ms = nullptr;

    const auto offline_id_status = requiredValue(row, 0, offline_message_id);
    if (!offline_id_status.isOk()) {
        return offline_id_status;
    }
    const auto user_status = requiredValue(row, 1, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }
    const auto offline_created_status = requiredValue(row, 2, offline_created_at_ms);
    if (!offline_created_status.isOk()) {
        return offline_created_status;
    }
    const auto message_id_status = requiredValue(row, 3, message_id);
    if (!message_id_status.isOk()) {
        return message_id_status;
    }
    const auto conversation_type_status = requiredValue(row, 4, conversation_type);
    if (!conversation_type_status.isOk()) {
        return conversation_type_status;
    }
    const auto conversation_id_status = requiredValue(row, 5, conversation_id);
    if (!conversation_id_status.isOk()) {
        return conversation_id_status;
    }
    const auto sender_status = requiredValue(row, 6, sender_id);
    if (!sender_status.isOk()) {
        return sender_status;
    }
    const auto receiver_status = requiredValue(row, 7, receiver_id);
    if (!receiver_status.isOk()) {
        return receiver_status;
    }
    const auto text_status = requiredValue(row, 8, message_text);
    if (!text_status.isOk()) {
        return text_status;
    }
    const auto message_created_status = requiredValue(row, 9, message_created_at_ms);
    if (!message_created_status.isOk()) {
        return message_created_status;
    }

    OfflineMessageRecord parsed_record;
    const auto offline_parse_status = parseUint64(*offline_message_id, parsed_record.offline_message_id);
    if (!offline_parse_status.isOk()) {
        return offline_parse_status;
    }
    const auto user_parse_status = parseUint64(*user_id, parsed_record.user_id);
    if (!user_parse_status.isOk()) {
        return user_parse_status;
    }
    const auto offline_created_parse_status = parseInt64(*offline_created_at_ms, parsed_record.created_at_ms);
    if (!offline_created_parse_status.isOk()) {
        return offline_created_parse_status;
    }
    const auto message_parse_status = parseUint64(*message_id, parsed_record.message.message_id);
    if (!message_parse_status.isOk()) {
        return message_parse_status;
    }
    const auto type_status = parseConversationType(*conversation_type, parsed_record.message.conversation.type);
    if (!type_status.isOk()) {
        return type_status;
    }
    const auto conversation_parse_status = parseUint64(*conversation_id, parsed_record.message.conversation.id);
    if (!conversation_parse_status.isOk()) {
        return conversation_parse_status;
    }
    const auto sender_parse_status = parseUint64(*sender_id, parsed_record.message.sender_id);
    if (!sender_parse_status.isOk()) {
        return sender_parse_status;
    }
    const auto receiver_parse_status = parseUint64(*receiver_id, parsed_record.message.receiver_id);
    if (!receiver_parse_status.isOk()) {
        return receiver_parse_status;
    }
    const auto message_created_parse_status =
        parseInt64(*message_created_at_ms, parsed_record.message.created_at_ms);
    if (!message_created_parse_status.isOk()) {
        return message_created_parse_status;
    }
    parsed_record.message.text = *message_text;

    record = std::move(parsed_record);
    return Status::ok();
}

} // namespace

OfflineMessageDao::OfflineMessageDao(MySqlPool& pool, std::chrono::milliseconds acquire_timeout)
    : pool_(&pool), acquire_timeout_(acquire_timeout) {
}

Status OfflineMessageDao::saveOfflineMessage(std::uint64_t user_id, std::uint64_t message_id) {
    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("INSERT INTO offline_messages "
                          "(user_id, message_id, delivered, created_at_ms, delivered_at_ms) "
                          "VALUES (?, ?, 0, ?, NULL)");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto user_status = bindUint64(statement, 0, user_id, "user_id");
    if (!user_status.isOk()) {
        return user_status;
    }
    const auto message_status = bindUint64(statement, 1, message_id, "message_id");
    if (!message_status.isOk()) {
        return message_status;
    }
    const auto created_status = statement.bindInt64(2, Timestamp::now().millisecondsSinceEpoch());
    if (!created_status.isOk()) {
        return created_status;
    }

    std::uint64_t affected_rows = 0;
    const auto insert_status = statement.executeUpdate(affected_rows);
    if (!insert_status.isOk()) {
        if (statement.lastErrorNumber() == kMysqlDuplicateEntry) {
            return Status::error(ErrorCode::AlreadyExists, "offline message already exists");
        }
        return insert_status;
    }
    if (affected_rows != 1U) {
        return Status::error(ErrorCode::InternalError, "save offline message affected unexpected row count");
    }
    return Status::ok();
}

Status OfflineMessageDao::getOfflineMessages(std::uint64_t user_id,
                                             std::vector<OfflineMessageRecord>& messages) {
    messages.clear();

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("SELECT om.offline_message_id, om.user_id, om.created_at_ms, "
                          "m.message_id, m.conversation_type, m.conversation_id, m.sender_id, "
                          "m.receiver_id, m.message_text, m.created_at_ms "
                          "FROM offline_messages om "
                          "JOIN messages m ON m.message_id = om.message_id "
                          "WHERE om.user_id = ? AND om.delivered = 0 "
                          "ORDER BY om.offline_message_id ASC");
    if (!prepare_status.isOk()) {
        return prepare_status;
    }
    const auto user_status = bindUint64(statement, 0, user_id, "user_id");
    if (!user_status.isOk()) {
        return user_status;
    }

    MySqlQueryResult result;
    const auto query_status = statement.executeQuery(result);
    if (!query_status.isOk()) {
        return query_status;
    }

    std::vector<OfflineMessageRecord> parsed_messages;
    parsed_messages.reserve(result.rows().size());
    for (const auto& row : result.rows()) {
        OfflineMessageRecord message;
        const auto row_status = rowToOfflineMessageRecord(row, message);
        if (!row_status.isOk()) {
            return row_status;
        }
        parsed_messages.push_back(std::move(message));
    }

    messages = std::move(parsed_messages);
    return Status::ok();
}

Status OfflineMessageDao::markOfflineDelivered(std::uint64_t user_id,
                                               const std::vector<std::uint64_t>& message_ids) {
    if (message_ids.empty()) {
        return Status::ok();
    }

    if (user_id == 0U || user_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return Status::error(ErrorCode::InvalidArgument, "user_id is invalid");
    }
    for (const auto message_id : message_ids) {
        if (message_id == 0U ||
            message_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return Status::error(ErrorCode::InvalidArgument, "message_id is invalid");
        }
    }

    ConnectionGuard guard;
    const auto acquire_status = pool_->acquire(acquire_timeout_, guard);
    if (!acquire_status.isOk()) {
        return acquire_status;
    }

    const auto begin_status = executeSimple(*guard, "START TRANSACTION");
    if (!begin_status.isOk()) {
        return begin_status;
    }

    PreparedStatement statement(*guard);
    const auto prepare_status =
        statement.prepare("UPDATE offline_messages "
                          "SET delivered = 1, delivered_at_ms = ? "
                          "WHERE user_id = ? AND message_id = ? AND delivered = 0");
    if (!prepare_status.isOk()) {
        rollbackSilently(*guard);
        return prepare_status;
    }

    const auto delivered_at_ms = Timestamp::now().millisecondsSinceEpoch();
    for (const auto message_id : message_ids) {
        const auto delivered_status = statement.bindInt64(0, delivered_at_ms);
        if (!delivered_status.isOk()) {
            statement.close();
            rollbackSilently(*guard);
            return delivered_status;
        }
        const auto user_status = statement.bindInt64(1, static_cast<std::int64_t>(user_id));
        if (!user_status.isOk()) {
            statement.close();
            rollbackSilently(*guard);
            return user_status;
        }
        const auto message_status = statement.bindInt64(2, static_cast<std::int64_t>(message_id));
        if (!message_status.isOk()) {
            statement.close();
            rollbackSilently(*guard);
            return message_status;
        }

        std::uint64_t affected_rows = 0;
        const auto update_status = statement.executeUpdate(affected_rows);
        if (!update_status.isOk()) {
            statement.close();
            rollbackSilently(*guard);
            return update_status;
        }
    }

    statement.close();
    return executeSimple(*guard, "COMMIT");
}

} // namespace liteim
