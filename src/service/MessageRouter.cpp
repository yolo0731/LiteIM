#include "liteim/service/MessageRouter.hpp"

#include "liteim/protocol/MessageType.hpp"

#include <string>
#include <utility>

namespace liteim::service {
namespace {

protocol::Packet makeResponse(
    const protocol::Packet& request,
    protocol::MsgType response_type,
    std::string body) {
    protocol::Packet response;
    response.header.msg_type = protocol::toUint16(response_type);
    response.header.seq_id = request.header.seq_id;
    response.body = std::move(body);
    return response;
}

}  // namespace

void MessageRouter::route(net::Session& session, const protocol::Packet& packet) const {
    switch (packet.header.msg_type) {
    case protocol::toUint16(protocol::MsgType::HEARTBEAT_REQ):
        session.sendPacket(makeHeartbeatResponse(packet));
        break;
    default:
        session.sendPacket(makeErrorResponse(packet));
        break;
    }
}

protocol::Packet MessageRouter::makeHeartbeatResponse(const protocol::Packet& request) const {
    return makeResponse(request, protocol::MsgType::HEARTBEAT_RESP, "");
}

protocol::Packet MessageRouter::makeErrorResponse(const protocol::Packet& request) const {
    return makeResponse(request, protocol::MsgType::ERROR_RESP, "unknown message type");
}

}  // namespace liteim::service
