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

// 注册和登录业务的接口
namespace liteim {

// 登录限制配置
struct AuthServiceOptions {
    std::uint32_t max_login_failures{3};
    std::chrono::seconds login_failure_ttl{std::chrono::minutes{5}};  // 登录失败记录保留 5 分钟
    std::string default_remote_ip{"unknown"};  // Session 没有 peer IP 时的登录限流兜底值
};

class AuthService {
public:
    AuthService(IStorage& storage, ICache& cache, OnlineService& online_service,
                AuthServiceOptions options = AuthServiceOptions{});
    // 把注册和登录 handler 注册到 MessageRouter,丢到业务线程池
    Status registerHandlers(MessageRouter& router);
    // 处理注册请求
    Status handleRegister(const MessageRouter::RouterRequest& request, Packet& response);
    // 处理登录请求
    Status handleLogin(const MessageRouter::RouterRequest& request, Packet& response);
    // 返回当前配置
    const AuthServiceOptions& options() const noexcept;

private:
    IStorage& storage_;
    ICache& cache_;
    OnlineService& online_service_;
    AuthServiceOptions options_;
};

}  // namespace liteim

/*
 ## 注册时：生成随机salt + 计算 hash
  输入：明文密码 + salt
  算法：PBKDF2-HMAC-SHA256
  迭代：10000 次
  输出：32 字节二进制 hash

  最后再把 32 字节 hash 转成 hex 字符串保存。32 字节 hash 转 hex 后通常是 64 个
  字符。

  所以数据库里保存的是：

  username       = alice
  password_salt = 随机 salt 的 hex 字符串
  password_hash = PBKDF2(password, salt) 后的 hex 字符串

登录时：取出数据库里的 password_salt，用本次输入的 password + 数据库里的 salt 重新算 hash再比较
*/
