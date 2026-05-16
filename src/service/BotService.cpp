#include "liteim/service/BotService.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/base/Logger.hpp"
#include "liteim/protocol/TlvCodec.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace liteim {
namespace {

Status emptyBotReplyStatus() {
    return Status::error(ErrorCode::InvalidArgument, "bot reply text must not be empty");
}

Status appendConversationType(ConversationType type, Packet& packet) {
    return appendUint64(TlvType::ConversationType, static_cast<std::uint64_t>(type), packet.body);
}

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

Status sendMessagePush(const Session::Ptr& session, MessageType type, const MessageRecord& message) {
    if (session == nullptr || session->closed()) {
        return Status::error(ErrorCode::InvalidArgument, "target session is not available");
    }

    Packet push;
    push.header.msg_type = type;
    const auto append_status = appendMessageFields(message, push);
    if (!append_status.isOk()) {
        return append_status;
    }
    return session->sendPacket(push);
}

Status saveBotReply(IStorage& storage, const MessageRecord& message,
                    const std::vector<std::uint64_t>& offline_user_ids,
                    MessageRecord& saved_message) {
    return storage.saveMessageWithOfflineRecipients(message, offline_user_ids, saved_message);
}

void incrementUnreadOrLog(ICache& cache, std::uint64_t user_id,
                          const MessageRecord& saved_message) {
    std::uint64_t unread_count = 0;
    const auto unread_status =
        cache.incrUnread(UnreadKey{user_id, saved_message.conversation}, 1, unread_count);
    if (!unread_status.isOk()) {
        Logger::get()->warn(
            "Failed to increment bot reply unread for user {} conversation {} after message {} "
            "was saved: {}",
            user_id, saved_message.conversation.id, saved_message.message_id,
            unread_status.message());
    }
}

bool containsMember(const std::vector<GroupMemberRecord>& members, std::uint64_t user_id) {
    return std::any_of(members.begin(), members.end(), [user_id](const GroupMemberRecord& member) {
        return member.user_id == user_id;
    });
}

}  // namespace

EchoBotGateway::EchoBotGateway(std::string prefix) : prefix_(std::move(prefix)) {}

Status EchoBotGateway::onPrivateMessage(const MessageRecord& message, BotReply& reply) {
    reply.text = prefix_ + message.text;
    return Status::ok();
}

Status EchoBotGateway::onGroupMention(const MessageRecord& message, BotReply& reply) {
    reply.text = prefix_ + message.text;
    return Status::ok();
}

BotService::BotService(IStorage& storage, ICache& cache, OnlineService& online_service,
                       BotGateway& gateway, BotOptions options)
    : storage_(storage),
      cache_(cache),
      online_service_(online_service),
      gateway_(gateway),
      options_(std::move(options)) {}

const BotOptions& BotService::options() const noexcept {
    return options_;
}

bool BotService::isBotUser(std::uint64_t user_id) const noexcept {
    return user_id == options_.user_id;
}

bool BotService::isBotMentioned(const std::string& text) const {
    return !options_.mention.empty() && text.find(options_.mention) != std::string::npos;
}

bool BotService::shouldHandleGroupMention(
    const MessageRecord& message, const std::vector<GroupMemberRecord>& members) const {
    return message.conversation.type == ConversationType::kGroup && !isBotUser(message.sender_id) &&
           isBotMentioned(message.text) && containsMember(members, options_.user_id);
}

Status BotService::handlePrivateMessageToBot(const MessageRecord& user_message,
                                             const Session::Ptr& sender_session) {
    BotReply reply;
    const auto gateway_status = gateway_.onPrivateMessage(user_message, reply);
    if (!gateway_status.isOk()) {
        return gateway_status;
    }
    if (reply.text.empty()) {
        return emptyBotReplyStatus();
    }

    MessageRecord message;
    message.conversation = user_message.conversation;
    message.sender_id = options_.user_id;
    message.receiver_id = user_message.sender_id;
    message.text = std::move(reply.text);

    MessageRecord saved_message;
    const auto save_status = saveBotReply(storage_, message, {}, saved_message);
    if (!save_status.isOk()) {
        return save_status;
    }
    return sendMessagePush(sender_session, MessageType::PrivateMessagePush, saved_message);
}

Status BotService::handleGroupMention(const MessageRecord& user_message,
                                      const std::vector<GroupMemberRecord>& members) {
    if (!shouldHandleGroupMention(user_message, members)) {
        return Status::ok();
    }

    BotReply reply;
    const auto gateway_status = gateway_.onGroupMention(user_message, reply);
    if (!gateway_status.isOk()) {
        return gateway_status;
    }
    if (reply.text.empty()) {
        return emptyBotReplyStatus();
    }

    std::vector<Session::Ptr> online_sessions;
    std::vector<std::uint64_t> offline_user_ids;
    for (const auto& member : members) {
        if (isBotUser(member.user_id)) {
            continue;
        }

        Session::Ptr member_session;
        const auto session_status =
            online_service_.getSessionByUser(member.user_id, member_session);
        if (session_status.isOk()) {
            online_sessions.push_back(std::move(member_session));
            continue;
        }
        if (session_status.code() == ErrorCode::NotFound) {
            offline_user_ids.push_back(member.user_id);
            continue;
        }
        return session_status;
    }

    MessageRecord message;
    message.conversation = user_message.conversation;
    message.sender_id = options_.user_id;
    message.receiver_id = user_message.receiver_id;
    message.text = std::move(reply.text);

    MessageRecord saved_message;
    const auto save_status = saveBotReply(storage_, message, offline_user_ids, saved_message);
    if (!save_status.isOk()) {
        return save_status;
    }

    for (const auto user_id : offline_user_ids) {
        incrementUnreadOrLog(cache_, user_id, saved_message);
    }
    for (const auto& session : online_sessions) {
        const auto send_status =
            sendMessagePush(session, MessageType::GroupMessagePush, saved_message);
        if (!send_status.isOk()) {
            return send_status;
        }
    }
    return Status::ok();
}

}  // namespace liteim
