#include "liteim/concurrency/ThreadPool.hpp"

#include "liteim/base/ErrorCode.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

TEST(ThreadPoolTest, StartRejectsZeroWorkers) {
    liteim::ThreadPool pool(0);

    const auto status = pool.start();

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_FALSE(pool.started());
    EXPECT_EQ(pool.workerCount(), 0U);
}

TEST(ThreadPoolTest, SubmitExecutesTask) {
    liteim::ThreadPool pool(2);
    const auto start_status = pool.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();

    std::mutex mutex;
    std::condition_variable condition;
    int executed = 0;

    const auto submit_status = pool.submit([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        ++executed;
        condition.notify_all();
    });

    ASSERT_TRUE(submit_status.isOk()) << submit_status.message();

    std::unique_lock<std::mutex> lock(mutex);
    EXPECT_TRUE(condition.wait_for(lock, 1s, [&]() { return executed == 1; }));

    pool.stop();
}

TEST(ThreadPoolTest, MultipleWorkersRunConcurrently) {
    liteim::ThreadPool pool(3);
    const auto start_status = pool.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();

    std::mutex mutex;
    std::condition_variable condition;
    std::set<std::thread::id> worker_ids;
    std::size_t started = 0;
    bool release = false;

    for (std::size_t i = 0; i < 3; ++i) {
        const auto submit_status = pool.submit([&]() {
            std::unique_lock<std::mutex> lock(mutex);
            worker_ids.insert(std::this_thread::get_id());
            ++started;
            condition.notify_all();
            condition.wait(lock, [&]() { return release; });
        });
        ASSERT_TRUE(submit_status.isOk()) << submit_status.message();
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, 1s, [&]() { return started == 3; }));
        EXPECT_EQ(worker_ids.size(), 3U);
        EXPECT_EQ(worker_ids.count(std::this_thread::get_id()), 0U);
        release = true;
    }
    condition.notify_all();

    pool.stop();
}

TEST(ThreadPoolTest, StopRejectsNewTasks) {
    liteim::ThreadPool pool(1);
    const auto start_status = pool.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();

    pool.stop();

    const auto status = pool.submit([]() {});
    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
}

TEST(ThreadPoolTest, StopBeforeStartIsNoopAndSubmitStillRejected) {
    liteim::ThreadPool pool(1);

    pool.stop();

    EXPECT_FALSE(pool.started());
    EXPECT_EQ(pool.pendingTaskCount(), 0U);
    const auto submit_status = pool.submit([]() {});
    EXPECT_FALSE(submit_status.isOk());
    EXPECT_EQ(submit_status.code(), liteim::ErrorCode::InvalidArgument);
}

TEST(ThreadPoolTest, StopWithEmptyQueueAllowsCleanRestart) {
    liteim::ThreadPool pool(1);
    ASSERT_TRUE(pool.start().isOk());

    pool.stop();

    EXPECT_FALSE(pool.started());
    EXPECT_EQ(pool.pendingTaskCount(), 0U);
    ASSERT_TRUE(pool.start().isOk());
    pool.stop();
}

TEST(ThreadPoolTest, StopCalledFromWorkerRequiresOwnerCleanupBeforeRestart) {
    liteim::ThreadPool pool(1);
    const auto start_status = pool.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();

    std::mutex mutex;
    std::condition_variable condition;
    bool worker_stop_returned = false;

    const auto submit_status = pool.submit([&]() {
        pool.stop();
        {
            std::lock_guard<std::mutex> lock(mutex);
            worker_stop_returned = true;
        }
        condition.notify_all();
    });
    ASSERT_TRUE(submit_status.isOk()) << submit_status.message();

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, 1s, [&]() { return worker_stop_returned; }));
    }

    const auto restart_status_before_cleanup = pool.start();
    EXPECT_FALSE(restart_status_before_cleanup.isOk());
    if (restart_status_before_cleanup.isOk()) {
        pool.stop();
    }

    pool.stop();

    const auto restart_status_after_cleanup = pool.start();
    ASSERT_TRUE(restart_status_after_cleanup.isOk()) << restart_status_after_cleanup.message();
    pool.stop();
}

