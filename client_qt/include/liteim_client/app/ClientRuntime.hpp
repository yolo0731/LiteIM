#pragma once

#include "liteim_client/network/ClientSession.hpp"
#include "liteim_client/network/TcpClient.hpp"

#include <QObject>

namespace liteim::client {

class ClientRuntime final : public QObject {
    Q_OBJECT

public:
    explicit ClientRuntime(QObject* parent = nullptr);

    TcpClient& client() noexcept;
    const TcpClient& client() const noexcept;
    ClientSession& session() noexcept;
    const ClientSession& session() const noexcept;

private:
    TcpClient client_;
    ClientSession session_;
};

}  // namespace liteim::client
