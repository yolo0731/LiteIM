#include "liteim/base/Config.hpp"
#include "liteim/storage/MessageDao.hpp"
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"
#include "liteim/storage/OfflineMessageDao.hpp"
#include "liteim/storage/UserDao.hpp"

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

std::string uniqueStep26Suffix() {
    static std::atomic<int> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(ticks) + "_" + std::to_string(counter.fetch_add(1));
}

std::string uniqueUsername() {
    return "step26_user_" + uniqueStep26Suffix();
}

std::string uniqueMessageText(const std::string& label) {
    return "step26_" + label + "_" + uniqueStep26Suffix();
}

std::uint64_t uniqueConversationId() {
    static std::atomic<std::uint64_t> counter{0};
    const auto ticks =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    return 6000000000ULL + (ticks % 1000000ULL) + counter.fetch_add(1);
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

void cleanupStep26Rows(const liteim::MySqlConfig& config) {
    liteim::MySqlConnection connection;
    const auto connect_status = connection.connect(config);
    if (!connect_status.isOk()) {
        return;
    }

    executeCleanupSql(connection,
                      "DELETE FROM offline_messages "
                      "WHERE message_id IN ("
                      "SELECT message_id FROM messages WHERE message_text LIKE 'step26\\_%')");
    executeCleanupSql(connection, "DELETE FROM messages WHERE message_text LIKE 'step26\\_%'");
    executeCleanupSql(connection, "DELETE FROM users WHERE username LIKE 'step26\\_%'");
}

class MessageDaoIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config = testMySqlConfig();

        liteim::MySqlConnection probe;
        const auto status = probe.connect(config);
        if (!status.isOk()) {
            GTEST_SKIP() << "local LiteIM MySQL is not available: " << status.message();
        }

        cleanupStep26Rows(config);
        pool = std::make_unique<liteim::MySqlPool>(config);
        ASSERT_TRUE(pool->start().isOk());
        user_dao = std::make_unique<liteim::UserDao>(*pool);
        message_dao = std::make_unique<liteim::MessageDao>(*pool);
        offline_message_dao = std::make_unique<liteim::OfflineMessageDao>(*pool);
    }

    void TearDown() override {
        offline_message_dao.reset();
        message_dao.reset();
        user_dao.reset();
        if (pool) {
            pool->close();
            pool.reset();
        }
        cleanupStep26Rows(config);
    }

    liteim::UserRecord createUser() {
        liteim::UserRecord user;
        const auto request = makeUserRequest();
        const auto status = user_dao->createUser(request, user);
        EXPECT_TRUE(status.isOk()) << status.message();
        return user;
    }

    liteim::MessageRecord makePrivateMessage(const liteim::UserRecord& sender,
                                             const liteim::UserRecord& receiver,
                                             std::uint64_t conversation_id, const std::string& text,
                                             std::int64_t created_at_ms) {
        liteim::MessageRecord message;
        message.conversation = {liteim::ConversationType::kPrivate, conversation_id};
        message.sender_id = sender.user_id;
        message.receiver_id = receiver.user_id;
        message.text = text;
        message.created_at_ms = created_at_ms;
        return message;
    }

    liteim::MySqlConfig config;
    std::unique_ptr<liteim::MySqlPool> pool;
    std::unique_ptr<liteim::UserDao> user_dao;
    std::unique_ptr<liteim::MessageDao> message_dao;
    std::unique_ptr<liteim::OfflineMessageDao> offline_message_dao;
};

}  // namespace

TEST(MessageDaoTest, HeadersAreSelfContained) {
    liteim::MySqlPool pool(testMySqlConfig());
    liteim::MessageDao message_dao(pool);
    liteim::OfflineMessageDao offline_message_dao(pool);
}

TEST_F(MessageDaoIntegrationTest, SavePrivateMessagePersistsRecord) {
    const auto sender = createUser();
    const auto receiver = createUser();
    const auto conversation_id = uniqueConversationId();
    const auto text = uniqueMessageText("private");
    const auto message = makePrivateMessage(sender, receiver, conversation_id, text, 1001);

    liteim::MessageRecord saved;
    const auto status = message_dao->savePrivateMessage(message, saved);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_GE(saved.message_id, 10000U);
    EXPECT_EQ(saved.conversation.type, liteim::ConversationType::kPrivate);
    EXPECT_EQ(saved.conversation.id, conversation_id);
    EXPECT_EQ(saved.sender_id, sender.user_id);
    EXPECT_EQ(saved.receiver_id, receiver.user_id);
    EXPECT_EQ(saved.text, text);
    EXPECT_EQ(saved.created_at_ms, 1001);
}

