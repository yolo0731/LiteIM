#include "liteim/service/FriendService.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/base/Logger.hpp"
#include "liteim/protocol/TlvCodec.hpp"

#include <utility>
#include <vector>

namespace liteim {
namespace {

Status notLoggedInStatus() {
    return Status::error(ErrorCode::InvalidArgument, "session is not logged in");
}

Status invalidTargetUserStatus() {
    return Status::error(ErrorCode::InvalidArgument, "target user id must be positive");
}

UserProfileRecord toProfile(const UserRecord& user) {
    return UserProfileRecord{user.user_id, user.username, user.nickname, user.created_at_ms};
}

}  // namespace

FriendService::FriendService(IStorage& storage, ICache& cache, OnlineService& online_service)
    : storage_(storage), cache_(cache), online_service_(online_service) {}

Status FriendService::registerHandlers(MessageRouter& router) {
    const auto add_status = router.registerHandler(
        MessageType::AddFriendRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleAddFriend(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
    if (!add_status.isOk()) {
        return add_status;
    }

    const auto accept_status = router.registerHandler(
        MessageType::AcceptFriendRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleAcceptFriend(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
    if (!accept_status.isOk()) {
        return accept_status;
    }

    const auto reject_status = router.registerHandler(
        MessageType::RejectFriendRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleRejectFriend(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
    if (!reject_status.isOk()) {
        return reject_status;
    }

    return router.registerHandler(
        MessageType::ListFriendsRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleListFriends(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
}

Status FriendService::handleAddFriend(const MessageRouter::RouterRequest& request,
                                      Packet& response) {
    std::uint64_t user_id = 0;
    const auto user_status = currentUserId(request, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    std::uint64_t target_user_id = 0;
    const auto target_status = getUint64(request.fields, TlvType::TargetUserId, target_user_id);
    if (!target_status.isOk()) {
        return target_status;
    }
    if (target_user_id == 0 || target_user_id == user_id) {
        return invalidTargetUserStatus();
    }

    UserRecord target_user;
    const auto find_status = storage_.findUserById(target_user_id, target_user);
    if (!find_status.isOk()) {
        return find_status;
    }

    FriendRequestRecord friend_request;
    const auto request_status =
        storage_.createFriendRequest(user_id, target_user_id, friend_request);
    if (!request_status.isOk()) {
        return request_status;
    }

    response.header.msg_type = MessageType::AddFriendResponse;
    response.header.seq_id = request.packet.header.seq_id;
    const auto append_status = appendFriendFields(toProfile(target_user), response);
    if (!append_status.isOk()) {
        return append_status;
    }
    return appendFriendRequestStatus(friend_request.status, response);
}

Status FriendService::handleAcceptFriend(const MessageRouter::RouterRequest& request,
                                         Packet& response) {
    std::uint64_t user_id = 0;
    const auto user_status = currentUserId(request, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    std::uint64_t requester_id = 0;
    const auto requester_status = getUint64(request.fields, TlvType::TargetUserId, requester_id);
    if (!requester_status.isOk()) {
        return requester_status;
    }
    if (requester_id == 0 || requester_id == user_id) {
        return invalidTargetUserStatus();
    }

    UserRecord requester;
    const auto find_status = storage_.findUserById(requester_id, requester);
    if (!find_status.isOk()) {
        return find_status;
    }

    const auto accept_status = storage_.acceptFriendRequest(requester_id, user_id);
    if (!accept_status.isOk()) {
        return accept_status;
    }

    response.header.msg_type = MessageType::AcceptFriendResponse;
    response.header.seq_id = request.packet.header.seq_id;
    const auto append_status = appendFriendFields(toProfile(requester), response);
    if (!append_status.isOk()) {
        return append_status;
    }
    return appendFriendRequestStatus(FriendRequestStatus::kAccepted, response);
}

Status FriendService::handleRejectFriend(const MessageRouter::RouterRequest& request,
                                         Packet& response) {
    std::uint64_t user_id = 0;
    const auto user_status = currentUserId(request, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    std::uint64_t requester_id = 0;
    const auto requester_status = getUint64(request.fields, TlvType::TargetUserId, requester_id);
    if (!requester_status.isOk()) {
        return requester_status;
    }
    if (requester_id == 0 || requester_id == user_id) {
        return invalidTargetUserStatus();
    }

    UserRecord requester;
    const auto find_status = storage_.findUserById(requester_id, requester);
    if (!find_status.isOk()) {
        return find_status;
    }

    const auto reject_status = storage_.rejectFriendRequest(requester_id, user_id);
    if (!reject_status.isOk()) {
        return reject_status;
    }

    response.header.msg_type = MessageType::RejectFriendResponse;
    response.header.seq_id = request.packet.header.seq_id;
    const auto append_status = appendFriendFields(toProfile(requester), response);
    if (!append_status.isOk()) {
        return append_status;
    }
    return appendFriendRequestStatus(FriendRequestStatus::kRejected, response);
}

Status FriendService::handleListFriends(const MessageRouter::RouterRequest& request,
                                        Packet& response) {
    std::uint64_t user_id = 0;
    const auto user_status = currentUserId(request, user_id);
    if (!user_status.isOk()) {
        return user_status;
    }

    std::vector<UserProfileRecord> friends;
    const auto friends_status = storage_.getFriends(user_id, friends);
    if (!friends_status.isOk()) {
        return friends_status;
    }

    response.header.msg_type = MessageType::ListFriendsResponse;
    response.header.seq_id = request.packet.header.seq_id;
    for (const auto& friend_profile : friends) {
        const auto append_status = appendFriendFields(friend_profile, response);
        if (!append_status.isOk()) {
            return append_status;
        }
    }
    return Status::ok();
}

Status FriendService::currentUserId(const MessageRouter::RouterRequest& request,
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

Status FriendService::appendFriendFields(const UserProfileRecord& friend_profile,
                                         Packet& response) {
    const auto id_status = appendUint64(TlvType::FriendId, friend_profile.user_id, response.body);
    if (!id_status.isOk()) {
        return id_status;
    }
    const auto username_status =
        appendString(TlvType::Username, friend_profile.username, response.body);
    if (!username_status.isOk()) {
        return username_status;
    }
    const auto nickname_status =
        appendString(TlvType::Nickname, friend_profile.nickname, response.body);
    if (!nickname_status.isOk()) {
        return nickname_status;
    }

    bool online = false;
    const auto online_status = cache_.isUserOnline(friend_profile.user_id, online);
    if (!online_status.isOk()) {
        Logger::get()->warn("Failed to query online status for friend {}: {}",
                            friend_profile.user_id, online_status.message());
        online = false;
    }
    return appendUint64(TlvType::OnlineStatus, online ? 1U : 0U, response.body);
}

Status FriendService::appendFriendRequestStatus(FriendRequestStatus status, Packet& response) {
    return appendUint64(TlvType::FriendRequestStatus, static_cast<std::uint64_t>(status),
                        response.body);
}

}  // namespace liteim
