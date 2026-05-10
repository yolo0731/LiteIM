#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "liteim/net/UniqueFd.hpp"

namespace liteim {

class Channel;
class Epoller;

class EventLoop {
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void loop();
    void quit() noexcept;
    void runInLoop(Functor task);
    void queueInLoop(Functor task);

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);

    bool isInLoopThread() const noexcept;
    void assertInLoopThread() const;

private:
    void wakeup() noexcept;
    void handleWakeup() noexcept;
    void doPendingTasks();

    std::atomic_bool looping_{false};
    std::atomic_bool quit_{false};
    const std::thread::id thread_id_;
    std::unique_ptr<Epoller> epoller_;
    UniqueFd wakeup_fd_;
    std::unique_ptr<Channel> wakeup_channel_;
    std::mutex mutex_;
    std::vector<Functor> pending_tasks_;
    std::atomic_bool calling_pending_tasks_{false};
};

} // namespace liteim
