#pragma once

#include <cstdint>
#include <string>

namespace liteim {

enum class ConversationType : std::uint8_t {
    kPrivate = 1,
    kGroup = 2,
};

enum class DeliveryStatus : std::uint8_t {
    kPending = 0,
    kPushed = 1,
    kDelivered = 2,
    kReadReserved = 3,
};

enum class FriendRequestStatus : std::uint8_t {
    kPending = 0,
    kAccepted = 1,
    kRejected = 2,
};

struct ConversationKey {
    ConversationType type{ConversationType::kPrivate};
    std::uint64_t id{0};
};

struct CreateUserRequest {
    std::string username;
    std::string password_hash;
    std::string password_salt;
    std::string nickname;
};

struct UserRecord {
    std::uint64_t user_id{0};
    std::string username;
    std::string password_hash;
    std::string password_salt;
    std::string nickname;
    std::int64_t created_at_ms{0};
};

struct UserProfileRecord {
    std::uint64_t user_id{0};
    std::string username;
    std::string nickname;
    std::int64_t created_at_ms{0};
};

struct FriendRequestRecord {
    std::uint64_t requester_id{0};
    std::uint64_t target_user_id{0};
    FriendRequestStatus status{FriendRequestStatus::kPending};
    std::int64_t created_at_ms{0};
    std::int64_t updated_at_ms{0};
};

struct CreateGroupRequest {
    std::uint64_t owner_id{0};
    std::string group_name;
};

struct GroupRecord {
    std::uint64_t group_id{0};
    std::uint64_t owner_id{0};
    std::string group_name;
    std::int64_t created_at_ms{0};
};

struct GroupMemberRecord {
    std::uint64_t user_id{0};
    std::string username;
    std::string nickname;
    std::int64_t joined_at_ms{0};
};

struct MessageRecord {
    std::uint64_t message_id{0};
    ConversationKey conversation;
    std::uint64_t sender_id{0};
    std::uint64_t receiver_id{0};
    std::string text;
    std::int64_t created_at_ms{0};
    std::string client_msg_id;
};

struct OfflineMessageRecord {
    std::uint64_t offline_message_id{0};
    std::uint64_t user_id{0};
    MessageRecord message;
    std::int64_t created_at_ms{0};
};

struct HistoryQuery {
    ConversationKey conversation;
    std::uint64_t before_message_id{0};
    std::uint32_t limit{50};
};

}  // namespace liteim
