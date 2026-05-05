#include "liteim/protocol/Tlv.hpp"

namespace liteim {

std::string_view toString(TlvType type) noexcept {
    switch (type) {
        case TlvType::Username:
            return "USERNAME";
        case TlvType::Password:
            return "PASSWORD";
        case TlvType::UserId:
            return "USER_ID";
        case TlvType::Nickname:
            return "NICKNAME";
        case TlvType::Token:
            return "TOKEN";
        case TlvType::SessionId:
            return "SESSION_ID";
        case TlvType::FriendId:
            return "FRIEND_ID";
        case TlvType::TargetUserId:
            return "TARGET_USER_ID";
        case TlvType::GroupId:
            return "GROUP_ID";
        case TlvType::GroupName:
            return "GROUP_NAME";
        case TlvType::ConversationType:
            return "CONVERSATION_TYPE";
        case TlvType::ConversationId:
            return "CONVERSATION_ID";
        case TlvType::MessageId:
            return "MESSAGE_ID";
        case TlvType::MessageText:
            return "MESSAGE_TEXT";
        case TlvType::SenderId:
            return "SENDER_ID";
        case TlvType::ReceiverId:
            return "RECEIVER_ID";
        case TlvType::TimestampMs:
            return "TIMESTAMP_MS";
        case TlvType::Offset:
            return "OFFSET";
        case TlvType::Limit:
            return "LIMIT";
        case TlvType::UnreadCount:
            return "UNREAD_COUNT";
        case TlvType::ErrorCode:
            return "ERROR_CODE";
        case TlvType::ErrorMessage:
            return "ERROR_MESSAGE";
        case TlvType::BotId:
            return "BOT_ID";
        case TlvType::PersonaId:
            return "PERSONA_ID";
        case TlvType::Unknown:
            return "UNKNOWN";
    }

    return "UNKNOWN";
}

}  // namespace liteim
