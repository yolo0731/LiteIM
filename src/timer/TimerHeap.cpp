#include "liteim/timer/TimerHeap.hpp"

#include <utility>

namespace liteim {

bool TimerHeap::Compare::operator()(const HeapEntry& lhs, const HeapEntry& rhs) const noexcept {
    if (lhs.expiration_ms == rhs.expiration_ms) {
        return lhs.timer_id > rhs.timer_id;
    }
    return lhs.expiration_ms > rhs.expiration_ms;
}

TimerHeap::TimerId TimerHeap::add(std::int64_t expiration_ms, TimerCallback callback) {
    const auto timer_id = next_timer_id_++;
    timers_.emplace(timer_id, TimerEntry{expiration_ms, std::move(callback)});
    heap_.push(HeapEntry{expiration_ms, timer_id});
    return timer_id;
}

void TimerHeap::cancel(TimerId timer_id) {
    timers_.erase(timer_id);
}

std::size_t TimerHeap::popExpired(std::int64_t now_ms) {
    std::size_t fired_count = 0;  // 实际处理了多少个“有效且已经过期”的定时器

    while (true) {
        removeStaleTopEntries();
        if (heap_.empty() || heap_.top().expiration_ms > now_ms) {
            return fired_count;
        }

        const auto heap_entry = heap_.top();
        heap_.pop();

        auto timer_it = timers_.find(heap_entry.timer_id);
        if (timer_it == timers_.end() ||
            timer_it->second.expiration_ms != heap_entry.expiration_ms) {
            continue;
        }

        auto callback = std::move(timer_it->second.callback);
        timers_.erase(timer_it);
        if (callback) {
            callback();
        }
        ++fired_count;
    }
}

std::int64_t TimerHeap::nextExpirationMilliseconds() {
    removeStaleTopEntries();
    if (heap_.empty()) {
        return -1;
    }
    return heap_.top().expiration_ms;
}

std::size_t TimerHeap::activeTimerCount() const {
    return timers_.size();
}

bool TimerHeap::empty() const {
    return timers_.empty();
}

void TimerHeap::clear() {
    timers_.clear();
    heap_ = std::priority_queue<HeapEntry, std::vector<HeapEntry>, Compare>();
}

void TimerHeap::removeStaleTopEntries() {
    while (!heap_.empty()) {
        const auto& heap_entry = heap_.top();
        const auto timer_it = timers_.find(heap_entry.timer_id);
        if (timer_it != timers_.end() &&
            timer_it->second.expiration_ms == heap_entry.expiration_ms) {
            return;
        }
        heap_.pop();
    }
}

}  // namespace liteim
