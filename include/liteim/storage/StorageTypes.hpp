#pragma once

#include <cstdint>
#include <string>

namespace liteim::storage {

using UserId = std::uint64_t;
using GroupId = std::uint64_t;
using MessageId = std::uint64_t;
using UnixTimestamp = std::int64_t;

enum class UserType : std::uint8_t {
    Human = 0,
    Bot = 1,
};

struct User {
    UserId id = 0;
    std::string username;
    std::string nickname;
    std::string password_salt;
    std::string password_hash;
    UserType type = UserType::Human;
    UnixTimestamp created_at = 0;
};

struct CreateUserRequest {
    std::string username;
    std::string nickname;
    std::string password_salt;
    std::string password_hash;
    UserType type = UserType::Human;
};

struct Group {
    GroupId id = 0;
    std::string name;
    UserId owner_id = 0;
    UnixTimestamp created_at = 0;
};

struct CreateGroupRequest {
    std::string name;
    UserId owner_id = 0;
};

struct PrivateMessage {
    MessageId id = 0;
    UserId sender_id = 0;
    UserId receiver_id = 0;
    std::string body;
    UnixTimestamp created_at = 0;
    bool delivered = false;
};

struct SavePrivateMessageRequest {
    UserId sender_id = 0;
    UserId receiver_id = 0;
    std::string body;
    UnixTimestamp created_at = 0;
    bool delivered = false;
};

struct GroupMessage {
    MessageId id = 0;
    GroupId group_id = 0;
    UserId sender_id = 0;
    std::string body;
    UnixTimestamp created_at = 0;
};

struct SaveGroupMessageRequest {
    GroupId group_id = 0;
    UserId sender_id = 0;
    std::string body;
    UnixTimestamp created_at = 0;
};

}  // namespace liteim::storage
