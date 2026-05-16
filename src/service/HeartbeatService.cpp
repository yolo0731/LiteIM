#include "liteim/service/HeartbeatService.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/base/Logger.hpp"

namespace liteim {

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
        return Status::error(ErrorCode::InvalidArgument, "session is not active");
    }

    response.header.msg_type = MessageType::HeartbeatResponse;
    response.header.seq_id = request.packet.header.seq_id;

    std::uint64_t user_id = 0;
    const auto user_status = online_service_.getUserBySession(request.session->id(), user_id);
    if (!user_status.isOk()) {
        if (user_status.code() == ErrorCode::NotFound) {
            // 未登录心跳的结果是body 为空但不报错
            return Status::ok();
        }
        return user_status;
    }

    // 已登录：刷新 Redis 里的在线状态 TTL
    const auto refresh_status = online_service_.refreshUserOnline(user_id, request.session->id());
    // 刷新失败了也返回心跳成功，但记录日志warning
    if (!refresh_status.isOk()) {
        Logger::get()->warn(
            "heartbeat redis ttl refresh failed, user_id={}, session_id={}, seq_id={}, error={}",
            user_id, request.session->id(), request.packet.header.seq_id, refresh_status.message());
    }

    return Status::ok();
}

}  // namespace liteim
