#include "liteim/base/Config.hpp"
#include "liteim/storage/FriendDao.hpp"
#include "liteim/storage/GroupDao.hpp"
#include "liteim/storage/MySqlConnection.hpp"
#include "liteim/storage/MySqlPool.hpp"
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

std::string uniqueStep27Suffix() {
    static std::atomic<int> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(ticks) + "_" + std::to_string(counter.fetch_add(1));
}

std::string uniqueUsername() {
    return "step27_user_" + uniqueStep27Suffix();
}

std::string uniqueGroupName() {
    return "step27_group_" + uniqueStep27Suffix();
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

void cleanupStep27Rows(const liteim::MySqlConfig& config) {
    liteim::MySqlConnection connection;
    const auto connect_status = connection.connect(config);
    if (!connect_status.isOk()) {
        return;
    }

    executeCleanupSql(
        connection,
        "DELETE FROM group_members "
        "WHERE group_id IN (SELECT group_id FROM chat_groups WHERE group_name LIKE 'step27\\_%') "
        "OR user_id IN (SELECT user_id FROM users WHERE username LIKE 'step27\\_%')");
    executeCleanupSql(
        connection, "DELETE FROM chat_groups "
                    "WHERE group_name LIKE 'step27\\_%' "
                    "OR owner_id IN (SELECT user_id FROM users WHERE username LIKE 'step27\\_%')");
    executeCleanupSql(
        connection, "DELETE FROM friendships "
                    "WHERE user_id IN (SELECT user_id FROM users WHERE username LIKE 'step27\\_%') "
                    "OR friend_id IN (SELECT user_id FROM users WHERE username LIKE 'step27\\_%')");
    executeCleanupSql(
        connection,
        "DELETE FROM friend_requests "
        "WHERE requester_id IN (SELECT user_id FROM users WHERE username LIKE 'step27\\_%') "
        "OR target_user_id IN (SELECT user_id FROM users WHERE username LIKE 'step27\\_%')");
    executeCleanupSql(connection, "DELETE FROM users WHERE username LIKE 'step27\\_%'");
}

bool containsUserId(const std::vector<liteim::GroupMemberRecord>& members, std::uint64_t user_id) {
    return std::any_of(members.begin(), members.end(),
                       [user_id](const auto& member) { return member.user_id == user_id; });
}

class FriendGroupDaoIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        config = testMySqlConfig();

        liteim::MySqlConnection probe;
        const auto status = probe.connect(config);
        if (!status.isOk()) {
            GTEST_SKIP() << "local LiteIM MySQL is not available: " << status.message();
        }

        cleanupStep27Rows(config);
        pool = std::make_unique<liteim::MySqlPool>(config);
        ASSERT_TRUE(pool->start().isOk());
        user_dao = std::make_unique<liteim::UserDao>(*pool);
        friend_dao = std::make_unique<liteim::FriendDao>(*pool);
        group_dao = std::make_unique<liteim::GroupDao>(*pool);
    }

    void TearDown() override {
        group_dao.reset();
        friend_dao.reset();
        user_dao.reset();
        if (pool) {
            pool->close();
            pool.reset();
        }
        cleanupStep27Rows(config);
    }

    liteim::UserRecord createUser() {
        liteim::UserRecord user;
        const auto request = makeUserRequest();
        const auto status = user_dao->createUser(request, user);
        EXPECT_TRUE(status.isOk()) << status.message();
        return user;
    }

    liteim::GroupRecord createGroup(std::uint64_t owner_id) {
        liteim::GroupRecord group;
        const liteim::CreateGroupRequest request{owner_id, uniqueGroupName()};
        const auto status = group_dao->createGroup(request, group);
        EXPECT_TRUE(status.isOk()) << status.message();
        return group;
    }

    liteim::MySqlConfig config;
    std::unique_ptr<liteim::MySqlPool> pool;
    std::unique_ptr<liteim::UserDao> user_dao;
    std::unique_ptr<liteim::FriendDao> friend_dao;
    std::unique_ptr<liteim::GroupDao> group_dao;
};

}  // namespace

TEST(FriendGroupDaoTest, HeadersAreSelfContained) {
    liteim::MySqlPool pool(testMySqlConfig());
    liteim::FriendDao friend_dao(pool);
    liteim::GroupDao group_dao(pool);
}

