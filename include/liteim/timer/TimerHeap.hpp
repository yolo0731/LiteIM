#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

namespace liteim {

class TimerHeap {
public:
    using TimerId = std::uint64_t;
    using TimerCallback = std::function<void()>;

    TimerHeap() = default;
    ~TimerHeap() = default;

    TimerHeap(const TimerHeap&) = delete;
    TimerHeap& operator=(const TimerHeap&) = delete;

    TimerId add(std::int64_t expiration_ms, TimerCallback callback);
    void cancel(TimerId timer_id); // 惰性删除,从 timers_ 里面删除 id,等它浮到堆顶时，removeStaleTopEntries() 再把它清掉
    std::size_t popExpired(std::int64_t now_ms);
    // 拿当前时间now_ms去比较哪些定时器已经过期，清除堆里过期，无效的定时器，并执行有效但过期的定时器的callback，返回执行了多少个定时器
    std::int64_t nextExpirationMilliseconds(); // 返回最近一个有效定时器的过期时间
    std::size_t activeTimerCount() const;      // 返回当前有效定时器数量
    bool empty() const;
    void clear();

private:
    struct HeapEntry {
        std::int64_t expiration_ms{0}; // 过期时间，单位毫秒
        TimerId timer_id{0};           // 定时器 ID
    };

    struct TimerEntry {
        std::int64_t expiration_ms{0};
        TimerCallback callback; // 定时器真正要执行的 callback
    };

    struct Compare {
        bool operator()(const HeapEntry& lhs, const HeapEntry& rhs) const noexcept;
    };

    void removeStaleTopEntries();

    TimerId next_timer_id_{1};
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, Compare> heap_;
    // 小根堆，按照过期时间排序，负责快速找“最早到期的是谁”
    std::unordered_map<TimerId, TimerEntry> timers_; // 负责保存这个定时器是否还有效，以及它真正要执行的 callback
};

} // namespace liteim
