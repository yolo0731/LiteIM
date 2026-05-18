#include "liteim_client/ClientSession.hpp"

#include <utility>

namespace liteim::client {

std::uint64_t ClientSession::nextSeqId() {
    return next_seq_id_++;
}

std::uint64_t ClientSession::trackRequest(MessageType request_type) {
    const auto seq_id = nextSeqId();
    pending_.emplace(seq_id, PendingRequest{request_type, QDateTime::currentDateTimeUtc()});
    return seq_id;
}

bool ClientSession::hasPending(std::uint64_t seq_id) const {
    return pending_.find(seq_id) != pending_.end();
}

std::optional<PendingRequest> ClientSession::takePending(std::uint64_t seq_id) {
    const auto it = pending_.find(seq_id);
    if (it == pending_.end()) {
        return std::nullopt;
    }

    auto pending = std::move(it->second);
    pending_.erase(it);
    return pending;
}

std::size_t ClientSession::pendingCount() const noexcept {
    return pending_.size();
}

void ClientSession::markLoggedIn(std::uint64_t user_id, QString token, QString session_id) {
    logged_in_ = true;
    user_id_ = user_id;
    token_ = std::move(token);
    session_id_ = std::move(session_id);
}

void ClientSession::clearLogin() {
    logged_in_ = false;
    user_id_ = 0;
    token_.clear();
    session_id_.clear();
}

void ClientSession::reset() {
    next_seq_id_ = 1;
    pending_.clear();
    clearLogin();
}

bool ClientSession::isLoggedIn() const noexcept {
    return logged_in_;
}

std::uint64_t ClientSession::userId() const noexcept {
    return user_id_;
}

const QString& ClientSession::token() const noexcept {
    return token_;
}

const QString& ClientSession::sessionId() const noexcept {
    return session_id_;
}

}  // namespace liteim::client
