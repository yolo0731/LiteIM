#include "TestUtil.hpp"

#include "liteim/storage/SQLiteStorage.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <unistd.h>
#include <vector>

namespace {

using liteim::storage::CreateGroupRequest;
using liteim::storage::CreateUserRequest;
using liteim::storage::SQLiteStorage;
using liteim::storage::UserType;
using liteim::tests::TestCase;
using liteim::tests::expect;

#ifndef LITEIM_SOURCE_DIR
#define LITEIM_SOURCE_DIR "."
#endif

std::string schemaPath() {
    return std::string(LITEIM_SOURCE_DIR) + "/sql/init.sql";
}

CreateUserRequest makeUserRequest(std::string username, std::string nickname) {
    CreateUserRequest request;
    request.username = std::move(username);
    request.nickname = std::move(nickname);
    request.password_salt = "salt-" + request.username;
    request.password_hash = "hash-" + request.username;
    request.type = UserType::Human;
    return request;
}

class TempDbFile {
public:
    TempDbFile() {
        static int counter = 0;
        path_ = "/tmp/liteim_sqlite_storage_" + std::to_string(::getpid()) + "_" +
                std::to_string(counter++) + ".db";
        std::remove(path_.c_str());
    }

    ~TempDbFile() {
        std::remove(path_.c_str());
    }

