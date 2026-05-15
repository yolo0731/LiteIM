#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/ICache.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/service/MessageRouter.hpp"
#include "liteim/service/OnlineService.hpp"
#include "liteim/storage/IStorage.hpp"

#include <cstdint>
#include <vector>

namespace liteim {

struct OfflineMessageServiceOptions {
    std::uint32_t max_messages_per_pull{100};
};

class OfflineMessageService {
public:
    OfflineMessageService(IStorage& storage, ICache& cache, OnlineService& online_service,
                          OfflineMessageServiceOptions options = OfflineMessageServiceOptions{});

    Status registerHandlers(MessageRouter& router);
    Status handleOfflineMessages(const MessageRouter::RouterRequest& request, Packet& response);
    const OfflineMessageServiceOptions& options() const noexcept;

private:
    Status currentUserId(const MessageRouter::RouterRequest& request, std::uint64_t& user_id);
    Status requestLimit(const MessageRouter::RouterRequest& request, std::uint32_t& limit) const;
    Status appendMessages(const std::vector<OfflineMessageRecord>& messages, Packet& response);
    Status clearUnreadForMessages(std::uint64_t user_id,
                                  const std::vector<OfflineMessageRecord>& messages);

    IStorage& storage_;
    ICache& cache_;
    OnlineService& online_service_;
    OfflineMessageServiceOptions options_;
};

}  // namespace liteim
