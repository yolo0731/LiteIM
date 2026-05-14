#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <chrono>
#include <vector>

namespace liteim {

class MySqlPool;

class MessageDao {
public:
    explicit MessageDao(MySqlPool& pool,
                        std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status savePrivateMessage(const MessageRecord& message, MessageRecord& saved_message);

    Status saveGroupMessage(const MessageRecord& message, MessageRecord& saved_message);
    // 获取某个会话的历史消息，按照 message_id 降序排列
    Status getHistoryByConversation(const HistoryQuery& query,
                                    std::vector<MessageRecord>& messages);

private:
    MySqlPool* pool_;
    std::chrono::milliseconds acquire_timeout_;
};

}  // namespace liteim
