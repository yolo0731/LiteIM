#include "liteim_client/app/ClientRuntime.hpp"

namespace liteim::client {

ClientRuntime::ClientRuntime(QObject* parent) : QObject(parent), client_(this) {}

TcpClient& ClientRuntime::client() noexcept {
    return client_;
}

const TcpClient& ClientRuntime::client() const noexcept {
    return client_;
}

ClientSession& ClientRuntime::session() noexcept {
    return session_;
}

const ClientSession& ClientRuntime::session() const noexcept {
    return session_;
}

}  // namespace liteim::client
