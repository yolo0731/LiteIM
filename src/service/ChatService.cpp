#include "liteim/service/ChatService.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/protocol/TlvCodec.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace liteim {
namespace {

constexpr std::uint64_t kSmallUserIdConversationBase = 10000;

Status notLoggedInStatus() {
    return Status::error(ErrorCode::InvalidArgument, "session is not logged in");
}

Status invalidReceiverStatus() {
    return Status::error(ErrorCode::InvalidArgument, "receiver user id is invalid");
}

Status emptyMessageTextStatus() {
    return Status::error(ErrorCode::InvalidArgument, "message text must not be empty");
}

Status privateConversationId(std::uint64_t sender_id, std::uint64_t receiver_id,
                             std::uint64_t& conversation_id) {
    conversation_id = 0;
    if (sender_id == 0 || receiver_id == 0 || sender_id == receiver_id) {
        return invalidReceiverStatus();
    }

    const auto left = std::min(sender_id, receiver_id);
    const auto right = std::max(sender_id, receiver_id);
    if (left < kSmallUserIdConversationBase && right < kSmallUserIdConversationBase) {
        conversation_id = left * kSmallUserIdConversationBase + right;
        return Status::ok();
    }

    if (right > std::numeric_limits<std::uint32_t>::max()) {
        return Status::error(ErrorCode::InvalidArgument,
                             "user id is too large for private conversation id");
    }
    conversation_id = (left << 32U) | right;
    return Status::ok();
}

Status appendConversationType(ConversationType type, Packet& packet) {
    return appendUint64(TlvType::ConversationType, static_cast<std::uint64_t>(type), packet.body);
}

}  // namespace

ChatService::ChatService(IStorage& storage, ICache& cache, OnlineService& online_service)
    : storage_(storage), cache_(cache), online_service_(online_service) {}

Status ChatService::registerHandlers(MessageRouter& router) {
    return router.registerHandler(
        MessageType::PrivateMessageRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handlePrivateMessage(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
}

Status ChatService::handlePrivateMessage(const MessageRouter::RouterRequest& request,
                                         Packet& response) {
    std::uint64_t sender_id = 0;
    const auto sender_status = currentUserId(request, sender_id);
    if (!sender_status.isOk()) {
        return sender_status;
    }

    std::uint64_t receiver_id = 0;
    const auto receiver_status = getUint64(request.fields, TlvType::ReceiverId, receiver_id);
    if (!receiver_status.isOk()) {
        return receiver_status;
    }
    if (receiver_id == 0 || receiver_id == sender_id) {
        return invalidReceiverStatus();
    }

    std::string message_text;
    const auto text_status = getString(request.fields, TlvType::MessageText, message_text);
    if (!text_status.isOk()) {
        return text_status;
    }
    if (message_text.empty()) {
        return emptyMessageTextStatus();
    }

    UserRecord receiver;
    const auto find_status = storage_.findUserById(receiver_id, receiver);
    if (!find_status.isOk()) {
        return find_status;
    }

    std::uint64_t conversation_id = 0;
    const auto conversation_status =
        privateConversationId(sender_id, receiver_id, conversation_id);
    if (!conversation_status.isOk()) {
        return conversation_status;
    }

    Session::Ptr receiver_session;
    const auto session_status = online_service_.getSessionByUser(receiver_id, receiver_session);
    const bool receiver_online = session_status.isOk();
    if (!session_status.isOk() && session_status.code() != ErrorCode::NotFound) {
        return session_status;
    }

    MessageRecord message;
    message.conversation = {ConversationType::kPrivate, conversation_id};
    message.sender_id = sender_id;
    message.receiver_id = receiver_id;
    message.text = std::move(message_text);

    const std::vector<std::uint64_t> offline_user_ids =
        receiver_online ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{receiver_id};

    MessageRecord saved_message;
    const auto save_status =
        storage_.saveMessageWithOfflineRecipients(message, offline_user_ids, saved_message);
    if (!save_status.isOk()) {
        return save_status;
    }

    if (receiver_online) {
        Packet push;
        push.header.msg_type = MessageType::PrivateMessagePush;
        const auto append_status = appendMessageFields(saved_message, push);
        if (!append_status.isOk()) {
            return append_status;
        }
        const auto send_status = receiver_session->sendPacket(push);
        if (!send_status.isOk()) {
            return send_status;
        }
    } else {
        std::uint64_t unread_count = 0;
        const auto unread_status =
            cache_.incrUnread(UnreadKey{receiver_id, saved_message.conversation}, 1, unread_count);
        if (!unread_status.isOk()) {
            return unread_status;
        }
    }

    response.header.msg_type = MessageType::PrivateMessageResponse;
    response.header.seq_id = request.packet.header.seq_id;
    return appendMessageFields(saved_message, response);
}

Status ChatService::currentUserId(const MessageRouter::RouterRequest& request,
                                  std::uint64_t& user_id) {
    user_id = 0;
    if (request.session == nullptr || request.session->closed()) {
        return notLoggedInStatus();
    }

    const auto status = online_service_.getUserBySession(request.session->id(), user_id);
    if (!status.isOk() && status.code() == ErrorCode::NotFound) {
        return notLoggedInStatus();
    }
    return status;
}

Status ChatService::appendMessageFields(const MessageRecord& message, Packet& packet) {
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
