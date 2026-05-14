#include "liteim/base/Config.hpp"
#include "liteim/storage/MySqlConnection.hpp"

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

namespace {

class MySqlIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto config = liteim::Config::defaults();
        const auto status = connection.connect(config.mysql);
        if (!status.isOk()) {
            GTEST_SKIP() << "local LiteIM MySQL is not available: " << status.message();
        }
    }

    liteim::MySqlConnection connection;
};

}  // namespace

TEST(MySqlConnectionTest, HeaderIsSelfContained) {
    liteim::MySqlConnection connection;
    EXPECT_FALSE(connection.isConnected());
}

TEST_F(MySqlIntegrationTest, ConnectsAndPingsLocalMySql) {
    EXPECT_TRUE(connection.isConnected());

    const auto status = connection.ping();

    EXPECT_TRUE(status.isOk()) << status.message();
}

TEST_F(MySqlIntegrationTest, PreparedStatementExecutesSimpleSelect) {
    liteim::PreparedStatement statement(connection);
    ASSERT_TRUE(statement.prepare("SELECT username FROM users WHERE user_id = ?").isOk());
    ASSERT_TRUE(statement.bindInt64(0, 1001).isOk());

    liteim::MySqlQueryResult result;
    const auto status = statement.executeQuery(result);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_EQ(result.rows().size(), 1U);
    ASSERT_EQ(result.rows().front().values.size(), 1U);
    ASSERT_TRUE(result.rows().front().values.front().has_value());
    EXPECT_EQ(*result.rows().front().values.front(), "alice");
}

TEST_F(MySqlIntegrationTest, ExecuteUpdateAndQueryRoundTripSpecialCharacters) {
    liteim::PreparedStatement create_table(connection);
    ASSERT_TRUE(create_table
                    .prepare("CREATE TEMPORARY TABLE liteim_step23_params ("
                             "id BIGINT NOT NULL PRIMARY KEY, "
                             "text_value TEXT NOT NULL)")
                    .isOk());
    std::uint64_t affected_rows = 0;
    ASSERT_TRUE(create_table.executeUpdate(affected_rows).isOk());

    const std::string special_text = "quote ' and injection OR 1=1 -- \\\\ newline\n中文";
    liteim::PreparedStatement insert(connection);
    ASSERT_TRUE(
        insert.prepare("INSERT INTO liteim_step23_params (id, text_value) VALUES (?, ?)").isOk());
    ASSERT_TRUE(insert.bindInt64(0, 7).isOk());
    ASSERT_TRUE(insert.bindString(1, special_text).isOk());
    ASSERT_TRUE(insert.executeUpdate(affected_rows).isOk());
    EXPECT_EQ(affected_rows, 1U);

    liteim::PreparedStatement query(connection);
    ASSERT_TRUE(
        query.prepare("SELECT text_value FROM liteim_step23_params WHERE text_value = ?").isOk());
    ASSERT_TRUE(query.bindString(0, special_text).isOk());

    liteim::MySqlQueryResult result;
    const auto query_status = query.executeQuery(result);

    ASSERT_TRUE(query_status.isOk()) << query_status.message();
    ASSERT_EQ(result.rows().size(), 1U);
    ASSERT_EQ(result.rows().front().values.size(), 1U);
    ASSERT_TRUE(result.rows().front().values.front().has_value());
    EXPECT_EQ(*result.rows().front().values.front(), special_text);
}

TEST_F(MySqlIntegrationTest, BindUInt64StoresUnsignedBigIntBeyondSignedRange) {
    liteim::PreparedStatement create_table(connection);
    ASSERT_TRUE(create_table
                    .prepare("CREATE TEMPORARY TABLE liteim_uint64_params ("
                             "id BIGINT UNSIGNED NOT NULL PRIMARY KEY)")
                    .isOk());
    std::uint64_t affected_rows = 0;
    ASSERT_TRUE(create_table.executeUpdate(affected_rows).isOk());

    constexpr std::uint64_t kBeyondSignedInt64 = 9223372036854775808ULL;
    liteim::PreparedStatement insert(connection);
    ASSERT_TRUE(insert.prepare("INSERT INTO liteim_uint64_params (id) VALUES (?)").isOk());
    ASSERT_TRUE(insert.bindUInt64(0, kBeyondSignedInt64).isOk());
    ASSERT_TRUE(insert.executeUpdate(affected_rows).isOk());
    EXPECT_EQ(affected_rows, 1U);

    liteim::PreparedStatement query(connection);
    ASSERT_TRUE(query.prepare("SELECT id FROM liteim_uint64_params WHERE id = ?").isOk());
    ASSERT_TRUE(query.bindUInt64(0, kBeyondSignedInt64).isOk());

    liteim::MySqlQueryResult result;
    const auto query_status = query.executeQuery(result);

    ASSERT_TRUE(query_status.isOk()) << query_status.message();
    ASSERT_EQ(result.rows().size(), 1U);
    ASSERT_EQ(result.rows().front().values.size(), 1U);
    ASSERT_TRUE(result.rows().front().values.front().has_value());
    EXPECT_EQ(*result.rows().front().values.front(), "9223372036854775808");
}

TEST_F(MySqlIntegrationTest, InvalidSqlReturnsErrorStatus) {
    liteim::PreparedStatement statement(connection);

    const auto status = statement.prepare("SELECT missing_column FROM missing_table");

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::IoError);
    EXPECT_FALSE(status.message().empty());
}
