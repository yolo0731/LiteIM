#include "liteim/service/HistoryService.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim/service/MessagePacketBuilder.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace liteim {
namespace {

constexpr std::uint32_t kHardMaxHistoryLimit = 50;
constexpr std::uint64_t kSmallUserIdConversationBase = 10000;

Status notLoggedInStatus() {
    return Status::error(ErrorCode::InvalidArgument, "session is not logged in");
}

Status invalidConversationTypeStatus() {
    return Status::error(ErrorCode::InvalidArgument, "conversation type is invalid");
}

Status invalidConversationIdStatus() {
    return Status::error(ErrorCode::InvalidArgument, "conversation id must be positive");
}

Status invalidLimitStatus() {
    return Status::error(ErrorCode::InvalidArgument, "history limit must be positive");
}

Status notConversationMemberStatus() {
    return Status::error(ErrorCode::InvalidArgument, "user is not a conversation member");
}

// 把原始的 uint64_t 转成 ConversationType 枚举
Status toConversationType(std::uint64_t raw, ConversationType& type) {
    if (raw == static_cast<std::uint64_t>(ConversationType::kPrivate)) {
        type = ConversationType::kPrivate;
        return Status::ok();
    }
    if (raw == static_cast<std::uint64_t>(ConversationType::kGroup)) {
        type = ConversationType::kGroup;
        return Status::ok();
    }
    return invalidConversationTypeStatus();
}

bool containsMember(const std::vector<GroupMemberRecord>& members, std::uint64_t user_id) {
    return std::any_of(members.begin(), members.end(), [user_id](const GroupMemberRecord& member) {
        return member.user_id == user_id;
    });
}
// 解码私聊会话 ID，得到两个用户 ID
bool decodePrivateConversationId(std::uint64_t conversation_id, std::uint64_t& left_user_id,
                                 std::uint64_t& right_user_id) {
    left_user_id = 0;
    right_user_id = 0;
    if (conversation_id == 0U) {
        return false;
    }

    const auto small_max = kSmallUserIdConversationBase * kSmallUserIdConversationBase;
    if (conversation_id < small_max) {
        left_user_id = conversation_id / kSmallUserIdConversationBase;
        right_user_id = conversation_id % kSmallUserIdConversationBase;
    } else {
        left_user_id = conversation_id >> 32U;
        right_user_id = conversation_id & std::numeric_limits<std::uint32_t>::max();
    }

    return left_user_id != 0U && right_user_id != 0U && left_user_id != right_user_id;
}

// 检查用户是否是这个私聊会话的成员
bool isPrivateConversationMember(std::uint64_t user_id, std::uint64_t conversation_id) {
    std::uint64_t left_user_id = 0;
    std::uint64_t right_user_id = 0;
    if (!decodePrivateConversationId(conversation_id, left_user_id, right_user_id)) {
        return false;
    }
    return user_id == left_user_id || user_id == right_user_id;
}

}  // namespace

HistoryService::HistoryService(IStorage& storage, OnlineService& online_service,
                               HistoryServiceOptions options)
    : storage_(storage), online_service_(online_service), options_(std::move(options)) {
    // 检查分页配置是否合法
    if (options_.max_limit == 0U || options_.max_limit > kHardMaxHistoryLimit ||
        options_.default_limit == 0U || options_.default_limit > options_.max_limit) {
        throw std::invalid_argument("history limits must satisfy 1 <= default <= max <= 50");
    }
}

