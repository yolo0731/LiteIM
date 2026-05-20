#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <chrono>
#include <vector>

namespace liteim {

class MySqlConnection;
class MySqlPool;

class MessageDao {
public:
    explicit MessageDao(MySqlPool& pool,
                        std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status savePrivateMessage(const MessageRecord& message, MessageRecord& saved_message);

    Status saveGroupMessage(const MessageRecord& message, MessageRecord& saved_message);
    Status insertMessageInTransaction(MySqlConnection& connection, const MessageRecord& message,
                                      MessageRecord& saved_message);
    Status findByIdInTransaction(MySqlConnection& connection, std::uint64_t message_id,
                                 MessageRecord& message);
    Status findByClientMessageId(std::uint64_t sender_id, const std::string& client_msg_id,
                                 MessageRecord& message);
    // 获取某个会话的历史消息，按照 message_id 降序排列
    Status getHistoryByConversation(const HistoryQuery& query,
                                    std::vector<MessageRecord>& messages);

private:
    MySqlPool* pool_;
    std::chrono::milliseconds acquire_timeout_;
};

}  // namespace liteim