TEST(ThreadPoolTest, ConcurrentStopCallsAreSerialized) {
    liteim::ThreadPool pool(1);
    const auto start_status = pool.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();

    std::mutex mutex;
    std::condition_variable condition;
    bool task_started = false;
    bool release_task = false;
    int stop_callers_ready = 0;
    std::atomic<int> stop_callers_returned{0};

    const auto submit_status = pool.submit([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        task_started = true;
        condition.notify_all();
        condition.wait(lock, [&]() { return release_task; });
    });
    ASSERT_TRUE(submit_status.isOk()) << submit_status.message();

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, 1s, [&]() { return task_started; }));
    }

    auto stop_pool = [&]() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++stop_callers_ready;
        }
        condition.notify_all();
        pool.stop();
        stop_callers_returned.fetch_add(1);
        condition.notify_all();
    };

    std::thread first_stop(stop_pool);
    std::thread second_stop(stop_pool);

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, 1s, [&]() { return stop_callers_ready == 2; }));
    }

    std::this_thread::sleep_for(20ms);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_task = true;
    }
    condition.notify_all();

    first_stop.join();
    second_stop.join();

    EXPECT_EQ(stop_callers_returned.load(), 2);
    EXPECT_FALSE(pool.started());

    const auto restart_status = pool.start();
    ASSERT_TRUE(restart_status.isOk()) << restart_status.message();
    pool.stop();
}

TEST(ThreadPoolTest, DestructorWaitsForQueuedTasks) {
    std::atomic<int> completed{0};

    {
        liteim::ThreadPool pool(1);
        const auto start_status = pool.start();
        ASSERT_TRUE(start_status.isOk()) << start_status.message();

        const auto submit_status = pool.submit([&]() {
            std::this_thread::sleep_for(20ms);
            completed.fetch_add(1);
        });
        ASSERT_TRUE(submit_status.isOk()) << submit_status.message();
    }

    EXPECT_EQ(completed.load(), 1);
}

TEST(ThreadPoolTest, PendingTaskCountTracksQueuedTasks) {
    liteim::ThreadPool pool(1);
    const auto start_status = pool.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();

    std::mutex mutex;
    std::condition_variable condition;
    bool first_task_started = false;
    bool release_first_task = false;
    int completed = 0;

    const auto first_status = pool.submit([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        first_task_started = true;
        condition.notify_all();
        condition.wait(lock, [&]() { return release_first_task; });
        ++completed;
        condition.notify_all();
    });
    ASSERT_TRUE(first_status.isOk()) << first_status.message();

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, 1s, [&]() { return first_task_started; }));
    }

    const auto second_status = pool.submit([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        ++completed;
        condition.notify_all();
    });
    ASSERT_TRUE(second_status.isOk()) << second_status.message();

    const auto third_status = pool.submit([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        ++completed;
        condition.notify_all();
    });
    ASSERT_TRUE(third_status.isOk()) << third_status.message();

    EXPECT_EQ(pool.pendingTaskCount(), 2U);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_first_task = true;
    }
    condition.notify_all();

    {
        std::unique_lock<std::mutex> lock(mutex);
        EXPECT_TRUE(condition.wait_for(lock, 1s, [&]() { return completed == 3; }));
    }
    EXPECT_EQ(pool.pendingTaskCount(), 0U);

    pool.stop();
}

TEST(ThreadPoolTest, BoundedQueueRejectsWhenPendingTaskLimitIsReached) {
    liteim::ThreadPool pool(1, 1);
    const auto start_status = pool.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();

    std::mutex mutex;
    std::condition_variable condition;
    bool first_task_started = false;
    bool release_first_task = false;

    const auto first_status = pool.submit([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        first_task_started = true;
        condition.notify_all();
        condition.wait(lock, [&]() { return release_first_task; });
    });
    ASSERT_TRUE(first_status.isOk()) << first_status.message();

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, 1s, [&]() { return first_task_started; }));
    }

    const auto queued_status = pool.submit([]() {});
    ASSERT_TRUE(queued_status.isOk()) << queued_status.message();
    EXPECT_EQ(pool.pendingTaskCount(), 1U);

    const auto full_status = pool.submit([]() {});
    EXPECT_FALSE(full_status.isOk());
    EXPECT_EQ(full_status.code(), liteim::ErrorCode::ResourceExhausted);
    EXPECT_EQ(pool.pendingTaskCount(), 1U);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_first_task = true;
    }
    condition.notify_all();
    pool.stop();
}

TEST(ThreadPoolTest, ZeroPendingTaskLimitKeepsQueueUnbounded) {
    liteim::ThreadPool pool(1, 0);
    const auto start_status = pool.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();

    std::mutex mutex;
    std::condition_variable condition;
    bool first_task_started = false;
    bool release_first_task = false;

    ASSERT_TRUE(pool
                    .submit([&]() {
                        std::unique_lock<std::mutex> lock(mutex);
                        first_task_started = true;
                        condition.notify_all();
                        condition.wait(lock, [&]() { return release_first_task; });
                    })
                    .isOk());
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, 1s, [&]() { return first_task_started; }));
    }

    for (int i = 0; i < 4; ++i) {
        const auto status = pool.submit([]() {});
        EXPECT_TRUE(status.isOk()) << status.message();
    }
    EXPECT_EQ(pool.pendingTaskCount(), 4U);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_first_task = true;
    }
    condition.notify_all();
    pool.stop();
}