Status HistoryService::registerHandlers(MessageRouter& router) {
    return router.registerHandler(
        MessageType::HistoryRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleHistory(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
}

Status HistoryService::handleHistory(const MessageRouter::RouterRequest& request,
                                     Packet& response) {
    std::uint64_t user_id = 0;
    const auto user_status = currentUserId(request, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    // 从请求里解析出 HistoryQuery，包含会话类型、会话 ID、分页信息等
    HistoryQuery query;
    const auto query_status = buildQuery(request, query);
    if (!query_status.isOk()) {
        return query_status;
    }

    // 判断当前用户是否有权限看这个会话
    const auto auth_status = authorizeQuery(user_id, query);
    if (!auth_status.isOk()) {
        return auth_status;
    }

    // 从存储里查历史消息写进响应包
    std::vector<MessageRecord> messages;
    const auto history_status = storage_.getHistory(query, messages);
    if (!history_status.isOk()) {
        return history_status;
    }

    response.header.msg_type = MessageType::HistoryResponse;
    response.header.seq_id = request.packet.header.seq_id;
    return appendMessages(messages, response);
}

const HistoryServiceOptions& HistoryService::options() const noexcept {
    return options_;
}

Status HistoryService::currentUserId(const MessageRouter::RouterRequest& request,
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

// 从请求包里解析分页条件、会话类型，组装成 HistoryQuery 结构体
Status HistoryService::buildQuery(const MessageRouter::RouterRequest& request,
                                  HistoryQuery& query) const {
    std::uint64_t raw_type = 0;
    const auto type_status = getUint64(request.fields, TlvType::ConversationType, raw_type);
    if (!type_status.isOk()) {
        return type_status;
    }
    const auto convert_status = toConversationType(raw_type, query.conversation.type);
    if (!convert_status.isOk()) {
        return convert_status;
    }

    const auto conversation_status =
        getUint64(request.fields, TlvType::ConversationId, query.conversation.id);
    if (!conversation_status.isOk()) {
        return conversation_status;
    }
    if (query.conversation.id == 0U) {
        return invalidConversationIdStatus();
    }

    // before_message_id 可选，默认是 0，表示从最新的消息开始往前查，如果提供了这个字段，则从指定消息 ID 往前查
    query.before_message_id = 0;
    if (request.fields.find(TlvType::MessageId) != request.fields.end()) {
        const auto before_status =
            getUint64(request.fields, TlvType::MessageId, query.before_message_id);
        if (!before_status.isOk()) {
            return before_status;
        }
    }

    // limit 可选，默认是 options_.default_limit，最大不能超过 options_.max_limit
    query.limit = options_.default_limit;
    if (request.fields.find(TlvType::Limit) != request.fields.end()) {
        std::uint64_t requested_limit = 0;
        const auto limit_status = getUint64(request.fields, TlvType::Limit, requested_limit);
        if (!limit_status.isOk()) {
            return limit_status;
        }
        if (requested_limit == 0U) {
            return invalidLimitStatus();
        }
        query.limit = requested_limit > options_.max_limit
                          ? options_.max_limit
                          : static_cast<std::uint32_t>(requested_limit);
    }

    return Status::ok();
}

Status HistoryService::authorizeQuery(std::uint64_t user_id, const HistoryQuery& query) {
    // 检验私聊
    if (query.conversation.type == ConversationType::kPrivate) {
        if (!isPrivateConversationMember(user_id, query.conversation.id)) {
            return notConversationMemberStatus();
        }
        return Status::ok();
    }

    // 检验群聊
    GroupRecord group;
    const auto group_status = storage_.findGroupById(query.conversation.id, group);
    if (!group_status.isOk()) {
        return group_status;
    }

    std::vector<GroupMemberRecord> members;
    const auto members_status = storage_.getGroupMembers(query.conversation.id, members);
    if (!members_status.isOk()) {
        return members_status;
    }
    if (!containsMember(members, user_id)) {
        return notConversationMemberStatus();
    }
    return Status::ok();
}

Status HistoryService::appendMessages(const std::vector<MessageRecord>& messages,
                                      Packet& response) {
    for (const auto& message : messages) {
        const auto status = appendMessageFields(message, response);
        if (!status.isOk()) {
            return status;
        }
    }
    return Status::ok();
}

}  // namespace liteim
