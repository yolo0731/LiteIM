#include "liteim/service/GroupService.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/base/Logger.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim/service/BotService.hpp"
#include "liteim/service/MessagePacketBuilder.hpp"
#include "liteim/service/Validation.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace liteim {
namespace {

Status notLoggedInStatus() {
    return Status::error(ErrorCode::InvalidArgument, "session is not logged in");
}

Status invalidGroupIdStatus() {
    return Status::error(ErrorCode::InvalidArgument, "group id must be positive");
}

Status emptyGroupNameStatus() {
    return Status::error(ErrorCode::InvalidArgument, "group name must not be empty");
}

Status emptyMessageTextStatus() {
    return Status::error(ErrorCode::InvalidArgument, "message text must not be empty");
}

Status notGroupMemberStatus() {
    return Status::error(ErrorCode::InvalidArgument, "sender is not a group member");
}

// 判断某个用户是不是群成员
bool containsMember(const std::vector<GroupMemberRecord>& members, std::uint64_t user_id) {
    return std::any_of(members.begin(), members.end(), [user_id](const GroupMemberRecord& member) {
        return member.user_id == user_id;
    });
}

}  // namespace

GroupService::GroupService(IStorage& storage, ICache& cache, OnlineService& online_service,
                           BotService* bot_service)
    : storage_(storage),
      cache_(cache),
      online_service_(online_service),
      bot_service_(bot_service) {}

