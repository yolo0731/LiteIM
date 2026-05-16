#include "liteim/service/OfflineMessageService.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/base/Logger.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim/service/MessagePacketBuilder.hpp"

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
bool sameConversation(const ConversationKey& lhs, const ConversationKey& rhs) {
    return lhs.type == rhs.type && lhs.id == rhs.id;
}
// 只要 conversations 中有一个和 conversation 是同一个会话，就返回 true
bool containsConversation(const std::vector<ConversationKey>& conversations,
                          const ConversationKey& conversation) {
    return std::any_of(
        conversations.begin(), conversations.end(),
        [&](const ConversationKey& item) { return sameConversation(item, conversation); });
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

Status OfflineMessageService::handleOfflineMessages(const MessageRouter::RouterRequest& request,
                                                    Packet& response) {
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
    // 从数据库拉取待投递消息记录
    std::vector<OfflineMessageRecord> messages;
    const auto fetch_status = storage_.getOfflineMessages(user_id, limit, messages);
    if (!fetch_status.isOk()) {
        return fetch_status;
    }

    response.header.msg_type = MessageType::OfflineMessagesResponse;
    response.header.seq_id = request.packet.header.seq_id;
    const auto append_status = appendMessages(messages, response);
    if (!append_status.isOk()) {
        return append_status;
    }
    // 清 Redis 未读数
    const auto clear_status = clearUnreadForMessages(user_id, messages);
    if (!clear_status.isOk()) {
        Logger::get()->warn("Failed to clear unread counters for user {} after offline pull: {}",
                            user_id, clear_status.message());
    }
    // 将这些消息标记为已投递
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
// 解析请求中的 limit 参数，决定本次拉取的消息数量
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
// 把消息列表写成 TLV response body
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

// 未读数是每条离线消息都会 +1；清未读时才是每个会话只清一次
// 收集这批消息涉及哪些会话，并清理一个对话中的所有消息的未读数
Status
OfflineMessageService::clearUnreadForMessages(std::uint64_t user_id,
                                              const std::vector<OfflineMessageRecord>& messages) {
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
