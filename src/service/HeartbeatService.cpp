#include "liteim/service/HeartbeatService.hpp"

#include "liteim/base/ErrorCode.hpp"

namespace liteim {
namespace {

Status invalidSessionStatus() {
    return Status::error(ErrorCode::InvalidArgument, "session is invalid");
}

void fillHeartbeatResponse(const MessageRouter::RouterRequest& request, Packet& response) {
    response.header.msg_type = MessageType::HeartbeatResponse;
    response.header.seq_id = request.packet.header.seq_id;
}

}  // namespace

HeartbeatService::HeartbeatService(OnlineService& online_service)
    : online_service_(online_service) {}

Status HeartbeatService::registerHandlers(MessageRouter& router) {
    return router.registerHandler(
        MessageType::HeartbeatRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleHeartbeat(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
}

Status HeartbeatService::handleHeartbeat(const MessageRouter::RouterRequest& request,
                                         Packet& response) {
    if (request.session == nullptr || request.session->closed()) {
        return invalidSessionStatus();
    }

    std::uint64_t user_id = 0;
    const auto user_status = online_service_.getUserBySession(request.session->id(), user_id);
    if (!user_status.isOk()) {
        if (user_status.code() == ErrorCode::NotFound) {
            fillHeartbeatResponse(request, response);
            return Status::ok();
        }
        return user_status;
    }

    const auto refresh_status = online_service_.refreshUserOnline(user_id, request.session->id());
    if (!refresh_status.isOk()) {
        return refresh_status;
    }

    fillHeartbeatResponse(request, response);
    return Status::ok();
}

}  // namespace liteim
