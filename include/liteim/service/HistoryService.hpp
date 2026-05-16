#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/service/MessageRouter.hpp"
#include "liteim/service/OnlineService.hpp"
#include "liteim/storage/IStorage.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <cstdint>
#include <vector>

namespace liteim {

struct HistoryServiceOptions {
    std::uint32_t default_limit{20};
    std::uint32_t max_limit{50};
};

class HistoryService {
public:
    HistoryService(IStorage& storage, OnlineService& online_service,
                   HistoryServiceOptions options = HistoryServiceOptions{});

    Status registerHandlers(MessageRouter& router);
    Status handleHistory(const MessageRouter::RouterRequest& request, Packet& response);
    const HistoryServiceOptions& options() const noexcept;

private:
    Status currentUserId(const MessageRouter::RouterRequest& request, std::uint64_t& user_id);
    Status buildQuery(const MessageRouter::RouterRequest& request, HistoryQuery& query) const;
    Status authorizeQuery(std::uint64_t user_id, const HistoryQuery& query);
    Status appendMessages(const std::vector<MessageRecord>& messages, Packet& response);

    IStorage& storage_;
    OnlineService& online_service_;
    HistoryServiceOptions options_;
};

}  // namespace liteim
