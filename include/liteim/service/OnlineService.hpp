#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/ICache.hpp"
#include "liteim/net/Session.hpp"
#include "liteim/service/SessionManager.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace liteim {

class OnlineService {
public:
    OnlineService(SessionManager& sessions, ICache& cache, std::string server_id,
                  std::chrono::seconds online_ttl);

    Status bindUser(std::uint64_t user_id, const Session::Ptr& session);
    Status unbindUser(std::uint64_t user_id, std::uint64_t session_id);
    Status refreshUserOnline(std::uint64_t user_id, std::uint64_t session_id);

    Status getSessionByUser(std::uint64_t user_id, Session::Ptr& session);
    Status getUserBySession(std::uint64_t session_id, std::uint64_t& user_id);

    const std::string& serverId() const noexcept;
    std::chrono::seconds onlineTtl() const noexcept;

private:
    Status validateSession(std::uint64_t user_id, const Session::Ptr& session) const;

    SessionManager& sessions_;
    ICache& cache_;
    std::string server_id_;
    std::chrono::seconds online_ttl_;
};

}  // namespace liteim
