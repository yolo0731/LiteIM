#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace liteim
{

    class Channel;
    class Epoller;

    class EventLoop
    {
    public:
        using Functor = std::function<void()>;

        EventLoop();
        ~EventLoop();

        EventLoop(const EventLoop &) = delete;
        EventLoop &operator=(const EventLoop &) = delete;

        void loop();
        void quit() noexcept;
        void runInLoop(Functor task);   // 在 loop 所在线程执行 task,如果当前线程就是 loop 所在线程，则直接执行 task，否则将 task 放入 loop 所在线程的待执行任务队列
        void queueInLoop(Functor task); // 将 task 放入 loop 所在线程的待执行任务队列

        void updateChannel(Channel *channel);
        void removeChannel(Channel *channel);

        bool isInLoopThread() const noexcept;
        void assertInLoopThread() const;

    private:
        void wakeup() noexcept;
        void handleWakeup() noexcept;
        void doPendingTasks();

        bool looping_{false};
        std::atomic_bool quit_{false};
        const std::thread::id thread_id_;
        std::unique_ptr<Epoller> epoller_;
        int wakeup_fd_{-1};
        std::unique_ptr<Channel> wakeup_channel_; // 用于唤醒 loop 所在线程的 Channel,当其他线程向 wakeup_fd_ 写入数据时，wakeup_channel_ 就会变成可读状态，loop 所在线程就会被唤醒
        std::mutex mutex_;
        std::vector<Functor> pending_tasks_;            // 由 loop() 线程以后要执行的任务函数
        std::atomic_bool calling_pending_tasks_{false}; // 是否正在执行 pending_tasks_ 里的任务
    };

} // namespace liteim
