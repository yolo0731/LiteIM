#include "liteim/net/EventLoopThread.hpp"

#include "liteim/net/EventLoop.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <gtest/gtest.h>

TEST(EventLoopThreadTest, StartLoopCreatesLoopOnWorkerThread) {
    liteim::EventLoopThread thread;
    auto* loop = thread.startLoop();

    ASSERT_NE(loop, nullptr);
    EXPECT_TRUE(thread.running());
    EXPECT_FALSE(loop->isInLoopThread());

    std::promise<std::thread::id> thread_id_promise;
    auto thread_id_future = thread_id_promise.get_future();
    loop->queueInLoop([loop, &thread_id_promise]() {
        thread_id_promise.set_value(std::this_thread::get_id());
        loop->quit();
    });

    const auto worker_thread_id = thread_id_future.get();
    EXPECT_NE(worker_thread_id, std::this_thread::get_id());

    thread.stop();
    EXPECT_FALSE(thread.running());
}

TEST(EventLoopThreadTest, StopWithoutStartIsNoop) {
    liteim::EventLoopThread thread;

    thread.stop();

    EXPECT_FALSE(thread.running());
}

TEST(EventLoopThreadTest, OwnerStopWaitsAfterStopIsRequestedInsideLoop) {
    liteim::EventLoopThread thread;
    auto* loop = thread.startLoop();
    ASSERT_NE(loop, nullptr);

    std::mutex mutex;
    std::condition_variable ready;
    bool self_stop_returned = false;
    bool task_finished = false;

    loop->queueInLoop([&]() {
        thread.stop();
        {
            std::lock_guard<std::mutex> lock(mutex);
            self_stop_returned = true;
        }
        ready.notify_one();

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        {
            std::lock_guard<std::mutex> lock(mutex);
            task_finished = true;
        }
        ready.notify_one();
    });

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(ready.wait_for(lock, std::chrono::seconds(2), [&]() { return self_stop_returned; }));
    }

    thread.stop();

    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_TRUE(task_finished);
    EXPECT_FALSE(thread.running());
}

TEST(EventLoopThreadTest, DestructorStopsRunningLoop) {
    std::atomic_bool ran{false};

    {
        liteim::EventLoopThread thread;
        auto* loop = thread.startLoop();
        ASSERT_NE(loop, nullptr);
        loop->queueInLoop([&ran]() { ran = true; });
    }

    EXPECT_TRUE(ran.load());
}
