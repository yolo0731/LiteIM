#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/service/MessageRouter.hpp"
#include "liteim/service/OnlineService.hpp"

namespace liteim {

// 业务层心跳响应服务，负责处理客户端的心跳请求，刷新在线状态的过期时间
class HeartbeatService {
public:
    explicit HeartbeatService(OnlineService& online_service);

    Status registerHandlers(MessageRouter& router);
    Status handleHeartbeat(const MessageRouter::RouterRequest& request, Packet& response);

private:
    OnlineService& online_service_;
};

}  // namespace liteim
