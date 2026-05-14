#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/CacheTypes.hpp"
#include "liteim/cache/RedisPool.hpp"

#include <chrono>
#include <cstdint>

namespace liteim {

// unread的key格式：unread:user:<user_id>:conversation:<conversation_type>:<conversation_id>
// value格式：未读计数，使用Redis的字符串类型存储
class UnreadCounter {
public:
    explicit UnreadCounter(RedisPool& pool,
                           std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds{
                               200});
    // 增加未读计数，delta 可以为正（增加）或负（减少）。返回更新后的未读计数。
    Status incrUnread(const UnreadKey& key, std::uint64_t delta, std::uint64_t& unread_count);
    // 获取当前未读计数
    Status getUnread(const UnreadKey& key, std::uint64_t& unread_count);
    // 清除未读计数（设置为0）
    Status clearUnread(const UnreadKey& key);

private:
    RedisPool& pool_;
    std::chrono::milliseconds acquire_timeout_;
};

}  // namespace liteim
