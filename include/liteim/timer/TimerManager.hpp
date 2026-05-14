#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/timer/TimerHeap.hpp"

#include <chrono>
#include <memory>

namespace liteim {

class Channel;
class EventLoop;

class TimerManager {
public:
    using TimerId = TimerHeap::TimerId;
    using TimerCallback = TimerHeap::TimerCallback;

    TimerManager(EventLoop* loop, std::chrono::milliseconds tick_interval);
    ~TimerManager();

    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;

    Status start();
    void stop() noexcept;
    TimerId runAfter(std::chrono::milliseconds delay, TimerCallback callback);
    // 在 delay 之后进行timers_.add，返回一个 TimerId
    void cancel(TimerId timer_id);

    bool started() const noexcept;
    int timerFd() const noexcept;

private:
    Status startInLoop();
    void stopInLoop() noexcept;
    // 在 timerfd 可读时调用，先 read 清掉 timerfd 的到期事件，再让 TimerHeap 执行所有已经到期的 callback
    void handleRead() noexcept;
    // 获取当前单调时钟的毫秒值，专门用于定时器到期比较，不是现实世界的时间戳
    std::int64_t steadyNowMilliseconds() const noexcept;

    EventLoop* loop_;
    std::chrono::milliseconds tick_interval_;  // 定时器管理器的滴答间隔，单位毫秒
    UniqueFd timer_fd_;
    std::unique_ptr<Channel> timer_channel_;
    TimerHeap timers_;
    bool started_{false};
    bool channel_registered_{false};
    bool handling_timer_event_{false};
};

}  // namespace liteim
