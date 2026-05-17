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

class ChatService {
public:
    ChatService(IStorage& storage, ICache& cache, OnlineService& online_service);
    // 注册私聊消息到 MessageRouter
    Status registerHandlers(MessageRouter& router);
    // 真正处理私聊消息
    Status handlePrivateMessage(const MessageRouter::RouterRequest& request, Packet& response);

private:
    // 从 session 获取当前登录的 user_id
    Status currentUserId(const MessageRouter::RouterRequest& request, std::uint64_t& user_id);

    // 三个依赖
    IStorage& storage_;
    ICache& cache_;
    OnlineService& online_service_;
};

}  // namespace liteim