TEST_F(MessageDaoIntegrationTest, SaveGroupMessagePersistsRecord) {
    const auto sender = createUser();
    const auto group_id = uniqueConversationId();
    const auto text = uniqueMessageText("group");

    liteim::MessageRecord message;
    message.conversation = {liteim::ConversationType::kGroup, group_id};
    message.sender_id = sender.user_id;
    message.text = text;
    message.created_at_ms = 2001;

    liteim::MessageRecord saved;
    const auto status = message_dao->saveGroupMessage(message, saved);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_GE(saved.message_id, 10000U);
    EXPECT_EQ(saved.conversation.type, liteim::ConversationType::kGroup);
    EXPECT_EQ(saved.conversation.id, group_id);
    EXPECT_EQ(saved.sender_id, sender.user_id);
    EXPECT_EQ(saved.receiver_id, group_id);
    EXPECT_EQ(saved.text, text);
    EXPECT_EQ(saved.created_at_ms, 2001);
}

TEST_F(MessageDaoIntegrationTest, OfflineMessageSaveFetchAndDeliveredFlow) {
    const auto sender = createUser();
    const auto receiver = createUser();
    const auto message = makePrivateMessage(sender, receiver, uniqueConversationId(),
                                            uniqueMessageText("offline"), 3001);

    liteim::MessageRecord saved_message;
    ASSERT_TRUE(message_dao->savePrivateMessage(message, saved_message).isOk());

    const auto save_status =
        offline_message_dao->saveOfflineMessage(receiver.user_id, saved_message.message_id);
    ASSERT_TRUE(save_status.isOk()) << save_status.message();

    std::vector<liteim::OfflineMessageRecord> pending;
    const auto fetch_status = offline_message_dao->getOfflineMessages(receiver.user_id, 100, pending);
    ASSERT_TRUE(fetch_status.isOk()) << fetch_status.message();
    ASSERT_EQ(pending.size(), 1U);
    EXPECT_GE(pending.front().offline_message_id, 10000U);
    EXPECT_EQ(pending.front().user_id, receiver.user_id);
    EXPECT_EQ(pending.front().message.message_id, saved_message.message_id);
    EXPECT_EQ(pending.front().message.text, saved_message.text);
    EXPECT_GT(pending.front().created_at_ms, 0);

    const auto mark_status =
        offline_message_dao->markOfflineDelivered(receiver.user_id, {saved_message.message_id});
    ASSERT_TRUE(mark_status.isOk()) << mark_status.message();

    pending.clear();
    const auto refetch_status =
        offline_message_dao->getOfflineMessages(receiver.user_id, 100, pending);
    ASSERT_TRUE(refetch_status.isOk()) << refetch_status.message();
    EXPECT_TRUE(pending.empty());
}

TEST_F(MessageDaoIntegrationTest, HistoryReturnsNewestMessagesBeforeCursor) {
    const auto sender = createUser();
    const auto receiver = createUser();
    const auto conversation_id = uniqueConversationId();

    std::vector<liteim::MessageRecord> saved_messages;
    for (int index = 0; index < 3; ++index) {
        const auto message =
            makePrivateMessage(sender, receiver, conversation_id,
                               uniqueMessageText("history_" + std::to_string(index)), 4000 + index);
        liteim::MessageRecord saved;
        ASSERT_TRUE(message_dao->savePrivateMessage(message, saved).isOk());
        saved_messages.push_back(saved);
    }

    liteim::HistoryQuery first_page;
    first_page.conversation = {liteim::ConversationType::kPrivate, conversation_id};
    first_page.limit = 2;

    std::vector<liteim::MessageRecord> history;
    const auto first_status = message_dao->getHistoryByConversation(first_page, history);
    ASSERT_TRUE(first_status.isOk()) << first_status.message();
    ASSERT_EQ(history.size(), 2U);
    EXPECT_EQ(history[0].message_id, saved_messages[2].message_id);
    EXPECT_EQ(history[1].message_id, saved_messages[1].message_id);

    liteim::HistoryQuery second_page = first_page;
    second_page.before_message_id = history[1].message_id;
    history.clear();
    const auto second_status = message_dao->getHistoryByConversation(second_page, history);
    ASSERT_TRUE(second_status.isOk()) << second_status.message();
    ASSERT_EQ(history.size(), 1U);
    EXPECT_EQ(history[0].message_id, saved_messages[0].message_id);
}

TEST_F(MessageDaoIntegrationTest, HistoryLimitIsCappedAtFifty) {
    const auto sender = createUser();
    const auto receiver = createUser();
    const auto conversation_id = uniqueConversationId();

    for (int index = 0; index < 55; ++index) {
        const auto message =
            makePrivateMessage(sender, receiver, conversation_id,
                               uniqueMessageText("limit_" + std::to_string(index)), 5000 + index);
        liteim::MessageRecord saved;
        ASSERT_TRUE(message_dao->savePrivateMessage(message, saved).isOk());
    }

    liteim::HistoryQuery query;
    query.conversation = {liteim::ConversationType::kPrivate, conversation_id};
    query.limit = 100;

    std::vector<liteim::MessageRecord> history;
    const auto status = message_dao->getHistoryByConversation(query, history);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(history.size(), 50U);
    EXPECT_TRUE(
        std::is_sorted(history.begin(), history.end(), [](const auto& left, const auto& right) {
            return left.message_id > right.message_id;
        }));
}
