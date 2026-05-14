#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

namespace liteim {

class EventLoop;

class EventLoopThread {
public:
    EventLoopThread() = default;
    ~EventLoopThread();

    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    EventLoop* startLoop();  // 把子线程里这个 EventLoop loop 的地址保存到成员变量 loop_里。
    void stop() noexcept;
    bool running() const noexcept;

private:
    void threadFunc() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread thread_;
    std::thread::id thread_id_;
    EventLoop* loop_{nullptr};
    bool started_{false};
    bool running_{false};
    std::atomic_bool join_started_{false};
    /*
    跨线程、跨作用域传递和重抛异常的句柄。本质是一个智能指针
    主要函数：std::current_exception() 在 catch 块中捕获当前异常，返回 exception_ptr
    std::rethrow_exception(ptr)重新抛出 exception_ptr 持有的异常
    */
    std::exception_ptr startup_exception_;
};

}  // namespace liteim
