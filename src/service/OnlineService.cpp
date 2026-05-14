#include "liteim/service/OnlineService.hpp"

#include <stdexcept>
#include <utility>

namespace liteim {

OnlineService::OnlineService(SessionManager& sessions, ICache& cache, std::string server_id,
                             std::chrono::seconds online_ttl)
    : sessions_(sessions), cache_(cache), server_id_(std::move(server_id)),
      online_ttl_(online_ttl) {
    if (server_id_.empty()) {
        throw std::invalid_argument("server id must not be empty");
    }
    if (online_ttl_.count() <= 0) {
        throw std::invalid_argument("online ttl must be positive");
    }
}

Status OnlineService::bindUser(std::uint64_t user_id, const Session::Ptr& session) {
    const auto validation_status = validateSession(user_id, session);
    if (!validation_status.isOk()) {
        return validation_status;
    }
    // 一个连接只能绑定一个用户
    std::uint64_t already_bound_user = 0;
    const auto bound_status = sessions_.getUserBySession(session->id(), already_bound_user);
    if (bound_status.isOk() && already_bound_user != user_id) {
        return Status::error(ErrorCode::AlreadyExists, "session is already bound to another user");
    }
    if (!bound_status.isOk() && bound_status.code() != ErrorCode::NotFound) {
        return bound_status;
    }

    // 构建在线状态信息，写入 Redis
    OnlineSession online_session;
    online_session.user_id = user_id;
    online_session.session_id = session->id();
    online_session.server_id = server_id_;
    online_session.last_active_time_ms = session->lastActiveTimeMilliseconds();

    // 新登录会覆盖旧 Redis 状态,因为一个key只能对应一个value
    const auto cache_status = cache_.setUserOnline(online_session, online_ttl_);
    if (!cache_status.isOk()) {
        return cache_status;
    }
    // 再写内存绑定表
    const auto bind_status = sessions_.bindUser(user_id, session);
    if (!bind_status.isOk()) {
        (void)cache_.setUserOffline(user_id);
        return bind_status;
    }
    return Status::ok();
}

Status OnlineService::unbindUser(std::uint64_t user_id, std::uint64_t session_id) {
    bool removed = false;
    const auto unbind_status = sessions_.unbindUser(user_id, session_id, removed);
    if (!unbind_status.isOk()) {
        return unbind_status;
    }
    if (!removed) {
        return Status::ok();
    }
    // 只有确实解绑了才去更新 Redis 在线状态
    return cache_.setUserOffline(user_id);
}

Status OnlineService::refreshUserOnline(std::uint64_t user_id, std::uint64_t session_id) {
    std::uint64_t bound_user_id = 0;
    const auto user_status = sessions_.getUserBySession(session_id, bound_user_id);
    if (!user_status.isOk()) {
        return user_status;
    }
    if (bound_user_id != user_id) {
        return Status::error(ErrorCode::NotFound, "session is not bound to this user");
    }
    // 刷新 Redis 在线状态的过期时间
    return cache_.refreshUserOnline(user_id, online_ttl_);
}

// 不涉及redis操作，直接从内存绑定表查询
Status OnlineService::getSessionByUser(std::uint64_t user_id, Session::Ptr& session) {
    return sessions_.getSessionByUser(user_id, session);
}

Status OnlineService::getUserBySession(std::uint64_t session_id, std::uint64_t& user_id) {
    return sessions_.getUserBySession(session_id, user_id);
}

const std::string& OnlineService::serverId() const noexcept {
    return server_id_;
}

std::chrono::seconds OnlineService::onlineTtl() const noexcept {
    return online_ttl_;
}

Status OnlineService::validateSession(std::uint64_t user_id, const Session::Ptr& session) const {
    if (user_id == 0) {
        return Status::error(ErrorCode::InvalidArgument, "user id must be positive");
    }
    if (session == nullptr) {
        return Status::error(ErrorCode::InvalidArgument, "session must not be null");
    }
    if (session->id() == 0) {
        return Status::error(ErrorCode::InvalidArgument, "session id must be positive");
    }
    if (session->closed()) {
        return Status::error(ErrorCode::InvalidArgument, "session is closed");
    }
    return Status::ok();
}

}  // namespace liteim
