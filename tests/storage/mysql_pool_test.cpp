#include "liteim/base/Config.hpp"
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;

liteim::MySqlConfig testMySqlConfig(std::uint32_t pool_size) {
    auto config = liteim::Config::defaults().mysql;
    config.pool_size = pool_size;
    return config;
}

class MySqlPoolIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        liteim::MySqlConnection probe;
        const auto status = probe.connect(testMySqlConfig(1));
        if (!status.isOk()) {
            GTEST_SKIP() << "local LiteIM MySQL is not available: " << status.message();
        }
    }
};

} // namespace

TEST(MySqlPoolTest, HeaderIsSelfContained) {
    liteim::MySqlPool pool(testMySqlConfig(1));
    EXPECT_FALSE(pool.started());
    EXPECT_FALSE(pool.closed());

    liteim::ConnectionGuard guard;
    EXPECT_FALSE(guard);
    EXPECT_EQ(guard.get(), nullptr);
}

TEST(MySqlPoolTest, RejectsZeroPoolSize) {
    liteim::MySqlPool pool(testMySqlConfig(0));

    const auto status = pool.start();

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_FALSE(pool.started());
}

TEST_F(MySqlPoolIntegrationTest, AcquiresConnectedConnection) {
    liteim::MySqlPool pool(testMySqlConfig(2));
    ASSERT_TRUE(pool.start().isOk());

    liteim::ConnectionGuard guard;
    const auto acquire_status = pool.acquire(200ms, guard);

    ASSERT_TRUE(acquire_status.isOk()) << acquire_status.message();
    ASSERT_TRUE(guard);
    ASSERT_NE(guard.get(), nullptr);
    EXPECT_TRUE(guard->ping().isOk());
    EXPECT_EQ(pool.size(), 2U);
}

TEST_F(MySqlPoolIntegrationTest, AcquireTimesOutWhenAllConnectionsAreBorrowed) {
    liteim::MySqlPool pool(testMySqlConfig(1));
    ASSERT_TRUE(pool.start().isOk());

    liteim::ConnectionGuard first;
    ASSERT_TRUE(pool.acquire(200ms, first).isOk());

    liteim::ConnectionGuard second;
    const auto status = pool.acquire(20ms, second);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::IoError);
    EXPECT_FALSE(second);
}

TEST_F(MySqlPoolIntegrationTest, ConnectionGuardReturnsConnectionOnDestruction) {
    liteim::MySqlPool pool(testMySqlConfig(1));
    ASSERT_TRUE(pool.start().isOk());

    {
        liteim::ConnectionGuard guard;
        ASSERT_TRUE(pool.acquire(200ms, guard).isOk());
        ASSERT_TRUE(guard);
    }

    liteim::ConnectionGuard next;
    const auto status = pool.acquire(200ms, next);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_TRUE(next);
}

TEST_F(MySqlPoolIntegrationTest, CloseMakesAcquireFail) {
    liteim::MySqlPool pool(testMySqlConfig(1));
    ASSERT_TRUE(pool.start().isOk());

    pool.close();

    liteim::ConnectionGuard guard;
    const auto status = pool.acquire(20ms, guard);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::InvalidArgument);
    EXPECT_TRUE(pool.closed());
    EXPECT_FALSE(guard);
}

TEST_F(MySqlPoolIntegrationTest, MultipleThreadsAcquireAndReleaseConnections) {
    liteim::MySqlPool pool(testMySqlConfig(2));
    ASSERT_TRUE(pool.start().isOk());

    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;
    for (int index = 0; index < 8; ++index) {
        threads.emplace_back([&]() {
            liteim::ConnectionGuard guard;
            const auto status = pool.acquire(1s, guard);
            if (status.isOk() && guard && guard->ping().isOk()) {
                ++success_count;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(success_count.load(), 8);
}

TEST_F(MySqlPoolIntegrationTest, ReconnectsConnectionThatWasClosedWhileBorrowed) {
    liteim::MySqlPool pool(testMySqlConfig(1));
    ASSERT_TRUE(pool.start().isOk());

    {
        liteim::ConnectionGuard guard;
        ASSERT_TRUE(pool.acquire(200ms, guard).isOk());
        ASSERT_TRUE(guard);
        guard->close();
    }

    liteim::ConnectionGuard next;
    const auto status = pool.acquire(500ms, next);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_TRUE(next);
    EXPECT_TRUE(next->ping().isOk());
}
