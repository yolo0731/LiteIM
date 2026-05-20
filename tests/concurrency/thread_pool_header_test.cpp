#include "liteim/concurrency/ThreadPool.hpp"

#include "liteim/base/Status.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>

#include <gtest/gtest.h>

TEST(ConcurrencyInterfaceTest, ThreadPoolHeaderIsSelfContained) {
    static_assert(!std::is_copy_constructible_v<liteim::ThreadPool>);
    static_assert(!std::is_copy_assignable_v<liteim::ThreadPool>);

    using ThreadPool = liteim::ThreadPool;
    using Task = std::function<void()>;

    static_assert(std::is_same_v<ThreadPool::Task, Task>);
    static_assert(std::is_constructible_v<ThreadPool, std::size_t>);
    static_assert(std::is_constructible_v<ThreadPool, std::size_t, std::size_t>);
    static_assert(std::is_same_v<decltype(&ThreadPool::start), liteim::Status (ThreadPool::*)()>);
    static_assert(
        std::is_same_v<decltype(&ThreadPool::submit), liteim::Status (ThreadPool::*)(Task)>);
    static_assert(std::is_same_v<decltype(&ThreadPool::stop), void (ThreadPool::*)() noexcept>);
    static_assert(std::is_same_v<decltype(&ThreadPool::workerCount),
                                 std::size_t (ThreadPool::*)() const noexcept>);
    static_assert(std::is_same_v<decltype(&ThreadPool::maxPendingTaskCount),
                                 std::size_t (ThreadPool::*)() const noexcept>);
    static_assert(std::is_same_v<decltype(&ThreadPool::pendingTaskCount),
                                 std::size_t (ThreadPool::*)() const>);
    static_assert(
        std::is_same_v<decltype(&ThreadPool::started), bool (ThreadPool::*)() const noexcept>);

    SUCCEED();
}
