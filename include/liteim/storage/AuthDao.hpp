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

    // 检查用户名是否已存在，结果通过bool值exists返回，函数返回Status表示操作是否成功
    Status usernameExists(const std::string& username, bool& exists);

private:
    MySqlPool* pool_;
    std::chrono::milliseconds acquire_timeout_;
};

}  // namespace liteim
