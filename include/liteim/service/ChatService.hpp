#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/ICache.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/service/MessageRouter.hpp"
#include "liteim/service/OnlineService.hpp"
#include "liteim/storage/IStorage.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <cstdint>

namespace liteim {

class BotService;

class ChatService {
public:
    ChatService(IStorage& storage, ICache& cache, OnlineService& online_service,
                BotService* bot_service = nullptr);
    // 注册私聊消息到 MessageRouter
    Status registerHandlers(MessageRouter& router);
    // 真正处理私聊消息
    Status handlePrivateMessage(const MessageRouter::RouterRequest& request, Packet& response);

private:
    // 从 session 获取当前登录的 user_id
    Status currentUserId(const MessageRouter::RouterRequest& request, std::uint64_t& user_id);
    // 把消息字段写进响应包,push到接收客户端和对发送客户端response都会用到，因为客户端需要知道消息 ID 和时间戳等信息
    Status appendMessageFields(const MessageRecord& message, Packet& packet);

    // 三个依赖
    IStorage& storage_;
    ICache& cache_;
    OnlineService& online_service_;
    BotService* bot_service_{nullptr};
};

}  // namespace liteim
