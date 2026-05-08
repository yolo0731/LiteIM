#include "liteim/net/EventLoopThread.hpp"

#include "liteim/net/EventLoop.hpp"

#include <stdexcept>
#include <utility>

namespace liteim {

EventLoopThread::~EventLoopThread() {
    stop();
}

EventLoop* EventLoopThread::startLoop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_) {
            throw std::logic_error("EventLoopThread has already been started");
        }
        started_ = true;
        running_ = true;
    }

    thread_ = std::thread([this]() { threadFunc(); });

    // 用unique_lock主线程会在condition_.wait时候暂时释放锁，等待线程函数threadFunc设置loop_指针或者发生异常
    std::unique_lock<std::mutex> lock(mutex_);

    // 检查条件和进入睡眠必须和 mutex_ 配合,等待的这些变量也都是被 mutex_ 保护的共享变量
    // notify_all()让等待线程醒来，然后重新检查第二个参数的条件是否满足，如果满足就继续执行，否则继续等待
    condition_.wait(lock, [this]() { return loop_ != nullptr || startup_exception_ != nullptr || !running_; });

    if (startup_exception_ != nullptr) {
        // utility头文件中的函数，交换两个对象的值，并返回旧值
        const auto exception = std::exchange(startup_exception_, nullptr);
        // std::unique_lock可以在需要时手动释放锁，且在wait期间会自动释放锁，等待条件满足后会重新获取锁
        lock.unlock();
        if (thread_.joinable()) {
            thread_.join();
        }
        std::rethrow_exception(exception);
    }

    return loop_;
}

void EventLoopThread::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loop_ != nullptr) {
            loop_->quit();
        }
    }

    try {
        if (thread_.joinable()) {
            if (thread_.get_id() == std::this_thread::get_id()) {
                thread_.detach();
                // 如果当前线程就是子线程，不能join自己，否则会导致死锁，所以选择detach让线程独立运行，直到自然结束
            } else {
                thread_.join();
            }
        }
    } catch (...) {
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = nullptr;
        running_ = false;
    }
}

bool EventLoopThread::running() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

void EventLoopThread::threadFunc() noexcept {
    try {
        EventLoop loop;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop_ = &loop;
        }
        condition_.notify_all();

        loop.loop(); // 子线程进入事件循环，直到调用quit()退出循环

        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop_ = nullptr;
            running_ = false; // 这个 EventLoopThread 管理的子线程不再运行了
        }
        condition_.notify_all();
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop_ = nullptr;
            running_ = false;
            // 捕获异常并保存到成员变量startup_exception_中
            startup_exception_ = std::current_exception();
        }
        condition_.notify_all();
    }
}

} // namespace liteim
