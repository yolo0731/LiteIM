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
    // 从 TLV 请求字段里组装 HistoryQuery
    Status buildQuery(const MessageRouter::RouterRequest& request, HistoryQuery& query) const;
    // 校验当前用户有没有权限看这个会话
    Status authorizeQuery(std::uint64_t user_id, const HistoryQuery& query);
    // 把查到的消息写进响应包
    Status appendMessages(const std::vector<MessageRecord>& messages, Packet& response);

    IStorage& storage_;
    OnlineService& online_service_;  // 用于查询用户在线状态和会话成员关系
    HistoryServiceOptions options_;  // 历史消息的默认和最大查询条数限制
};

}  // namespace liteim
