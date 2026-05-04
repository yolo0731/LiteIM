#include "TestUtil.hpp"

#include "liteim/storage/IStorage.hpp"
#include "liteim/storage/NullCache.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using liteim::storage::CreateGroupRequest;
using liteim::storage::CreateUserRequest;
using liteim::storage::Group;
using liteim::storage::GroupId;
using liteim::storage::GroupMessage;
using liteim::storage::ICache;
using liteim::storage::IStorage;
using liteim::storage::NullCache;
using liteim::storage::PrivateMessage;
using liteim::storage::SaveGroupMessageRequest;
using liteim::storage::SavePrivateMessageRequest;
using liteim::storage::User;
using liteim::storage::UserId;
using liteim::storage::UserType;
using liteim::tests::TestCase;
using liteim::tests::expect;

template <typename T>
std::vector<T> slice(std::vector<T> rows, std::size_t limit, std::size_t offset) {
    if (offset >= rows.size()) {
        return {};
    }

    const auto begin = rows.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = limit == 0 || offset + limit > rows.size()
                         ? rows.end()
                         : begin + static_cast<std::ptrdiff_t>(limit);
    return {begin, end};
}

class FakeStorage final : public IStorage {
public:
    std::optional<User> createUser(const CreateUserRequest& request) override {
        if (request.username.empty() || username_to_id_.count(request.username) > 0) {
            return std::nullopt;
        }

        User user;
        user.id = next_user_id_++;
        user.username = request.username;
        user.nickname = request.nickname;
        user.password_salt = request.password_salt;
        user.password_hash = request.password_hash;
        user.type = request.type;
        user.created_at = 1000 + static_cast<liteim::storage::UnixTimestamp>(user.id);

        username_to_id_[user.username] = user.id;
        users_[user.id] = user;
        return user;
    }

    std::optional<User> findUserByUsername(const std::string& username) const override {
        const auto name_it = username_to_id_.find(username);
        if (name_it == username_to_id_.end()) {
            return std::nullopt;
        }
        return findUserById(name_it->second);
    }

