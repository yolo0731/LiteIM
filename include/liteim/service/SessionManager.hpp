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
    // 登录成功后，把这个用户和当前连接绑定起来
    Status bindUser(std::uint64_t user_id, const Session::Ptr& session);
    Status unbindUser(std::uint64_t user_id, std::uint64_t session_id);
    // 解绑用户和连接的绑定关系，只有当提供的session_id和当前绑定的一致时才会解绑，避免重复登录时把新连接解绑了
    Status unbindUser(std::uint64_t user_id, std::uint64_t session_id, bool& removed);
    Status getSessionByUser(std::uint64_t user_id, Session::Ptr& session);
    Status getUserBySession(std::uint64_t session_id, std::uint64_t& user_id);

    std::size_t userCount() const;
    std::size_t sessionCount() const;

private:
    struct UserBinding {
        std::uint64_t session_id{0};
        std::weak_ptr<Session> session;  // SessionManager不拥有session，使用弱指针避免循环引用
    };
    // 删除 users_ 和 sessions_ 中对应的一组绑定；调用前必须已经持有 mutex_。
    void eraseBindingLocked(std::uint64_t user_id, std::uint64_t session_id);

    mutable std::mutex mutex_;
    // 维护两张映射表，保证用户和会话的绑定关系的一致性
    std::unordered_map<std::uint64_t, UserBinding> users_;
    std::unordered_map<std::uint64_t, std::uint64_t> sessions_;
};

}  // namespace liteim
