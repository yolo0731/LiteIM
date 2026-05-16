#include "liteim/base/Config.hpp"
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"
#include "liteim/storage/MySqlStorage.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

liteim::MySqlConfig testMySqlConfig(std::uint32_t pool_size = 3) {
    auto config = liteim::Config::defaults().mysql;
    config.pool_size = pool_size;
    return config;
}

std::string uniquePre31Suffix() {
    static std::atomic<int> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(ticks) + "_" + std::to_string(counter.fetch_add(1));
}

std::string uniqueUsername() {
    return "pre31_user_" + uniquePre31Suffix();
}

std::string uniqueMessageText(const std::string& label) {
    return "pre31_" + label + "_" + uniquePre31Suffix();
}

std::uint64_t uniqueConversationId() {
    static std::atomic<std::uint64_t> counter{0};
    const auto ticks =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    return 6100000000ULL + (ticks % 1000000ULL) + counter.fetch_add(1);
}

liteim::CreateUserRequest makeUserRequest() {
    const auto username = uniqueUsername();
    return liteim::CreateUserRequest{
        username,
        "hash_" + username,
        "salt_" + username,
        "Nick " + username,
    };
}

void executeCleanupSql(liteim::MySqlConnection& connection, const std::string& sql) {
    liteim::PreparedStatement statement(connection);
    if (!statement.prepare(sql).isOk()) {
        return;
    }

    std::uint64_t affected_rows = 0;
    (void)statement.executeUpdate(affected_rows);
}

void cleanupPre31Rows(const liteim::MySqlConfig& config) {
    liteim::MySqlConnection connection;
    const auto connect_status = connection.connect(config);
    if (!connect_status.isOk()) {
        return;
    }

    executeCleanupSql(connection,
                      "DELETE FROM offline_messages "
                      "WHERE message_id IN ("
                      "SELECT message_id FROM messages WHERE message_text LIKE 'pre31\\_%')");
    executeCleanupSql(connection, "DELETE FROM messages WHERE message_text LIKE 'pre31\\_%'");
    executeCleanupSql(
        connection, "DELETE FROM friendships "
                    "WHERE user_id IN (SELECT user_id FROM users WHERE username LIKE 'pre31\\_%') "
                    "OR friend_id IN (SELECT user_id FROM users WHERE username LIKE 'pre31\\_%')");
    executeCleanupSql(
        connection,
        "DELETE FROM group_members "
        "WHERE group_id IN (SELECT group_id FROM chat_groups WHERE group_name LIKE 'pre31\\_%') "
        "OR user_id IN (SELECT user_id FROM users WHERE username LIKE 'pre31\\_%')");
    executeCleanupSql(connection,
                      "DELETE FROM chat_groups "
                      "WHERE group_name LIKE 'pre31\\_%' "
                      "OR owner_id IN (SELECT user_id FROM users WHERE username LIKE 'pre31\\_%')");
    executeCleanupSql(connection, "DELETE FROM users WHERE username LIKE 'pre31\\_%'");
}

class MySqlStorageIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config = testMySqlConfig();

        liteim::MySqlConnection probe;
        const auto status = probe.connect(config);
        if (!status.isOk()) {
            GTEST_SKIP() << "local LiteIM MySQL is not available: " << status.message();
        }

        cleanupPre31Rows(config);
        pool = std::make_unique<liteim::MySqlPool>(config);
        ASSERT_TRUE(pool->start().isOk());
        storage = std::make_unique<liteim::MySqlStorage>(*pool);
    }

    void TearDown() override {
        storage.reset();
        if (pool) {
            pool->close();
            pool.reset();
        }
        cleanupPre31Rows(config);
    }

    liteim::UserRecord createUser() {
        liteim::UserRecord user;
        const auto request = makeUserRequest();
        const auto status = storage->createUser(request, user);
        EXPECT_TRUE(status.isOk()) << status.message();
        return user;
    }

    liteim::MessageRecord makePrivateMessage(const liteim::UserRecord& sender,
                                             const liteim::UserRecord& receiver,
                                             std::uint64_t conversation_id,
                                             const std::string& text) {
        liteim::MessageRecord message;
        message.conversation = {liteim::ConversationType::kPrivate, conversation_id};
        message.sender_id = sender.user_id;
        message.receiver_id = receiver.user_id;
        message.text = text;
        message.created_at_ms = 7001;
        return message;
    }

    liteim::MySqlConfig config;
    std::unique_ptr<liteim::MySqlPool> pool;
    std::unique_ptr<liteim::MySqlStorage> storage;
};

}  // namespace