TEST_F(FriendGroupDaoIntegrationTest, AddFriendshipCreatesBidirectionalRelationship) {
    const auto alice = createUser();
    const auto bob = createUser();

    const auto add_status = friend_dao->addFriendship(alice.user_id, bob.user_id);
    ASSERT_TRUE(add_status.isOk()) << add_status.message();

    std::vector<liteim::UserProfileRecord> alice_friends;
    const auto alice_status = friend_dao->getFriends(alice.user_id, alice_friends);
    ASSERT_TRUE(alice_status.isOk()) << alice_status.message();
    ASSERT_EQ(alice_friends.size(), 1U);
    EXPECT_EQ(alice_friends.front().user_id, bob.user_id);
    EXPECT_EQ(alice_friends.front().username, bob.username);
    EXPECT_EQ(alice_friends.front().nickname, bob.nickname);

    std::vector<liteim::UserProfileRecord> bob_friends;
    const auto bob_status = friend_dao->getFriends(bob.user_id, bob_friends);
    ASSERT_TRUE(bob_status.isOk()) << bob_status.message();
    ASSERT_EQ(bob_friends.size(), 1U);
    EXPECT_EQ(bob_friends.front().user_id, alice.user_id);
    EXPECT_EQ(bob_friends.front().username, alice.username);
    EXPECT_EQ(bob_friends.front().nickname, alice.nickname);
}

TEST_F(FriendGroupDaoIntegrationTest, RepeatedAddFriendshipDoesNotCreateDuplicates) {
    const auto alice = createUser();
    const auto bob = createUser();

    ASSERT_TRUE(friend_dao->addFriendship(alice.user_id, bob.user_id).isOk());
    ASSERT_TRUE(friend_dao->addFriendship(alice.user_id, bob.user_id).isOk());
    ASSERT_TRUE(friend_dao->addFriendship(bob.user_id, alice.user_id).isOk());

    std::vector<liteim::UserProfileRecord> alice_friends;
    ASSERT_TRUE(friend_dao->getFriends(alice.user_id, alice_friends).isOk());
    ASSERT_EQ(alice_friends.size(), 1U);
    EXPECT_EQ(alice_friends.front().user_id, bob.user_id);

    std::vector<liteim::UserProfileRecord> bob_friends;
    ASSERT_TRUE(friend_dao->getFriends(bob.user_id, bob_friends).isOk());
    ASSERT_EQ(bob_friends.size(), 1U);
    EXPECT_EQ(bob_friends.front().user_id, alice.user_id);
}

TEST_F(FriendGroupDaoIntegrationTest, FriendRequestRequiresAcceptanceBeforeFriendship) {
    const auto alice = createUser();
    const auto bob = createUser();

    liteim::FriendRequestRecord request;
    const auto create_status =
        friend_dao->createFriendRequest(alice.user_id, bob.user_id, request);
    ASSERT_TRUE(create_status.isOk()) << create_status.message();
    EXPECT_EQ(request.requester_id, alice.user_id);
    EXPECT_EQ(request.target_user_id, bob.user_id);
    EXPECT_EQ(request.status, liteim::FriendRequestStatus::kPending);

    bool are_friends = true;
    ASSERT_TRUE(friend_dao->areFriends(alice.user_id, bob.user_id, are_friends).isOk());
    EXPECT_FALSE(are_friends);

    const auto accept_status = friend_dao->acceptFriendRequest(alice.user_id, bob.user_id);
    ASSERT_TRUE(accept_status.isOk()) << accept_status.message();
    ASSERT_TRUE(friend_dao->areFriends(alice.user_id, bob.user_id, are_friends).isOk());
    EXPECT_TRUE(are_friends);

    std::vector<liteim::UserProfileRecord> alice_friends;
    ASSERT_TRUE(friend_dao->getFriends(alice.user_id, alice_friends).isOk());
    ASSERT_EQ(alice_friends.size(), 1U);
    EXPECT_EQ(alice_friends.front().user_id, bob.user_id);
}

TEST_F(FriendGroupDaoIntegrationTest, RejectFriendRequestDoesNotCreateFriendship) {
    const auto alice = createUser();
    const auto bob = createUser();

    liteim::FriendRequestRecord request;
    ASSERT_TRUE(friend_dao->createFriendRequest(alice.user_id, bob.user_id, request).isOk());
    const auto reject_status = friend_dao->rejectFriendRequest(alice.user_id, bob.user_id);
    ASSERT_TRUE(reject_status.isOk()) << reject_status.message();

    bool are_friends = true;
    ASSERT_TRUE(friend_dao->areFriends(alice.user_id, bob.user_id, are_friends).isOk());
    EXPECT_FALSE(are_friends);
}

TEST_F(FriendGroupDaoIntegrationTest, RepeatedFriendRequestAndAcceptReturnClearErrors) {
    const auto alice = createUser();
    const auto bob = createUser();

    liteim::FriendRequestRecord request;
    ASSERT_TRUE(friend_dao->createFriendRequest(alice.user_id, bob.user_id, request).isOk());
    const auto duplicate_create =
        friend_dao->createFriendRequest(alice.user_id, bob.user_id, request);
    EXPECT_FALSE(duplicate_create.isOk());
    EXPECT_EQ(duplicate_create.code(), liteim::ErrorCode::AlreadyExists);

    ASSERT_TRUE(friend_dao->acceptFriendRequest(alice.user_id, bob.user_id).isOk());
    const auto duplicate_accept = friend_dao->acceptFriendRequest(alice.user_id, bob.user_id);
    EXPECT_FALSE(duplicate_accept.isOk());
    EXPECT_EQ(duplicate_accept.code(), liteim::ErrorCode::AlreadyExists);
}

