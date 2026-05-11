#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace liteim {

class MySqlPool;

class FriendDao {
public:
    explicit FriendDao(MySqlPool& pool,
                       std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status addFriendship(std::uint64_t user_id, std::uint64_t friend_id);
    Status getFriends(std::uint64_t user_id, std::vector<UserProfileRecord>& friends);

private:
    MySqlPool* pool_;
    std::chrono::milliseconds acquire_timeout_;
};

} // namespace liteim
