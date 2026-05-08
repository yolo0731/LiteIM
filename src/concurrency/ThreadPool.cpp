#include "liteim/concurrency/ThreadPool.hpp"

#include "liteim/base/ErrorCode.hpp"

#include <exception>
#include <utility>

namespace liteim {

ThreadPool::ThreadPool(std::size_t worker_count) : worker_count_(worker_count) {}

ThreadPool::~ThreadPool() {
    stop();
}

Status ThreadPool::start() {
    if (worker_count_ == 0) {
        return Status::error(ErrorCode::InvalidArgument, "thread pool worker count must be greater than zero");
    }

    std::unique_lock<std::mutex> lock(mutex_);
    if (started_.load()) {
        return Status::error(ErrorCode::InvalidArgument, "thread pool has already started");
    }

    stopping_ = false;
    started_.store(true);

    try {
        workers_.reserve(worker_count_);
        for (std::size_t i = 0; i < worker_count_; ++i) {
            workers_.emplace_back([this]() { workerLoop(); });
        }
    } catch (...) {
        stopping_ = true;
        lock.unlock();
        condition_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        lock.lock();
        workers_.clear();
        tasks_.clear();
        stopping_ = false;
        started_.store(false);
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
        if (!started_.load() || stopping_) {
            return Status::error(ErrorCode::InvalidArgument, "thread pool is not accepting new tasks");
        }
        tasks_.push_back(std::move(task));
    }

    condition_.notify_one();
    return Status::ok();
}

void ThreadPool::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_.load() && workers_.empty()) {
            return;
        }
        stopping_ = true;
    }

    condition_.notify_all();

    for (auto& worker : workers_) {
        if (!worker.joinable()) {
            continue;
        }
        if (worker.get_id() == std::this_thread::get_id()) {
            worker.detach();
        } else {
            worker.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        workers_.clear();
        stopping_ = false;
        started_.store(false);
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
    return started_.load();
}

void ThreadPool::workerLoop() noexcept {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() { return stopping_ || !tasks_.empty(); });
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
