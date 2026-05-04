#pragma once

#include "liteim/net/Session.hpp"
#include "liteim/protocol/Packet.hpp"

namespace liteim::service {

class MessageRouter {
public:
    void route(net::Session& session, const protocol::Packet& packet) const;

private:
    protocol::Packet makeHeartbeatResponse(const protocol::Packet& request) const;
    protocol::Packet makeErrorResponse(const protocol::Packet& request) const;
};

}  // namespace liteim::service
