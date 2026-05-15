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
    Status handleCreateGroup(const MessageRouter::RouterRequest& request, Packet& response);
    Status handleJoinGroup(const MessageRouter::RouterRequest& request, Packet& response);
    Status handleListGroups(const MessageRouter::RouterRequest& request, Packet& response);
    Status handleGroupMessage(const MessageRouter::RouterRequest& request, Packet& response);

private:
    Status currentUserId(const MessageRouter::RouterRequest& request, std::uint64_t& user_id);
    Status appendGroupFields(const GroupRecord& group, Packet& packet);
    Status appendMessageFields(const MessageRecord& message, Packet& packet);

    IStorage& storage_;
    ICache& cache_;
    OnlineService& online_service_;
};

}  // namespace liteim
