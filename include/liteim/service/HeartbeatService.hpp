#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/service/MessageRouter.hpp"
#include "liteim/service/OnlineService.hpp"

#include <cstdint>

namespace liteim {

class HeartbeatService {
public:
    explicit HeartbeatService(OnlineService& online_service);

    Status registerHandlers(MessageRouter& router);
    Status handleHeartbeat(const MessageRouter::RouterRequest& request, Packet& response);

private:
    OnlineService& online_service_;
};

}  // namespace liteim
