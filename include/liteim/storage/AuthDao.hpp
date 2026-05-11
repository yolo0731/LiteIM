#pragma once

#include "liteim/base/Status.hpp"

#include <chrono>
#include <string>

namespace liteim {

class MySqlPool;

class AuthDao {
public:
    explicit AuthDao(MySqlPool& pool,
                     std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status usernameExists(const std::string& username, bool& exists);

private:
    MySqlPool* pool_;
    std::chrono::milliseconds acquire_timeout_;
};

} // namespace liteim
