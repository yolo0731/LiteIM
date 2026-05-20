#include "liteim/service/ChatService.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/base/Logger.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim/service/MessagePacketBuilder.hpp"
#include "liteim/service/Validation.hpp"

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

Status emptyClientMessageIdStatus() {
    return Status::error(ErrorCode::InvalidArgument, "client message id must not be empty");
}

Status invalidMessageIdStatus() {
    return Status::error(ErrorCode::InvalidArgument, "message id is invalid");
}

// 用两个用户 ID 生成一个私聊会话 ID
Status privateConversationId(std::uint64_t sender_id, std::uint64_t receiver_id,
                             std::uint64_t& conversation_id) {
    conversation_id = 0;
    if (sender_id == 0 || receiver_id == 0 || sender_id == receiver_id) {
        return invalidReceiverStatus();
    }

    const auto left = std::min(sender_id, receiver_id);
    const auto right = std::max(sender_id, receiver_id);
    if (left < kSmallUserIdConversationBase && right < kSmallUserIdConversationBase) {
        conversation_id = left * kSmallUserIdConversationBase + right;  // 生成私聊会话 ID
        return Status::ok();
    }

    if (right > std::numeric_limits<std::uint32_t>::max()) {
        return Status::error(ErrorCode::InvalidArgument,
                             "user id is too large for private conversation id");
    }
    conversation_id = (left << 32U) | right;
    return Status::ok();
}

}  // namespace

ChatService::ChatService(IStorage& storage, ICache& cache, OnlineService& online_service)
    : storage_(storage),
      cache_(cache),
      online_service_(online_service) {}