TEST(MySqlStorageTest, HeaderIsSelfContained) {
    liteim::MySqlPool pool(testMySqlConfig());
    liteim::MySqlStorage storage(pool);
    liteim::IStorage* interface = &storage;
    EXPECT_NE(interface, nullptr);
}

TEST_F(MySqlStorageIntegrationTest, ImplementsIStorageForUsersFriendsAndPublicProfiles) {
    const auto alice = createUser();
    const auto bob = createUser();
    liteim::IStorage& interface = *storage;

    const auto add_status = interface.addFriendship(alice.user_id, bob.user_id);
    ASSERT_TRUE(add_status.isOk()) << add_status.message();

    std::vector<liteim::UserProfileRecord> friends;
    const auto friends_status = interface.getFriends(alice.user_id, friends);
    ASSERT_TRUE(friends_status.isOk()) << friends_status.message();
    ASSERT_EQ(friends.size(), 1U);
    EXPECT_EQ(friends.front().user_id, bob.user_id);
    EXPECT_EQ(friends.front().username, bob.username);
    EXPECT_EQ(friends.front().nickname, bob.nickname);
}

TEST_F(MySqlStorageIntegrationTest, SaveMessageWithOfflineRecipientsCommitsBothTables) {
    const auto sender = createUser();
    const auto receiver = createUser();
    const auto message = makePrivateMessage(sender, receiver, uniqueConversationId(),
                                            uniqueMessageText("combined_commit"));

    liteim::MessageRecord saved;
    const auto status =
        storage->saveMessageWithOfflineRecipients(message, {receiver.user_id}, saved);
    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_GE(saved.message_id, 10000U);
    EXPECT_EQ(saved.text, message.text);

    std::vector<liteim::MessageRecord> history;
    liteim::HistoryQuery query;
    query.conversation = message.conversation;
    query.limit = 10;
    ASSERT_TRUE(storage->getHistory(query, history).isOk());
    ASSERT_EQ(history.size(), 1U);
    EXPECT_EQ(history.front().message_id, saved.message_id);

    std::vector<liteim::OfflineMessageRecord> pending;
    ASSERT_TRUE(storage->getOfflineMessages(receiver.user_id, 100, pending).isOk());
    ASSERT_EQ(pending.size(), 1U);
    EXPECT_EQ(pending.front().message.message_id, saved.message_id);
}

TEST_F(MySqlStorageIntegrationTest,
       SaveMessageWithOfflineRecipientsRollsBackMessageWhenOfflineInsertFails) {
    const auto sender = createUser();
    const auto receiver = createUser();
    const auto conversation_id = uniqueConversationId();
    const auto text = uniqueMessageText("combined_rollback");
    const auto message = makePrivateMessage(sender, receiver, conversation_id, text);

    liteim::MessageRecord saved;
    const auto status =
        storage->saveMessageWithOfflineRecipients(message, {999999999999ULL}, saved);
    ASSERT_FALSE(status.isOk());
    EXPECT_EQ(saved.message_id, 0U);
    EXPECT_TRUE(saved.text.empty());

    std::vector<liteim::MessageRecord> history;
    liteim::HistoryQuery query;
    query.conversation = {liteim::ConversationType::kPrivate, conversation_id};
    query.limit = 10;
    const auto history_status = storage->getHistory(query, history);
    ASSERT_TRUE(history_status.isOk()) << history_status.message();
    EXPECT_TRUE(history.empty());
}

TEST_F(MySqlStorageIntegrationTest, SaveMessageWithOfflineRecipientsDeduplicatesOfflineUsers) {
    const auto sender = createUser();
    const auto receiver = createUser();
    const auto message = makePrivateMessage(sender, receiver, uniqueConversationId(),
                                            uniqueMessageText("combined_dedup"));

    liteim::MessageRecord saved;
    const auto status = storage->saveMessageWithOfflineRecipients(
        message, {receiver.user_id, receiver.user_id}, saved);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_GE(saved.message_id, 10000U);

    std::vector<liteim::OfflineMessageRecord> pending;
    const auto offline_status = storage->getOfflineMessages(receiver.user_id, 100, pending);
    ASSERT_TRUE(offline_status.isOk()) << offline_status.message();
    ASSERT_EQ(pending.size(), 1U);
    EXPECT_EQ(pending.front().message.message_id, saved.message_id);
}
