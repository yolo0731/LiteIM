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

class FriendService {
public:
    FriendService(IStorage& storage, ICache& cache, OnlineService& online_service);

    // 注册好友相关handler
    Status registerHandlers(MessageRouter& router);
    // 处理添加好友，返回新好友信息
    Status handleAddFriend(const MessageRouter::RouterRequest& request, Packet& response);
    // 处理列出好友,返回好友列表和在线状态
    Status handleListFriends(const MessageRouter::RouterRequest& request, Packet& response);

private:
    Status currentUserId(const MessageRouter::RouterRequest& request, std::uint64_t& user_id);
    // 添加好友信息到响应Packet的body中，包含在线状态
    Status appendFriendFields(const UserProfileRecord& friend_profile, Packet& response);

    IStorage& storage_;
    ICache& cache_;
    OnlineService& online_service_;
};

}  // namespace liteim
