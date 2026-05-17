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

class GroupService {
public:
    GroupService(IStorage& storage, ICache& cache, OnlineService& online_service);

    Status registerHandlers(MessageRouter& router);
    // 处理创建群请求,返回创建的群信息
    Status handleCreateGroup(const MessageRouter::RouterRequest& request, Packet& response);
    // 处理加入群请求,返回加入的群信息
    Status handleJoinGroup(const MessageRouter::RouterRequest& request, Packet& response);
    // 处理列出群请求,返回用户加入的所有群的信息
    Status handleListGroups(const MessageRouter::RouterRequest& request, Packet& response);
    // 处理发送群消息,返回保存后的消息信息
    Status handleGroupMessage(const MessageRouter::RouterRequest& request, Packet& response);

private:
    // 从 session 里拿到当前用户 id，确认登录状态
    Status currentUserId(const MessageRouter::RouterRequest& request, std::uint64_t& user_id);
    // 把群信息写到响应的body里返回给客户端
    Status appendGroupFields(const GroupRecord& group, Packet& packet);

    IStorage& storage_;
    ICache& cache_;
    OnlineService& online_service_;
};

}  // namespace liteim
