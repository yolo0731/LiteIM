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

// 组合SessionManager和ICache，提供用户在线状态管理的服务
class OnlineService {
public:
    OnlineService(SessionManager& sessions, ICache& cache, std::string server_id,
                  std::chrono::seconds online_ttl);
    // 登录成功后，把用户绑定到这个 session，并写入在线缓存
    Status bindUser(std::uint64_t user_id, const Session::Ptr& session);
    // 用户登出或连接断开时，解除绑定并删除在线缓存
    Status unbindUser(std::uint64_t user_id, std::uint64_t session_id);
    // 连接关闭时只知道 session_id，先从绑定表查 user_id，再走防旧 session 误删的 unbindUser
    Status unbindSession(std::uint64_t session_id);
    // 心跳或活跃时刷新 Redis 在线状态的过期时间
    Status refreshUserOnline(std::uint64_t user_id, std::uint64_t session_id);

    Status getSessionByUser(std::uint64_t user_id, Session::Ptr& session);
    Status getUserBySession(std::uint64_t session_id, std::uint64_t& user_id);

    const std::string& serverId() const noexcept;
    std::chrono::seconds onlineTtl() const noexcept;

private:
    // 检查绑定前的数据是否合法
    Status validateSession(std::uint64_t user_id, const Session::Ptr& session) const;

    SessionManager& sessions_;  // 绑定表
    ICache& cache_;             // 在线状态等数据的存储，通常是 RedisCache
    std::string server_id_;
    std::chrono::seconds online_ttl_;
};

}  // namespace liteim
