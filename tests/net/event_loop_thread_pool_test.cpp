#include "liteim/net/EventLoopThreadPool.hpp"

#include "liteim/net/EventLoop.hpp"

#include <algorithm>
#include <future>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::thread::id runOnLoop(liteim::EventLoop* loop) {
    std::promise<std::thread::id> promise;
    auto future = promise.get_future();
    loop->queueInLoop([&promise]() { promise.set_value(std::this_thread::get_id()); });
    return future.get();
}

}  // namespace

TEST(EventLoopThreadPoolTest, StartCreatesRequestedNumberOfLoops) {
    liteim::EventLoop base_loop;
    liteim::EventLoopThreadPool pool(&base_loop, 3);

    pool.start();

    ASSERT_EQ(pool.threadCount(), 3U);
    ASSERT_TRUE(pool.started());
    ASSERT_EQ(pool.loops().size(), 3U);

    std::set<liteim::EventLoop*> unique_loops(pool.loops().begin(), pool.loops().end());
    EXPECT_EQ(unique_loops.size(), 3U);
    for (auto* loop : pool.loops()) {
        ASSERT_NE(loop, nullptr);
        EXPECT_FALSE(loop->isInLoopThread());
    }

    pool.stop();
    EXPECT_FALSE(pool.started());
}

TEST(EventLoopThreadPoolTest, GetNextLoopUsesRoundRobinOrder) {
    liteim::EventLoop base_loop;
    liteim::EventLoopThreadPool pool(&base_loop, 3);
    pool.start();

    const auto loops = pool.loops();
    ASSERT_EQ(loops.size(), 3U);

    EXPECT_EQ(pool.getNextLoop(), loops[0]);
    EXPECT_EQ(pool.getNextLoop(), loops[1]);
    EXPECT_EQ(pool.getNextLoop(), loops[2]);
    EXPECT_EQ(pool.getNextLoop(), loops[0]);
    EXPECT_EQ(pool.getNextLoop(), loops[1]);

    pool.stop();
}

TEST(EventLoopThreadPoolTest, ZeroThreadsReturnsBaseLoop) {
    liteim::EventLoop base_loop;
    liteim::EventLoopThreadPool pool(&base_loop, 0);

    pool.start();

    EXPECT_TRUE(pool.started());
    EXPECT_TRUE(pool.loops().empty());
    EXPECT_EQ(pool.getNextLoop(), &base_loop);
    EXPECT_EQ(pool.getNextLoop(), &base_loop);

    pool.stop();
    EXPECT_FALSE(pool.started());
}

TEST(EventLoopThreadPoolTest, ChildLoopsRunTasksOnDistinctThreads) {
    liteim::EventLoop base_loop;
    liteim::EventLoopThreadPool pool(&base_loop, 2);
    pool.start();

    std::vector<std::thread::id> thread_ids;
    for (auto* loop : pool.loops()) {
        thread_ids.push_back(runOnLoop(loop));
    }

    std::set<std::thread::id> unique_thread_ids(thread_ids.begin(), thread_ids.end());
    EXPECT_EQ(unique_thread_ids.size(), 2U);
    EXPECT_TRUE(std::none_of(thread_ids.begin(), thread_ids.end(),
                             [](const auto& id) { return id == std::this_thread::get_id(); }));

    pool.stop();
}