    std::optional<User> findUserById(UserId user_id) const override {
        const auto it = users_.find(user_id);
        if (it == users_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool addFriendship(UserId user_id, UserId friend_id) override {
        if (!findUserById(user_id) || !findUserById(friend_id)) {
            return false;
        }
        friendships_[user_id].push_back(friend_id);
        return true;
    }

    std::vector<User> getFriends(UserId user_id) const override {
        std::vector<User> friends;
        const auto it = friendships_.find(user_id);
        if (it == friendships_.end()) {
            return friends;
        }

        for (const auto friend_id : it->second) {
            if (auto user = findUserById(friend_id)) {
                friends.push_back(*user);
            }
        }
        return friends;
    }

    std::optional<Group> createGroup(const CreateGroupRequest& request) override {
        if (request.name.empty() || !findUserById(request.owner_id)) {
            return std::nullopt;
        }

        Group group;
        group.id = next_group_id_++;
        group.name = request.name;
        group.owner_id = request.owner_id;
        group.created_at = 2000 + static_cast<liteim::storage::UnixTimestamp>(group.id);

        groups_[group.id] = group;
        group_members_[group.id].push_back(request.owner_id);
        return group;
    }

    bool addGroupMember(GroupId group_id, UserId user_id) override {
        if (!findGroupById(group_id) || !findUserById(user_id)) {
            return false;
        }
        group_members_[group_id].push_back(user_id);
        return true;
    }

    bool removeGroupMember(GroupId group_id, UserId user_id) override {
        const auto it = group_members_.find(group_id);
        if (it == group_members_.end()) {
            return false;
        }

        auto& members = it->second;
        const auto old_size = members.size();
        members.erase(std::remove(members.begin(), members.end(), user_id), members.end());
        return members.size() != old_size;
    }

    std::vector<UserId> getGroupMembers(GroupId group_id) const override {
        const auto it = group_members_.find(group_id);
        if (it == group_members_.end()) {
            return {};
        }
        return it->second;
    }

    std::optional<Group> findGroupById(GroupId group_id) const override {
        const auto it = groups_.find(group_id);
        if (it == groups_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<PrivateMessage> savePrivateMessage(
        const SavePrivateMessageRequest& request) override {
        if (!findUserById(request.sender_id) || !findUserById(request.receiver_id)) {
            return std::nullopt;
        }

        PrivateMessage message;
        message.id = next_message_id_++;
        message.sender_id = request.sender_id;
        message.receiver_id = request.receiver_id;
        message.body = request.body;
        message.created_at = request.created_at;
        message.delivered = request.delivered;
        private_messages_.push_back(message);
        return message;
    }

    std::optional<GroupMessage> saveGroupMessage(
        const SaveGroupMessageRequest& request) override {
        if (!findGroupById(request.group_id) || !findUserById(request.sender_id)) {
            return std::nullopt;
        }

        GroupMessage message;
        message.id = next_message_id_++;
        message.group_id = request.group_id;
        message.sender_id = request.sender_id;
        message.body = request.body;
        message.created_at = request.created_at;
        group_messages_.push_back(message);
        return message;
    }

    std::vector<PrivateMessage> getPrivateHistory(
        UserId first_user_id,
        UserId second_user_id,
        std::size_t limit,
        std::size_t offset) const override {
        std::vector<PrivateMessage> rows;
        for (const auto& message : private_messages_) {
            const bool forward =
                message.sender_id == first_user_id && message.receiver_id == second_user_id;
            const bool backward =
                message.sender_id == second_user_id && message.receiver_id == first_user_id;
            if (forward || backward) {
                rows.push_back(message);
            }
        }
        return slice(std::move(rows), limit, offset);
    }

    std::vector<GroupMessage> getGroupHistory(
        GroupId group_id,
        std::size_t limit,
        std::size_t offset) const override {
        std::vector<GroupMessage> rows;
        for (const auto& message : group_messages_) {
            if (message.group_id == group_id) {
                rows.push_back(message);
            }
        }
        return slice(std::move(rows), limit, offset);
    }

    std::vector<PrivateMessage> getOfflineMessages(
        UserId user_id,
        std::size_t limit) const override {
        std::vector<PrivateMessage> rows;
        for (const auto& message : private_messages_) {
            if (message.receiver_id == user_id && !message.delivered) {
                rows.push_back(message);
            }
        }
        return slice(std::move(rows), limit, 0);
    }

private:
    UserId next_user_id_ = 1;
    GroupId next_group_id_ = 1;
    liteim::storage::MessageId next_message_id_ = 1;
    std::unordered_map<UserId, User> users_;
    std::unordered_map<std::string, UserId> username_to_id_;
    std::unordered_map<UserId, std::vector<UserId>> friendships_;
    std::unordered_map<GroupId, Group> groups_;
    std::unordered_map<GroupId, std::vector<UserId>> group_members_;
    std::vector<PrivateMessage> private_messages_;
    std::vector<GroupMessage> group_messages_;
};

CreateUserRequest makeUserRequest(std::string username, std::string nickname) {
    CreateUserRequest request;
    request.username = std::move(username);
    request.nickname = std::move(nickname);
    request.password_salt = "salt";
    request.password_hash = "hash";
    request.type = UserType::Human;
    return request;
}

void testStorageInterfacesAreAbstract() {
    static_assert(std::is_abstract_v<IStorage>, "IStorage should be abstract");
    static_assert(std::has_virtual_destructor_v<IStorage>, "IStorage should be polymorphic");
    static_assert(std::is_abstract_v<ICache>, "ICache should be abstract");
    static_assert(std::has_virtual_destructor_v<ICache>, "ICache should be polymorphic");
    static_assert(std::is_base_of_v<ICache, NullCache>, "NullCache should implement ICache");

    expect(true, "storage interfaces should compile as abstract contracts");
}

void testStorageTestDoubleImplementsFullContract() {
    FakeStorage storage;

    const auto alice = storage.createUser(makeUserRequest("alice", "Alice"));
    const auto bob = storage.createUser(makeUserRequest("bob", "Bob"));
    expect(alice.has_value(), "storage test double should create first user");
    expect(bob.has_value(), "storage test double should create second user");
    expect(!storage.createUser(makeUserRequest("alice", "Alice2")), "duplicate username should fail");
    expect(
        storage.findUserByUsername("alice")->id == alice->id,
        "findUserByUsername should return created user");
    expect(storage.findUserById(bob->id)->username == "bob", "findUserById should return user");

    expect(storage.addFriendship(alice->id, bob->id), "addFriendship should accept known users");
    const auto friends = storage.getFriends(alice->id);
    expect(friends.size() == 1 && friends.front().id == bob->id, "getFriends should return friend");

    const auto group = storage.createGroup({"team", alice->id});
    expect(group.has_value(), "createGroup should accept known owner");
    expect(storage.addGroupMember(group->id, bob->id), "addGroupMember should accept known user");
    auto members = storage.getGroupMembers(group->id);
    expect(members.size() == 2, "group should contain owner and added member");
    expect(storage.removeGroupMember(group->id, bob->id), "removeGroupMember should remove member");
    members = storage.getGroupMembers(group->id);
    expect(members.size() == 1 && members.front() == alice->id, "group should keep owner member");
    expect(storage.findGroupById(group->id)->name == "team", "findGroupById should return group");

    const auto private_message = storage.savePrivateMessage(
        {alice->id, bob->id, "hello", 3000, false});
    expect(private_message.has_value(), "savePrivateMessage should accept known users");
    const auto private_history = storage.getPrivateHistory(alice->id, bob->id, 10, 0);
    expect(private_history.size() == 1, "getPrivateHistory should return saved message");
    const auto offline_messages = storage.getOfflineMessages(bob->id, 10);
    expect(offline_messages.size() == 1, "getOfflineMessages should return undelivered message");

    const auto group_message = storage.saveGroupMessage({group->id, alice->id, "group hello", 4000});
    expect(group_message.has_value(), "saveGroupMessage should accept known group and sender");
    const auto group_history = storage.getGroupHistory(group->id, 10, 0);
    expect(group_history.size() == 1, "getGroupHistory should return saved group message");
}

void testNullCacheIsNoop() {
    NullCache cache;

    cache.setOnline(1, 42);
    expect(!cache.findOnlineSession(1), "NullCache should not store online sessions");
    cache.setOffline(1);
    expect(!cache.findOnlineSession(1), "NullCache offline should remain empty");
    cache.clear();
    expect(!cache.findOnlineSession(1), "NullCache clear should remain empty");
}

}  // namespace

std::vector<TestCase> storageInterfaceTests() {
    return {
        {"storage interfaces are abstract contracts", testStorageInterfacesAreAbstract},
        {"storage test double implements full contract", testStorageTestDoubleImplementsFullContract},
        {"NullCache is a no-op cache", testNullCacheIsNoop},
    };
}
