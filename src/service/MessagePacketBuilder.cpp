#include "liteim/service/MessagePacketBuilder.hpp"

#include "liteim/protocol/TlvCodec.hpp"

namespace liteim {
namespace {

Status appendConversationType(ConversationType type, Packet& packet) {
    return appendUint64(TlvType::ConversationType, static_cast<std::uint64_t>(type), packet.body);
}

}  // namespace

Status appendMessageFields(const MessageRecord& message, Packet& packet) {
    const auto message_status = appendUint64(TlvType::MessageId, message.message_id, packet.body);
    if (!message_status.isOk()) {
        return message_status;
    }
    const auto type_status = appendConversationType(message.conversation.type, packet);
    if (!type_status.isOk()) {
        return type_status;
    }
    const auto conversation_status =
        appendUint64(TlvType::ConversationId, message.conversation.id, packet.body);
    if (!conversation_status.isOk()) {
        return conversation_status;
    }
    const auto sender_status = appendUint64(TlvType::SenderId, message.sender_id, packet.body);
    if (!sender_status.isOk()) {
        return sender_status;
    }
    const auto receiver_status =
        appendUint64(TlvType::ReceiverId, message.receiver_id, packet.body);
    if (!receiver_status.isOk()) {
        return receiver_status;
    }
    const auto text_status = appendString(TlvType::MessageText, message.text, packet.body);
    if (!text_status.isOk()) {
        return text_status;
    }
    return appendUint64(TlvType::TimestampMs, static_cast<std::uint64_t>(message.created_at_ms),
                        packet.body);
}

}  // namespace liteim
