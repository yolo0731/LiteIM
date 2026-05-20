#pragma once

#include "liteim/base/Status.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace liteim {

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t worker_count, std::size_t max_pending_tasks = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    Status start();
    Status submit(Task task);
    void stop() noexcept;

    std::size_t workerCount() const noexcept;
    std::size_t maxPendingTaskCount() const noexcept;
    std::size_t pendingTaskCount() const;
    bool started() const noexcept;

private:
    void workerLoop() noexcept;

    std::size_t worker_count_{0};
    std::size_t max_pending_tasks_{0};
    mutable std::mutex mutex_;
    std::mutex stop_mutex_;
    std::condition_variable condition_;
    std::deque<Task> tasks_;
    std::vector<std::thread> workers_;
    std::atomic_bool running_{false};
};

}  // namespace liteim