    const std::string& path() const {
        return path_;
    }

private:
    std::string path_;
};

void testSQLiteStorageCreatesAndFindsUsers() {
    SQLiteStorage storage(":memory:", schemaPath());

    auto alice = storage.createUser(makeUserRequest("alice", "Alice"));
    expect(alice.has_value(), "SQLiteStorage should create a user");
    expect(alice->id > 0, "created user should have database id");
    expect(alice->username == "alice", "created user username should match");
    expect(alice->nickname == "Alice", "created user nickname should match");
    expect(alice->password_salt == "salt-alice", "created user salt should match");
    expect(alice->password_hash == "hash-alice", "created user hash should match");
    expect(alice->created_at > 0, "created user should have timestamp");

    expect(
        !storage.createUser(makeUserRequest("alice", "Alice2")),
        "duplicate username should return nullopt");
    expect(!storage.createUser(makeUserRequest("", "Nobody")), "empty username should fail");

    const auto by_name = storage.findUserByUsername("alice");
    expect(by_name.has_value() && by_name->id == alice->id, "find by username should work");

    const auto by_id = storage.findUserById(alice->id);
    expect(by_id.has_value() && by_id->username == "alice", "find by id should work");
    expect(!storage.findUserByUsername("missing"), "missing username should return nullopt");
    expect(!storage.findUserById(9999), "missing user id should return nullopt");
}

void testSQLiteStorageFriendsAndGroups() {
    SQLiteStorage storage(":memory:", schemaPath());

    const auto alice = storage.createUser(makeUserRequest("alice", "Alice"));
    const auto bob = storage.createUser(makeUserRequest("bob", "Bob"));
    expect(alice && bob, "users should be created before friendship/group tests");

    expect(storage.addFriendship(alice->id, bob->id), "addFriendship should accept known users");
    expect(!storage.addFriendship(alice->id, alice->id), "addFriendship should reject self");

    const auto alice_friends = storage.getFriends(alice->id);
    const auto bob_friends = storage.getFriends(bob->id);
    expect(alice_friends.size() == 1, "friendship should add alice -> bob");
    expect(bob_friends.size() == 1, "friendship should add bob -> alice");
    expect(alice_friends.front().id == bob->id, "alice friend should be bob");
    expect(bob_friends.front().id == alice->id, "bob friend should be alice");

    const auto group = storage.createGroup(CreateGroupRequest{"team", alice->id});
    expect(group.has_value(), "createGroup should create group for known owner");
    expect(group->id > 0, "created group should have database id");
    expect(group->created_at > 0, "created group should have timestamp");
    expect(storage.findGroupById(group->id)->name == "team", "findGroupById should work");

    auto members = storage.getGroupMembers(group->id);
    expect(members.size() == 1 && members.front() == alice->id, "group should include owner");

    expect(storage.addGroupMember(group->id, bob->id), "addGroupMember should add known user");
    members = storage.getGroupMembers(group->id);
    expect(members.size() == 2, "group should contain owner and bob");

    expect(storage.removeGroupMember(group->id, bob->id), "removeGroupMember should remove bob");
    members = storage.getGroupMembers(group->id);
    expect(members.size() == 1 && members.front() == alice->id, "group should keep owner");
    expect(!storage.removeGroupMember(group->id, bob->id), "removing absent member should fail");
    expect(!storage.createGroup(CreateGroupRequest{"bad", 9999}), "unknown owner should fail");
}

void testSQLiteStorageMessagesHistoryAndOfflineQueries() {
    SQLiteStorage storage(":memory:", schemaPath());

    const auto alice = storage.createUser(makeUserRequest("alice", "Alice"));
    const auto bob = storage.createUser(makeUserRequest("bob", "Bob"));
    expect(alice && bob, "users should be created before message tests");

    const auto first = storage.savePrivateMessage({alice->id, bob->id, "one", 10, false});
    const auto second = storage.savePrivateMessage({bob->id, alice->id, "two", 20, true});
    expect(first && second, "private messages should be saved");
    expect(!storage.savePrivateMessage({alice->id, 9999, "bad", 30, false}), "bad receiver fails");

    const auto all_history = storage.getPrivateHistory(alice->id, bob->id, 0, 0);
    expect(all_history.size() == 2, "limit 0 should return all private history");
    expect(all_history.front().body == "one", "private history should be ordered by time");

    const auto paged_history = storage.getPrivateHistory(alice->id, bob->id, 1, 1);
    expect(paged_history.size() == 1, "private history should support limit/offset");
    expect(paged_history.front().body == "two", "private history offset should skip first row");

    const auto offline_for_bob = storage.getOfflineMessages(bob->id, 10);
    expect(offline_for_bob.size() == 1, "offline query should return undelivered message");
    expect(offline_for_bob.front().id == first->id, "offline message should match undelivered row");

    const auto group = storage.createGroup(CreateGroupRequest{"team", alice->id});
    expect(group.has_value(), "group should be created before group message tests");
    expect(storage.addGroupMember(group->id, bob->id), "bob should join group");

    const auto group_first = storage.saveGroupMessage({group->id, alice->id, "group-one", 40});
    const auto group_second = storage.saveGroupMessage({group->id, bob->id, "group-two", 50});
    expect(group_first && group_second, "group messages should be saved");
    expect(!storage.saveGroupMessage({9999, alice->id, "bad", 60}), "bad group should fail");

    const auto group_history = storage.getGroupHistory(group->id, 0, 0);
    expect(group_history.size() == 2, "limit 0 should return all group history");
    expect(group_history.front().body == "group-one", "group history should be ordered by time");

    const auto group_paged = storage.getGroupHistory(group->id, 1, 1);
    expect(group_paged.size() == 1, "group history should support limit/offset");
    expect(group_paged.front().body == "group-two", "group offset should skip first row");
}

void testSQLiteStoragePersistsAcrossConnections() {
    TempDbFile db_file;

    {
        SQLiteStorage storage(db_file.path(), schemaPath());
        const auto alice = storage.createUser(makeUserRequest("alice", "Alice"));
        expect(alice.has_value(), "file-backed storage should create a user");
    }

    {
        SQLiteStorage storage(db_file.path(), schemaPath());
        const auto alice = storage.findUserByUsername("alice");
        expect(alice.has_value(), "file-backed storage should persist user across connections");
        expect(alice->nickname == "Alice", "persisted user should keep nickname");
    }
}

}  // namespace

std::vector<TestCase> sqliteStorageTests() {
    return {
        {"SQLiteStorage creates and finds users", testSQLiteStorageCreatesAndFindsUsers},
        {"SQLiteStorage handles friends and groups", testSQLiteStorageFriendsAndGroups},
        {"SQLiteStorage handles messages and offline queries",
         testSQLiteStorageMessagesHistoryAndOfflineQueries},
        {"SQLiteStorage persists across file connections", testSQLiteStoragePersistsAcrossConnections},
    };
}
