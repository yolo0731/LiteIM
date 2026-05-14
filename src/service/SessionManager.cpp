#include "liteim/service/SessionManager.hpp"

#include <utility>

namespace liteim {

Status SessionManager::bindUser(std::uint64_t user_id, const Session::Ptr& session) {
    if (user_id == 0) {
        return Status::error(ErrorCode::InvalidArgument, "user id must be positive");
    }
    if (session == nullptr) {
        return Status::error(ErrorCode::InvalidArgument, "session must not be null");
    }
    const auto session_id = session->id();
    if (session_id == 0) {
        return Status::error(ErrorCode::InvalidArgument, "session id must be positive");
    }
    if (session->closed()) {
        return Status::error(ErrorCode::InvalidArgument, "session is closed");
    }

    Session::Ptr old_session;
    {
        // 因为users_和sessions_可能会被多个线程访问，所以需要加锁保护
        std::lock_guard<std::mutex> lock(mutex_);

        const auto session_it = sessions_.find(session_id);
        if (session_it != sessions_.end() && session_it->second != user_id) {
            return Status::error(ErrorCode::AlreadyExists,
                                 "session is already bound to another user");
        }

        const auto user_it = users_.find(user_id);
        if (user_it != users_.end()) {
            if (user_it->second.session_id == session_id) {
                user_it->second.session = session;
                sessions_[session_id] = user_id;
                return Status::ok();
            }
            //从旧绑定里拿出旧的 Session 对象，如果它还活着，就临时转成 shared_ptr 保存到 old_session,这里是weak_ptr<Session>.lock()转化为shared_ptr<Session>
            old_session = user_it->second.session.lock();
            sessions_.erase(user_it->second.session_id);
        }

        users_[user_id] = UserBinding{session_id, session};
        sessions_[session_id] = user_id;
    }
    // 如果重复登录，关闭旧的连接。
    if (old_session != nullptr && old_session != session && !old_session->closed()) {
        old_session->close();
    }
    return Status::ok();
}

Status SessionManager::unbindUser(std::uint64_t user_id, std::uint64_t session_id) {
    bool removed = false;
    return unbindUser(user_id, session_id, removed);
}

Status SessionManager::unbindUser(std::uint64_t user_id, std::uint64_t session_id, bool& removed) {
    removed = false;
    if (user_id == 0) {
        return Status::error(ErrorCode::InvalidArgument, "user id must be positive");
    }
    if (session_id == 0) {
        return Status::error(ErrorCode::InvalidArgument, "session id must be positive");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto user_it = users_.find(user_id);
    if (user_it == users_.end() || user_it->second.session_id != session_id) {
        return Status::ok();
    }

    eraseBindingLocked(user_id, session_id);
    removed = true;
    return Status::ok();
}

Status SessionManager::getSessionByUser(std::uint64_t user_id, Session::Ptr& session) {
    session.reset();
    if (user_id == 0) {
        return Status::error(ErrorCode::InvalidArgument, "user id must be positive");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto user_it = users_.find(user_id);
    if (user_it == users_.end()) {
        return Status::error(ErrorCode::NotFound, "user is not bound to a session");
    }

    auto locked_session = user_it->second.session.lock();
    if (locked_session == nullptr || locked_session->closed()) {
        eraseBindingLocked(user_id, user_it->second.session_id);
        return Status::error(ErrorCode::NotFound, "user session is no longer alive");
    }

    session = std::move(locked_session);
    return Status::ok();
}

Status SessionManager::getUserBySession(std::uint64_t session_id, std::uint64_t& user_id) {
    user_id = 0;
    if (session_id == 0) {
        return Status::error(ErrorCode::InvalidArgument, "session id must be positive");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
        return Status::error(ErrorCode::NotFound, "session is not bound to a user");
    }

    const auto bound_user_id = session_it->second;
    const auto user_it = users_.find(bound_user_id);
    if (user_it == users_.end() || user_it->second.session_id != session_id) {
        sessions_.erase(session_it);
        return Status::error(ErrorCode::NotFound, "session binding is stale");
    }

    auto locked_session = user_it->second.session.lock();
    if (locked_session == nullptr || locked_session->closed()) {
        eraseBindingLocked(bound_user_id, session_id);
        return Status::error(ErrorCode::NotFound, "session is no longer alive");
    }

    user_id = bound_user_id;
    return Status::ok();
}

std::size_t SessionManager::userCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.size();
}

std::size_t SessionManager::sessionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
}

void SessionManager::eraseBindingLocked(std::uint64_t user_id, std::uint64_t session_id) {
    users_.erase(user_id);
    sessions_.erase(session_id);
}

}  // namespace liteim