Status GroupService::registerHandlers(MessageRouter& router) {
    const auto create_status = router.registerHandler(
        MessageType::CreateGroupRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleCreateGroup(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
    if (!create_status.isOk()) {
        return create_status;
    }

    const auto join_status = router.registerHandler(
        MessageType::JoinGroupRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleJoinGroup(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
    if (!join_status.isOk()) {
        return join_status;
    }

    const auto list_status = router.registerHandler(
        MessageType::ListGroupsRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleListGroups(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
    if (!list_status.isOk()) {
        return list_status;
    }

    return router.registerHandler(
        MessageType::GroupMessageRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleGroupMessage(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
}

Status GroupService::handleCreateGroup(const MessageRouter::RouterRequest& request,
                                       Packet& response) {
    std::uint64_t owner_id = 0;
    const auto user_status = currentUserId(request, owner_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    std::string group_name;
    const auto name_status = getString(request.fields, TlvType::GroupName, group_name);
    if (!name_status.isOk()) {
        return name_status;
    }
    if (group_name.empty()) {
        return emptyGroupNameStatus();
    }
    const auto group_name_length_status =
        validateMaxBytes(group_name, kMaxGroupNameBytes, "group name");
    if (!group_name_length_status.isOk()) {
        return group_name_length_status;
    }

    GroupRecord created_group;
    const auto create_status =
        storage_.createGroup(CreateGroupRequest{owner_id, group_name}, created_group);
    if (!create_status.isOk()) {
        return create_status;
    }

    response.header.msg_type = MessageType::CreateGroupResponse;
    response.header.seq_id = request.packet.header.seq_id;
    return appendGroupFields(created_group, response);
}

Status GroupService::handleJoinGroup(const MessageRouter::RouterRequest& request,
                                     Packet& response) {
    std::uint64_t user_id = 0;
    const auto user_status = currentUserId(request, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    std::uint64_t group_id = 0;
    const auto group_status = getUint64(request.fields, TlvType::GroupId, group_id);
    if (!group_status.isOk()) {
        return group_status;
    }
    if (group_id == 0U) {
        return invalidGroupIdStatus();
    }

    // 确认群存在
    GroupRecord group;
    const auto find_status = storage_.findGroupById(group_id, group);
    if (!find_status.isOk()) {
        return find_status;
    }
    // 把用户添加到群成员表里
    const auto add_status = storage_.addGroupMember(group_id, user_id);
    if (!add_status.isOk()) {
        return add_status;
    }

    response.header.msg_type = MessageType::JoinGroupResponse;
    response.header.seq_id = request.packet.header.seq_id;
    // 把群信息写到响应的body里返回给客户端
    return appendGroupFields(group, response);
}

// 列出用户加入的群，返回的响应里每个TLV是所有加入的群的信息
Status GroupService::handleListGroups(const MessageRouter::RouterRequest& request,
                                      Packet& response) {
    std::uint64_t user_id = 0;
    const auto user_status = currentUserId(request, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    // 从 MySQL 查这个用户加入了哪些群
    std::vector<GroupRecord> groups;
    const auto groups_status = storage_.getGroupsForUser(user_id, groups);
    if (!groups_status.isOk()) {
        return groups_status;
    }

    response.header.msg_type = MessageType::ListGroupsResponse;
    response.header.seq_id = request.packet.header.seq_id;
    for (const auto& group : groups) {
        const auto append_status = appendGroupFields(group, response);
        if (!append_status.isOk()) {
            return append_status;
        }
    }
    return Status::ok();
}

// 处理发送群消息
Status GroupService::handleGroupMessage(const MessageRouter::RouterRequest& request,
                                        Packet& response) {
    // 从 session 里拿到当前用户 id，确认登录状态
    std::uint64_t sender_id = 0;
    const auto user_status = currentUserId(request, sender_id);
    if (!user_status.isOk()) {
        return user_status;
    }
    // 从请求消息里拿到群 id 和消息文本
    std::uint64_t group_id = 0;
    const auto group_status = getUint64(request.fields, TlvType::GroupId, group_id);
    if (!group_status.isOk()) {
        return group_status;
    }
    if (group_id == 0U) {
        return invalidGroupIdStatus();
    }

    std::string message_text;
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

    // 确认群存在
    GroupRecord group;
    const auto find_status = storage_.findGroupById(group_id, group);
    if (!find_status.isOk()) {
        return find_status;
    }

    // 检查发送者是不是群成员
    std::vector<GroupMemberRecord> members;
    const auto members_status = storage_.getGroupMembers(group_id, members);
    if (!members_status.isOk()) {
        return members_status;
    }
    if (!containsMember(members, sender_id)) {
        return notGroupMemberStatus();
    }

    // 区分在线成员和离线成员，在线成员直接推送消息，离线成员把消息存到离线消息表里，并在缓存里增加未读计数
    std::vector<Session::Ptr> online_sessions;
    std::vector<std::uint64_t> offline_user_ids;
    bool online_bot_member = false;
    // 跳过发送者自己
    for (const auto& member : members) {
        if (member.user_id == sender_id) {
            continue;
        }

        Session::Ptr member_session;
        const auto session_status =
            online_service_.getSessionByUser(member.user_id, member_session);
        // 在线成员，指针放进 online_sessions，后面保存消息后直接推送
        if (session_status.isOk()) {
            if (bot_service_ != nullptr && bot_service_->isBotUser(member.user_id)) {
                online_bot_member = true;
            }
            online_sessions.push_back(std::move(member_session));
            continue;
        }
        if (bot_service_ != nullptr && bot_service_->isBotUser(member.user_id) &&
            session_status.code() == ErrorCode::NotFound) {
            continue;
        }
        // 离线成员，user_id 放进 offline_user_ids，后面存离线消息和增加未读计数
        if (session_status.code() == ErrorCode::NotFound) {
            offline_user_ids.push_back(member.user_id);
            continue;
        }
        return session_status;
    }
    // 构造群消息
    MessageRecord message;
    message.conversation = {ConversationType::kGroup, group.group_id};
    message.sender_id = sender_id;
    message.receiver_id = group.group_id;
    message.text = std::move(message_text);

    // 把消息存到 MySQL，存的时候根据离线成员列表把消息也存到离线消息表里，返回保存后的消息记录（包含 message_id 和 created_at_ms）
    MessageRecord saved_message;
    const auto save_status =
        storage_.saveMessageWithOfflineRecipients(message, offline_user_ids, saved_message);
    if (!save_status.isOk()) {
        return save_status;
    }

    // 给离线成员加 Redis 未读数
    for (const auto& user_id : offline_user_ids) {
        std::uint64_t unread_count = 0;
        const auto unread_status =
            cache_.incrUnread(UnreadKey{user_id, saved_message.conversation}, 1, unread_count);
        if (!unread_status.isOk()) {
            Logger::get()->warn(
                "Failed to increment group unread for user {} conversation {} after message {} "
                "was saved: {}",
                user_id, saved_message.conversation.id, saved_message.message_id,
                unread_status.message());
        }
    }
    // 给在线成员推消息
    for (const auto& session : online_sessions) {
        Packet push;
        push.header.msg_type = MessageType::GroupMessagePush;
        const auto append_status = appendMessageFields(saved_message, push);
        if (!append_status.isOk()) {
            return append_status;
        }
        const auto send_status = session->sendPacket(push);
        if (!send_status.isOk()) {
            return send_status;
        }
    }

    if (!online_bot_member && bot_service_ != nullptr &&
        bot_service_->shouldHandleGroupMention(saved_message, members)) {
        const auto bot_status = bot_service_->handleGroupMention(saved_message, members);
        if (!bot_status.isOk()) {
            return bot_status;
        }
    }

    response.header.msg_type = MessageType::GroupMessageResponse;
    response.header.seq_id = request.packet.header.seq_id;
    // 给发送者返回响应
    return appendMessageFields(saved_message, response);
}

Status GroupService::currentUserId(const MessageRouter::RouterRequest& request,
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

Status GroupService::appendGroupFields(const GroupRecord& group, Packet& packet) {
    const auto id_status = appendUint64(TlvType::GroupId, group.group_id, packet.body);
    if (!id_status.isOk()) {
        return id_status;
    }
    return appendString(TlvType::GroupName, group.group_name, packet.body);
}

}  // namespace liteim
