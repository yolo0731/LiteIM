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
    void cancel(TimerId timer_id);
    std::size_t popExpired(std::int64_t now_ms);
    std::int64_t nextExpirationMilliseconds();
    std::size_t activeTimerCount() const;
    bool empty() const;
    void clear();

private:
    struct HeapEntry {
        std::int64_t expiration_ms{0};
        TimerId timer_id{0};
    };

    struct TimerEntry {
        std::int64_t expiration_ms{0};
        TimerCallback callback;
    };

    struct Compare {
        bool operator()(const HeapEntry& lhs, const HeapEntry& rhs) const noexcept;
    };

    void removeStaleTopEntries();

    TimerId next_timer_id_{1};
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, Compare> heap_;
    std::unordered_map<TimerId, TimerEntry> timers_;
};

} // namespace liteim
