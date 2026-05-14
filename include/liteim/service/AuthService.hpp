#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/ICache.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/service/MessageRouter.hpp"
#include "liteim/service/OnlineService.hpp"
#include "liteim/storage/IStorage.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace liteim {

struct AuthServiceOptions {
    std::uint32_t max_login_failures{3};
    std::chrono::seconds login_failure_ttl{std::chrono::minutes{5}};
    std::string default_remote_ip{"unknown"};
};

class AuthService {
public:
    AuthService(IStorage& storage, ICache& cache, OnlineService& online_service,
                AuthServiceOptions options = AuthServiceOptions{});

    Status registerHandlers(MessageRouter& router);
    Status handleRegister(const MessageRouter::RouterRequest& request, Packet& response);
    Status handleLogin(const MessageRouter::RouterRequest& request, Packet& response);

    const AuthServiceOptions& options() const noexcept;

private:
    IStorage& storage_;
    ICache& cache_;
    OnlineService& online_service_;
    AuthServiceOptions options_;
};

}  // namespace liteim
