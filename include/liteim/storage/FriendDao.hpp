#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace liteim {

class MySqlPool;

// 操作mysql里的friendships表，存储用户好友关系
class FriendDao {
public:
    explicit FriendDao(MySqlPool& pool,
                       std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));
    Status addFriendship(std::uint64_t user_id, std::uint64_t friend_id);
    Status getFriends(std::uint64_t user_id, std::vector<UserProfileRecord>& friends);
    //将user_id的好友列表查询结果放到friends里

private:
    MySqlPool* pool_;
    std::chrono::milliseconds acquire_timeout_;
};

}  // namespace liteim
