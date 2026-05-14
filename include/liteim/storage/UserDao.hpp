#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace liteim {

class MySqlPool;

class UserDao {
public:
    explicit UserDao(MySqlPool& pool,
                     std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status createUser(const CreateUserRequest& request, UserRecord& created_user);
    Status findUserByUsername(const std::string& username, UserRecord& user);
    Status findUserById(std::uint64_t user_id, UserRecord& user);

private:
    MySqlPool* pool_;
    std::chrono::milliseconds acquire_timeout_;
};

}  // namespace liteim