Status ChatService::registerHandlers(MessageRouter& router) {
    const auto private_status = router.registerHandler(
        MessageType::PrivateMessageRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handlePrivateMessage(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
    if (!private_status.isOk()) {
        return private_status;
    }

    return router.registerHandler(
        MessageType::DeliveryAckRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleDeliveryAck(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
}

Status ChatService::handlePrivateMessage(const MessageRouter::RouterRequest& request,
                                         Packet& response) {
    std::uint64_t sender_id = 0;
    //  根据 session 获取当前登录的 user_id
    const auto sender_status = currentUserId(request, sender_id);
    if (!sender_status.isOk()) {
        return sender_status;
    }

    std::uint64_t receiver_id = 0;
    // 从 TLV 里读取 ReceiverId
    const auto receiver_status = getUint64(request.fields, TlvType::ReceiverId, receiver_id);
    if (!receiver_status.isOk()) {
        return receiver_status;
    }
    if (receiver_id == 0 || receiver_id == sender_id) {
        return invalidReceiverStatus();
    }

    std::string message_text;
    // 从 TLV 里读取 MessageText
    const auto text_status = getString(request.fields, TlvType::MessageText, message_text);
    if (!text_status.isOk()) {
        return text_status;
    }
    if (message_text.empty()) {
        return emptyMessageTextStatus();
    }
    const auto text_length_status =
        validateMaxBytes(message_text, kMaxMessageTextBytes, "message text");
    if (!text_length_status.isOk()) {
        return text_length_status;
    }

    std::string client_msg_id;
    if (request.fields.find(TlvType::ClientMessageId) != request.fields.end()) {
        const auto client_status =
            getString(request.fields, TlvType::ClientMessageId, client_msg_id);
        if (!client_status.isOk()) {
            return client_status;
        }
        if (client_msg_id.empty()) {
            return emptyClientMessageIdStatus();
        }
        const auto client_length_status =
            validateMaxBytes(client_msg_id, kMaxClientMessageIdBytes, "client message id");
        if (!client_length_status.isOk()) {
            return client_length_status;
        }
    }

    UserRecord receiver;
    const auto find_status = storage_.findUserById(receiver_id, receiver);
    if (!find_status.isOk()) {
        return find_status;
    }

    //生成私聊会话 ID
    std::uint64_t conversation_id = 0;
    const auto conversation_status = privateConversationId(sender_id, receiver_id, conversation_id);
    if (!conversation_status.isOk()) {
        return conversation_status;
    }

    // 检查接收用户是否在线，如果在线就直接发消息，否则就存离线消息和未读计数
    Session::Ptr receiver_session;
    bool receiver_online = false;
    const auto session_status = online_service_.getSessionByUser(receiver_id, receiver_session);
    receiver_online = session_status.isOk();
    if (!session_status.isOk() && session_status.code() != ErrorCode::NotFound) {
        return session_status;
    }

    // 构建消息
    MessageRecord message;
    message.conversation = {ConversationType::kPrivate, conversation_id};
    message.sender_id = sender_id;
    message.receiver_id = receiver_id;
    message.text = std::move(message_text);
    message.client_msg_id = std::move(client_msg_id);

    // 如果离线，offline_user_ids数组里放 receiver_id，然后在 saveMessageWithOfflineRecipients 里插入到offline_messages表里
    const std::vector<std::uint64_t> offline_user_ids =
        !receiver_online ? std::vector<std::uint64_t>{receiver_id} : std::vector<std::uint64_t>{};

    // 保存消息记录，如果接收用户离线就同时保存离线消息记录，然后把数据库生成/补齐后的完整消息信息放到 saved_message 里返回（包括 message_id 和 created_at_ms）
    MessageRecord saved_message;
    const auto save_status =
        storage_.saveMessageWithOfflineRecipients(message, offline_user_ids, saved_message);
    if (!save_status.isOk()) {
        if (save_status.code() != ErrorCode::AlreadyExists || message.client_msg_id.empty()) {
            return save_status;
        }
        const auto find_status = storage_.findMessageByClientMessageId(
            sender_id, message.client_msg_id, saved_message);
        if (!find_status.isOk()) {
            return find_status;
        }
        response.header.msg_type = MessageType::PrivateMessageResponse;
        response.header.seq_id = request.packet.header.seq_id;
        return appendMessageFields(saved_message, response);
    }

    if (receiver_online) {
        // 在线：直接把消息推送给接收用户
        Packet push;
        push.header.msg_type = MessageType::PrivateMessagePush;
        const auto append_status = appendMessageFields(saved_message, push);
        if (!append_status.isOk()) {
            return append_status;
        }
        const auto send_status = receiver_session->sendPacket(push);  //push是非相应包不带 seq_id
        if (!send_status.isOk()) {
            return send_status;
        }
    } else {  // 离线：把未读计数加一
        std::uint64_t unread_count = 0;
        const auto unread_status =
            cache_.incrUnread(UnreadKey{receiver_id, saved_message.conversation}, 1, unread_count);
        if (!unread_status.isOk()) {
            Logger::get()->warn(
                "Failed to increment unread for user {} conversation {} after message {} was "
                "saved: {}",
                receiver_id, saved_message.conversation.id, saved_message.message_id,
                unread_status.message());
        }
    }

    // 把消息字段写进响应包的 TLV body，之后MessageRouter会统一设置响应头并发回发送用户
    response.header.msg_type = MessageType::PrivateMessageResponse;
    response.header.seq_id = request.packet.header.seq_id;
    return appendMessageFields(saved_message, response);
}

Status ChatService::handleDeliveryAck(const MessageRouter::RouterRequest& request,
                                      Packet& response) {
    std::uint64_t user_id = 0;
    const auto user_status = currentUserId(request, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    std::uint64_t message_id = 0;
    const auto message_status = getUint64(request.fields, TlvType::MessageId, message_id);
    if (!message_status.isOk()) {
        return message_status;
    }
    if (message_id == 0U) {
        return invalidMessageIdStatus();
    }

    MessageRecord message;
    const auto ack_status = storage_.ackPrivateMessageDelivery(user_id, message_id, message);
    if (!ack_status.isOk()) {
        return ack_status;
    }

    response.header.msg_type = MessageType::DeliveryAckResponse;
    response.header.seq_id = request.packet.header.seq_id;
    const auto id_status = appendUint64(TlvType::MessageId, message.message_id, response.body);
    if (!id_status.isOk()) {
        return id_status;
    }
    return appendUint64(TlvType::DeliveryStatus,
                        static_cast<std::uint64_t>(DeliveryStatus::kDelivered), response.body);
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

}  // namespace liteim
