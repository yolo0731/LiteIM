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
