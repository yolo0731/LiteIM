#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/CacheTypes.hpp"
#include "liteim/cache/RedisPool.hpp"

#include <chrono>
#include <cstdint>

namespace liteim {

class UnreadCounter {
public:
    explicit UnreadCounter(RedisPool& pool, std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds{200});

    Status incrUnread(const UnreadKey& key, std::uint64_t delta, std::uint64_t& unread_count);
    Status getUnread(const UnreadKey& key, std::uint64_t& unread_count);
    Status clearUnread(const UnreadKey& key);

private:
    RedisPool& pool_;
    std::chrono::milliseconds acquire_timeout_;
};

} // namespace liteim
