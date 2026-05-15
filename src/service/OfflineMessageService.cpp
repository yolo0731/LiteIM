#include "liteim/service/OfflineMessageService.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/protocol/TlvCodec.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace liteim {
namespace {

constexpr std::uint32_t kHardMaxMessagesPerPull = 100;

Status notLoggedInStatus() {
    return Status::error(ErrorCode::InvalidArgument, "session is not logged in");
}

Status invalidLimitStatus() {
    return Status::error(ErrorCode::InvalidArgument, "offline message limit must be positive");
}

Status appendConversationType(ConversationType type, Packet& packet) {
    return appendUint64(TlvType::ConversationType, static_cast<std::uint64_t>(type), packet.body);
}

bool sameConversation(const ConversationKey& lhs, const ConversationKey& rhs) {
    return lhs.type == rhs.type && lhs.id == rhs.id;
}

bool containsConversation(const std::vector<ConversationKey>& conversations,
                          const ConversationKey& conversation) {
    return std::any_of(conversations.begin(), conversations.end(),
                       [&](const ConversationKey& item) {
                           return sameConversation(item, conversation);
                       });
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

}  // namespace

OfflineMessageService::OfflineMessageService(IStorage& storage, ICache& cache,
                                             OnlineService& online_service,
                                             OfflineMessageServiceOptions options)
    : storage_(storage), cache_(cache), online_service_(online_service),
      options_(std::move(options)) {
    if (options_.max_messages_per_pull == 0U ||
        options_.max_messages_per_pull > kHardMaxMessagesPerPull) {
        throw std::invalid_argument("offline max messages per pull must be in [1, 100]");
    }
}

Status OfflineMessageService::registerHandlers(MessageRouter& router) {
    return router.registerHandler(
        MessageType::OfflineMessagesRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleOfflineMessages(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
}

Status OfflineMessageService::handleOfflineMessages(
    const MessageRouter::RouterRequest& request, Packet& response) {
    std::uint64_t user_id = 0;
    const auto user_status = currentUserId(request, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    std::uint32_t limit = options_.max_messages_per_pull;
    const auto limit_status = requestLimit(request, limit);
    if (!limit_status.isOk()) {
        return limit_status;
    }

    std::vector<OfflineMessageRecord> messages;
    const auto fetch_status = storage_.getOfflineMessages(user_id, messages);
    if (!fetch_status.isOk()) {
        return fetch_status;
    }
    if (messages.size() > limit) {
        messages.resize(limit);
    }

    response.header.msg_type = MessageType::OfflineMessagesResponse;
    response.header.seq_id = request.packet.header.seq_id;
    const auto append_status = appendMessages(messages, response);
    if (!append_status.isOk()) {
        return append_status;
    }

    const auto clear_status = clearUnreadForMessages(user_id, messages);
    if (!clear_status.isOk()) {
        return clear_status;
    }

    std::vector<std::uint64_t> message_ids;
    message_ids.reserve(messages.size());
    for (const auto& message : messages) {
        message_ids.push_back(message.message.message_id);
    }
    return storage_.markOfflineDelivered(user_id, message_ids);
}

const OfflineMessageServiceOptions& OfflineMessageService::options() const noexcept {
    return options_;
}

Status OfflineMessageService::currentUserId(const MessageRouter::RouterRequest& request,
                                            std::uint64_t& user_id) {
    if (request.session == nullptr) {
        return notLoggedInStatus();
    }

    const auto status = online_service_.getUserBySession(request.session->id(), user_id);
    if (!status.isOk() && status.code() == ErrorCode::NotFound) {
        return notLoggedInStatus();
    }
    return status;
}

Status OfflineMessageService::requestLimit(const MessageRouter::RouterRequest& request,
                                           std::uint32_t& limit) const {
    limit = options_.max_messages_per_pull;
    if (request.fields.find(TlvType::Limit) == request.fields.end()) {
        return Status::ok();
    }

    std::uint64_t requested_limit = 0;
    const auto limit_status = getUint64(request.fields, TlvType::Limit, requested_limit);
    if (!limit_status.isOk()) {
        return limit_status;
    }
    if (requested_limit == 0U) {
        return invalidLimitStatus();
    }
    if (requested_limit < limit) {
        limit = static_cast<std::uint32_t>(requested_limit);
    }
    return Status::ok();
}

Status OfflineMessageService::appendMessages(const std::vector<OfflineMessageRecord>& messages,
                                             Packet& response) {
    for (const auto& offline_message : messages) {
        const auto status = appendMessageFields(offline_message.message, response);
        if (!status.isOk()) {
            return status;
        }
    }
    return Status::ok();
}

Status OfflineMessageService::clearUnreadForMessages(
    std::uint64_t user_id, const std::vector<OfflineMessageRecord>& messages) {
    std::vector<ConversationKey> conversations;
    conversations.reserve(messages.size());
    for (const auto& message : messages) {
        if (!containsConversation(conversations, message.message.conversation)) {
            conversations.push_back(message.message.conversation);
        }
    }

    for (const auto& conversation : conversations) {
        const auto status = cache_.clearUnread(UnreadKey{user_id, conversation});
        if (!status.isOk()) {
            return status;
        }
    }
    return Status::ok();
}

}  // namespace liteim
