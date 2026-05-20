#include "liteim/protocol/MessageType.hpp"

namespace liteim {

const char* toString(MessageType type) noexcept {
    switch (type) {
    case MessageType::HeartbeatRequest:
        return "HEARTBEAT_REQUEST";
    case MessageType::HeartbeatResponse:
        return "HEARTBEAT_RESPONSE";
    case MessageType::RegisterRequest:
        return "REGISTER_REQUEST";
    case MessageType::RegisterResponse:
        return "REGISTER_RESPONSE";
    case MessageType::LoginRequest:
        return "LOGIN_REQUEST";
    case MessageType::LoginResponse:
        return "LOGIN_RESPONSE";
    case MessageType::LogoutRequest:
        return "LOGOUT_REQUEST";
    case MessageType::LogoutResponse:
        return "LOGOUT_RESPONSE";
    case MessageType::AddFriendRequest:
        return "ADD_FRIEND_REQUEST";
    case MessageType::AddFriendResponse:
        return "ADD_FRIEND_RESPONSE";
    case MessageType::ListFriendsRequest:
        return "LIST_FRIENDS_REQUEST";
    case MessageType::ListFriendsResponse:
        return "LIST_FRIENDS_RESPONSE";
    case MessageType::PrivateMessageRequest:
        return "PRIVATE_MESSAGE_REQUEST";
    case MessageType::PrivateMessageResponse:
        return "PRIVATE_MESSAGE_RESPONSE";
    case MessageType::PrivateMessagePush:
        return "PRIVATE_MESSAGE_PUSH";
    case MessageType::CreateGroupRequest:
        return "CREATE_GROUP_REQUEST";
    case MessageType::CreateGroupResponse:
        return "CREATE_GROUP_RESPONSE";
    case MessageType::JoinGroupRequest:
        return "JOIN_GROUP_REQUEST";
    case MessageType::JoinGroupResponse:
        return "JOIN_GROUP_RESPONSE";
    case MessageType::ListGroupsRequest:
        return "LIST_GROUPS_REQUEST";
    case MessageType::ListGroupsResponse:
        return "LIST_GROUPS_RESPONSE";
    case MessageType::GroupMessageRequest:
        return "GROUP_MESSAGE_REQUEST";
    case MessageType::GroupMessageResponse:
        return "GROUP_MESSAGE_RESPONSE";
    case MessageType::GroupMessagePush:
        return "GROUP_MESSAGE_PUSH";
    case MessageType::OfflineMessagesRequest:
        return "OFFLINE_MESSAGES_REQUEST";
    case MessageType::OfflineMessagesResponse:
        return "OFFLINE_MESSAGES_RESPONSE";
    case MessageType::HistoryRequest:
        return "HISTORY_REQUEST";
    case MessageType::HistoryResponse:
        return "HISTORY_RESPONSE";
    case MessageType::OfflineMessagesAckRequest:
        return "OFFLINE_MESSAGES_ACK_REQUEST";
    case MessageType::OfflineMessagesAckResponse:
        return "OFFLINE_MESSAGES_ACK_RESPONSE";
    case MessageType::DeliveryAckRequest:
        return "DELIVERY_ACK_REQUEST";
    case MessageType::DeliveryAckResponse:
        return "DELIVERY_ACK_RESPONSE";
    case MessageType::ErrorResponse:
        return "ERROR_RESPONSE";
    case MessageType::Unknown:
        return "UNKNOWN";
    }

    return "UNKNOWN";
}

bool isRequestType(MessageType type) noexcept {
    switch (type) {
    case MessageType::HeartbeatRequest:
    case MessageType::RegisterRequest:
    case MessageType::LoginRequest:
    case MessageType::LogoutRequest:
    case MessageType::AddFriendRequest:
    case MessageType::ListFriendsRequest:
    case MessageType::PrivateMessageRequest:
    case MessageType::CreateGroupRequest:
    case MessageType::JoinGroupRequest:
    case MessageType::ListGroupsRequest:
    case MessageType::GroupMessageRequest:
    case MessageType::OfflineMessagesRequest:
    case MessageType::HistoryRequest:
    case MessageType::OfflineMessagesAckRequest:
    case MessageType::DeliveryAckRequest:
        return true;
    case MessageType::Unknown:
    case MessageType::HeartbeatResponse:
    case MessageType::RegisterResponse:
    case MessageType::LoginResponse:
    case MessageType::LogoutResponse:
    case MessageType::AddFriendResponse:
    case MessageType::ListFriendsResponse:
    case MessageType::PrivateMessageResponse:
    case MessageType::PrivateMessagePush:
    case MessageType::CreateGroupResponse:
    case MessageType::JoinGroupResponse:
    case MessageType::ListGroupsResponse:
    case MessageType::GroupMessageResponse:
    case MessageType::GroupMessagePush:
    case MessageType::OfflineMessagesResponse:
    case MessageType::HistoryResponse:
    case MessageType::OfflineMessagesAckResponse:
    case MessageType::DeliveryAckResponse:
    case MessageType::ErrorResponse:
        return false;
    }

    return false;
}

bool isResponseType(MessageType type) noexcept {
    switch (type) {
    case MessageType::HeartbeatResponse:
    case MessageType::RegisterResponse:
    case MessageType::LoginResponse:
    case MessageType::LogoutResponse:
    case MessageType::AddFriendResponse:
    case MessageType::ListFriendsResponse:
    case MessageType::PrivateMessageResponse:
    case MessageType::CreateGroupResponse:
    case MessageType::JoinGroupResponse:
    case MessageType::ListGroupsResponse:
    case MessageType::GroupMessageResponse:
    case MessageType::OfflineMessagesResponse:
    case MessageType::HistoryResponse:
    case MessageType::OfflineMessagesAckResponse:
    case MessageType::DeliveryAckResponse:
    case MessageType::ErrorResponse:
        return true;
    case MessageType::Unknown:
    case MessageType::HeartbeatRequest:
    case MessageType::RegisterRequest:
    case MessageType::LoginRequest:
    case MessageType::LogoutRequest:
    case MessageType::AddFriendRequest:
    case MessageType::ListFriendsRequest:
    case MessageType::PrivateMessageRequest:
    case MessageType::PrivateMessagePush:
    case MessageType::CreateGroupRequest:
    case MessageType::JoinGroupRequest:
    case MessageType::ListGroupsRequest:
    case MessageType::GroupMessageRequest:
    case MessageType::GroupMessagePush:
    case MessageType::OfflineMessagesRequest:
    case MessageType::HistoryRequest:
    case MessageType::OfflineMessagesAckRequest:
    case MessageType::DeliveryAckRequest:
        return false;
    }

    return false;
}

bool isPushType(MessageType type) noexcept {
    switch (type) {
    case MessageType::PrivateMessagePush:
    case MessageType::GroupMessagePush:
        return true;
    case MessageType::Unknown:
    case MessageType::HeartbeatRequest:
    case MessageType::HeartbeatResponse:
    case MessageType::RegisterRequest:
    case MessageType::RegisterResponse:
    case MessageType::LoginRequest:
    case MessageType::LoginResponse:
    case MessageType::LogoutRequest:
    case MessageType::LogoutResponse:
    case MessageType::AddFriendRequest:
    case MessageType::AddFriendResponse:
    case MessageType::ListFriendsRequest:
    case MessageType::ListFriendsResponse:
    case MessageType::PrivateMessageRequest:
    case MessageType::PrivateMessageResponse:
    case MessageType::CreateGroupRequest:
    case MessageType::CreateGroupResponse:
    case MessageType::JoinGroupRequest:
    case MessageType::JoinGroupResponse:
    case MessageType::ListGroupsRequest:
    case MessageType::ListGroupsResponse:
    case MessageType::GroupMessageRequest:
    case MessageType::GroupMessageResponse:
    case MessageType::OfflineMessagesRequest:
    case MessageType::OfflineMessagesResponse:
    case MessageType::HistoryRequest:
    case MessageType::HistoryResponse:
    case MessageType::OfflineMessagesAckRequest:
    case MessageType::OfflineMessagesAckResponse:
    case MessageType::DeliveryAckRequest:
    case MessageType::DeliveryAckResponse:
    case MessageType::ErrorResponse:
        return false;
    }

    return false;
}

}  // namespace liteim
