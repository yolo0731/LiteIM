#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace liteim {

class MySqlPool;

// 主要用来管理 offline_messages 表，不负责保存真实消息内容。真实消息内容在 messages 表
/*
负责：
1. 某个用户离线时，记录他还有哪条消息没收到
2. 用户上线时，查出这些未投递消息
3. 消息投递成功后，标记为已投递
*/

class OfflineMessageDao {
public:
    explicit OfflineMessageDao(MySqlPool& pool, std::chrono::milliseconds acquire_timeout =
                                                    std::chrono::milliseconds(500));

    // 保存一条离线消息，user_id 是消息的接收者，message_id 是 messages 表中对应的消息 ID。这个函数会在 offline_messages 表中插入一条记录，表示 user_id 有一条 message_id 的离线消息。
    Status saveOfflineMessage(std::uint64_t user_id, std::uint64_t message_id);

    // 获取用户的离线消息列表，按照 offline_message_id 升序排列
    Status getOfflineMessages(std::uint64_t user_id, std::uint32_t limit,
                              std::vector<OfflineMessageRecord>& messages);

    // 标记离线消息已送达，message_ids 是要标记为已送达的消息 ID 列表
    // 这个函数会将 offline_messages 表中 user_id 对应的这些 message_id 的记录的 delivered 字段更新为 1，并设置 delivered_at_ms 为当前时间戳，表示这些消息已经成功投递给用户了。但是这不会删除offline_messages 表这些记录
    Status markOfflineDelivered(std::uint64_t user_id,
                                const std::vector<std::uint64_t>& message_ids);

private:
    MySqlPool* pool_;
    std::chrono::milliseconds acquire_timeout_;
};

}  // namespace liteim