TEST_F(FriendGroupDaoIntegrationTest, CreateGroupPersistsGroupAndOwnerMembership) {
    const auto owner = createUser();
    const auto group_name = uniqueGroupName();

    liteim::GroupRecord created;
    const auto create_status =
        group_dao->createGroup(liteim::CreateGroupRequest{owner.user_id, group_name}, created);
    ASSERT_TRUE(create_status.isOk()) << create_status.message();
    EXPECT_GE(created.group_id, 10000U);
    EXPECT_EQ(created.owner_id, owner.user_id);
    EXPECT_EQ(created.group_name, group_name);
    EXPECT_GT(created.created_at_ms, 0);

    liteim::GroupRecord found;
    const auto find_status = group_dao->findGroupById(created.group_id, found);
    ASSERT_TRUE(find_status.isOk()) << find_status.message();
    EXPECT_EQ(found.group_id, created.group_id);
    EXPECT_EQ(found.owner_id, owner.user_id);
    EXPECT_EQ(found.group_name, group_name);
    EXPECT_EQ(found.created_at_ms, created.created_at_ms);

    std::vector<liteim::GroupMemberRecord> members;
    const auto members_status = group_dao->getGroupMembers(created.group_id, members);
    ASSERT_TRUE(members_status.isOk()) << members_status.message();
    ASSERT_EQ(members.size(), 1U);
    EXPECT_EQ(members.front().user_id, owner.user_id);
    EXPECT_EQ(members.front().username, owner.username);
    EXPECT_EQ(members.front().nickname, owner.nickname);
    EXPECT_GT(members.front().joined_at_ms, 0);
}

TEST_F(FriendGroupDaoIntegrationTest, AddGroupMemberIsIdempotent) {
    const auto owner = createUser();
    const auto member = createUser();
    const auto group = createGroup(owner.user_id);

    ASSERT_TRUE(group_dao->addGroupMember(group.group_id, member.user_id).isOk());
    ASSERT_TRUE(group_dao->addGroupMember(group.group_id, member.user_id).isOk());

    std::vector<liteim::GroupMemberRecord> members;
    const auto members_status = group_dao->getGroupMembers(group.group_id, members);
    ASSERT_TRUE(members_status.isOk()) << members_status.message();
    ASSERT_EQ(members.size(), 2U);
    EXPECT_TRUE(containsUserId(members, owner.user_id));
    EXPECT_TRUE(containsUserId(members, member.user_id));
}

TEST_F(FriendGroupDaoIntegrationTest, GetGroupsForUserReturnsOwnedAndJoinedGroups) {
    const auto alice = createUser();
    const auto bob = createUser();
    const auto alice_group = createGroup(alice.user_id);
    const auto bob_group = createGroup(bob.user_id);
    ASSERT_TRUE(group_dao->addGroupMember(alice_group.group_id, bob.user_id).isOk());

    std::vector<liteim::GroupRecord> bob_groups;
    const auto status = group_dao->getGroupsForUser(bob.user_id, bob_groups);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_EQ(bob_groups.size(), 2U);
    EXPECT_EQ(bob_groups[0].group_id, alice_group.group_id);
    EXPECT_EQ(bob_groups[0].owner_id, alice.user_id);
    EXPECT_EQ(bob_groups[0].group_name, alice_group.group_name);
    EXPECT_EQ(bob_groups[1].group_id, bob_group.group_id);
    EXPECT_EQ(bob_groups[1].owner_id, bob.user_id);
    EXPECT_EQ(bob_groups[1].group_name, bob_group.group_name);
}

TEST_F(FriendGroupDaoIntegrationTest, RemoveGroupMemberRemovesNormalMember) {
    const auto owner = createUser();
    const auto member = createUser();
    const auto group = createGroup(owner.user_id);
    ASSERT_TRUE(group_dao->addGroupMember(group.group_id, member.user_id).isOk());

    const auto remove_status = group_dao->removeGroupMember(group.group_id, member.user_id);
    ASSERT_TRUE(remove_status.isOk()) << remove_status.message();

    std::vector<liteim::GroupMemberRecord> members;
    ASSERT_TRUE(group_dao->getGroupMembers(group.group_id, members).isOk());
    ASSERT_EQ(members.size(), 1U);
    EXPECT_TRUE(containsUserId(members, owner.user_id));
    EXPECT_FALSE(containsUserId(members, member.user_id));
}

TEST_F(FriendGroupDaoIntegrationTest, FindMissingGroupReturnsNotFound) {
    liteim::GroupRecord found;
    const auto status = group_dao->findGroupById(999999999ULL, found);

    EXPECT_FALSE(status.isOk());
    EXPECT_EQ(status.code(), liteim::ErrorCode::NotFound);
    EXPECT_EQ(found.group_id, 0U);
}
