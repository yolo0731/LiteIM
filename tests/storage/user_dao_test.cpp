#include "liteim/base/Config.hpp"
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"
#include "liteim/storage/UserDao.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;

liteim::MySqlConfig testMySqlConfig(std::uint32_t pool_size = 2) {
    auto config = liteim::Config::defaults().mysql;
    config.pool_size = pool_size;
    return config;
}

std::string uniqueUsername() {
    static std::atomic<int> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return "step25_" + std::to_string(ticks) + "_" + std::to_string(counter.fetch_add(1));
}

liteim::CreateUserRequest makeCreateUserRequest() {
    const auto username = uniqueUsername();
    return liteim::CreateUserRequest{
        username,
        "hash_" + username,
        "salt_" + username,
        "Nick " + username,
    };
}

void cleanupStep25Users(const liteim::MySqlConfig& config) {
    liteim::MySqlConnection connection;
    const auto connect_status = connection.connect(config);
    if (!connect_status.isOk()) {
        return;
    }

    liteim::PreparedStatement statement(connection);
    if (!statement.prepare("DELETE FROM users WHERE username LIKE 'step25\\_%'").isOk()) {
        return;
    }

    std::uint64_t affected_rows = 0;
    (void)statement.executeUpdate(affected_rows);
}

class UserDaoIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config = testMySqlConfig();

        liteim::MySqlConnection probe;
        const auto status = probe.connect(config);
        if (!status.isOk()) {
            GTEST_SKIP() << "local LiteIM MySQL is not available: " << status.message();
        }

        cleanupStep25Users(config);
        pool = std::make_unique<liteim::MySqlPool>(config);
        ASSERT_TRUE(pool->start().isOk());
        user_dao = std::make_unique<liteim::UserDao>(*pool);
    }

    void TearDown() override {
        user_dao.reset();
        if (pool) {
            pool->close();
            pool.reset();
        }
        cleanupStep25Users(config);
    }

    liteim::MySqlConfig config;
    std::unique_ptr<liteim::MySqlPool> pool;
    std::unique_ptr<liteim::UserDao> user_dao;
};

}  // namespace

TEST(UserDaoTest, HeadersAreSelfContained) {
    liteim::MySqlPool pool(testMySqlConfig());
    liteim::UserDao user_dao(pool);
}

TEST_F(UserDaoIntegrationTest, CreateUserPersistsAndReturnsCreatedRecord) {
    const auto request = makeCreateUserRequest();

    liteim::UserRecord created;
    const auto status = user_dao->createUser(request, created);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_GE(created.user_id, 10000U);
    EXPECT_EQ(created.username, request.username);
    EXPECT_EQ(created.password_hash, request.password_hash);
    EXPECT_EQ(created.password_salt, request.password_salt);
    EXPECT_EQ(created.nickname, request.nickname);
    EXPECT_GT(created.created_at_ms, 0);
}

TEST_F(UserDaoIntegrationTest, CreateUserWorksWithSingleConnectionPool) {
    liteim::MySqlPool single_connection_pool(testMySqlConfig(1));
    ASSERT_TRUE(single_connection_pool.start().isOk());
    liteim::UserDao single_connection_user_dao(single_connection_pool);

    const auto request = makeCreateUserRequest();
    liteim::UserRecord created;
    const auto status = single_connection_user_dao.createUser(request, created);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(created.username, request.username);
}

TEST_F(UserDaoIntegrationTest, DuplicateUsernameReturnsAlreadyExists) {
    const auto request = makeCreateUserRequest();
    liteim::UserRecord created;
    ASSERT_TRUE(user_dao->createUser(request, created).isOk());

    liteim::UserRecord duplicate;
    const auto status = user_dao->createUser(request, duplicate);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::AlreadyExists);
    EXPECT_EQ(duplicate.user_id, 0U);
}

TEST_F(UserDaoIntegrationTest, FindUserByUsernameReturnsCreatedUser) {
    const auto request = makeCreateUserRequest();
    liteim::UserRecord created;
    ASSERT_TRUE(user_dao->createUser(request, created).isOk());

    liteim::UserRecord found;
    const auto status = user_dao->findUserByUsername(request.username, found);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(found.user_id, created.user_id);
    EXPECT_EQ(found.username, request.username);
    EXPECT_EQ(found.password_hash, request.password_hash);
    EXPECT_EQ(found.password_salt, request.password_salt);
    EXPECT_EQ(found.nickname, request.nickname);
    EXPECT_EQ(found.created_at_ms, created.created_at_ms);
}

TEST_F(UserDaoIntegrationTest, FindUserByIdReturnsCreatedUser) {
    const auto request = makeCreateUserRequest();
    liteim::UserRecord created;
    ASSERT_TRUE(user_dao->createUser(request, created).isOk());

    liteim::UserRecord found;
    const auto status = user_dao->findUserById(created.user_id, found);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(found.user_id, created.user_id);
    EXPECT_EQ(found.username, request.username);
}

TEST_F(UserDaoIntegrationTest, FindMissingUserReturnsNotFound) {
    liteim::UserRecord found;
    const auto status = user_dao->findUserByUsername(uniqueUsername(), found);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::NotFound);
    EXPECT_EQ(found.user_id, 0U);
}
