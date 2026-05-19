#pragma once

#include "liteim/protocol/MessageType.hpp"

#include <QDateTime>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace liteim::client {

/*ClientSession 是 Qt 客户端里的 会话状态管理器，负责生成请求 seq_id、记录“哪些请求还在等待响应”、保存登录后的 user_id / token / session_id*/

// 记录客户端发出去的请求
struct PendingRequest {
    MessageType request_type{MessageType::Unknown};
    QDateTime created_at;
};

class ClientSession {
public:
    std::uint64_t nextSeqId();
    std::uint64_t trackRequest(MessageType request_type);

    bool hasPending(std::uint64_t seq_id) const;
    std::optional<PendingRequest> pendingRequest(std::uint64_t seq_id) const;
    std::optional<PendingRequest> takePending(std::uint64_t seq_id);
    std::size_t pendingCount() const noexcept;

    void markLoggedIn(std::uint64_t user_id, QString token, QString session_id);
    void clearLogin();
    void reset();

    bool isLoggedIn() const noexcept;
    std::uint64_t userId() const noexcept;
    const QString& token() const noexcept;
    const QString& sessionId() const noexcept;

private:
    // 客户端发送的请求跟踪
    std::uint64_t next_seq_id_{1};
    std::unordered_map<std::uint64_t, PendingRequest> pending_;

    // 登录状态
    bool logged_in_{false};
    std::uint64_t user_id_{0};
    QString token_;
    QString session_id_;
};

}  // namespace liteim::client
