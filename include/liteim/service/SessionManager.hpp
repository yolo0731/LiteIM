#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/net/Session.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace liteim {

class SessionManager {
public:
    Status bindUser(std::uint64_t user_id, const Session::Ptr& session);
    Status unbindUser(std::uint64_t user_id, std::uint64_t session_id);
    Status unbindUser(std::uint64_t user_id, std::uint64_t session_id, bool& removed);
    Status getSessionByUser(std::uint64_t user_id, Session::Ptr& session);
    Status getUserBySession(std::uint64_t session_id, std::uint64_t& user_id);

    std::size_t userCount() const;
    std::size_t sessionCount() const;

private:
    struct UserBinding {
        std::uint64_t session_id{0};
        std::weak_ptr<Session> session;
    };

    void eraseBindingLocked(std::uint64_t user_id, std::uint64_t session_id);

    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, UserBinding> users_;
    std::unordered_map<std::uint64_t, std::uint64_t> sessions_;
};

}  // namespace liteim
