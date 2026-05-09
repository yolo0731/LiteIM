#include "liteim/concurrency/ThreadPool.hpp"

#include "liteim/base/ErrorCode.hpp"

#include <exception>
#include <utility>

namespace liteim {

namespace {

thread_local ThreadPool* current_thread_pool = nullptr;

class CurrentThreadPoolGuard {
public:
    explicit CurrentThreadPoolGuard(ThreadPool* pool) noexcept : previous_(current_thread_pool) {
        current_thread_pool = pool;
    }

    ~CurrentThreadPoolGuard() noexcept {
        current_thread_pool = previous_;
    }

    CurrentThreadPoolGuard(const CurrentThreadPoolGuard&) = delete;
    CurrentThreadPoolGuard& operator=(const CurrentThreadPoolGuard&) = delete;

private:
    ThreadPool* previous_{nullptr};
};

} // namespace

ThreadPool::ThreadPool(std::size_t worker_count) : worker_count_(worker_count) {
}

ThreadPool::~ThreadPool() {
    stop();
}

Status ThreadPool::start() {
    if (worker_count_ == 0) {
        return Status::error(ErrorCode::InvalidArgument, "thread pool worker count must be greater than zero");
    }

    std::unique_lock<std::mutex> lock(mutex_);
    if (running_.load() || !workers_.empty()) {
        return Status::error(ErrorCode::InvalidArgument, "thread pool has already started");
    }

    running_.store(true);

    try {
        workers_.reserve(worker_count_);
        for (std::size_t i = 0; i < worker_count_; ++i) {
            workers_.emplace_back([this]() { workerLoop(); });
        }
    } catch (...) {
        running_.store(false);
        lock.unlock(); // 先解锁再通知，避免工作线程在 join 时死锁
        condition_.notify_all();

        std::lock_guard<std::mutex> stop_lock(stop_mutex_);
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        {
            std::lock_guard<std::mutex> cleanup_lock(mutex_);
            workers_.clear();
            tasks_.clear();
        }
        return Status::error(ErrorCode::InternalError, "failed to start thread pool workers");
    }

    return Status::ok();
}

Status ThreadPool::submit(Task task) {
    if (!task) {
        return Status::error(ErrorCode::InvalidArgument, "thread pool task must not be empty");
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load()) {
            return Status::error(ErrorCode::InvalidArgument, "thread pool is not accepting new tasks");
        }
        tasks_.push_back(std::move(task));
    }

    condition_.notify_one();
    return Status::ok();
}

void ThreadPool::stop() noexcept {
    const bool called_from_worker = current_thread_pool == this;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load() && workers_.empty()) {
            return;
        }
        running_.store(false);
    }

    condition_.notify_all();

    if (called_from_worker) {
        return;
    }

    std::lock_guard<std::mutex> stop_lock(stop_mutex_);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (workers_.empty()) {
            return;
        }
    }

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        workers_.clear();
    }
}

std::size_t ThreadPool::workerCount() const noexcept {
    return worker_count_;
}

std::size_t ThreadPool::pendingTaskCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

bool ThreadPool::started() const noexcept {
    return running_.load();
}

void ThreadPool::workerLoop() noexcept {
    CurrentThreadPoolGuard guard(this);
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() { return !running_.load() || !tasks_.empty(); });
            if (tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }

        try {
            task();
        } catch (...) {
        }
    }
}

} // namespace liteim
