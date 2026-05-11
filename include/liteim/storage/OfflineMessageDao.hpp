#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace liteim {

class MySqlPool;

class OfflineMessageDao {
public:
    explicit OfflineMessageDao(MySqlPool& pool,
                               std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status saveOfflineMessage(std::uint64_t user_id, std::uint64_t message_id);
    Status getOfflineMessages(std::uint64_t user_id, std::vector<OfflineMessageRecord>& messages);
    Status markOfflineDelivered(std::uint64_t user_id, const std::vector<std::uint64_t>& message_ids);

private:
    MySqlPool* pool_;
    std::chrono::milliseconds acquire_timeout_;
};

} // namespace liteim
