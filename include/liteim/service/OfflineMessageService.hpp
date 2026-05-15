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
    std::uint32_t max_messages_per_pull{100};  // 每次拉取的最大消息数量，超过这个数量的消息会被丢弃
};

// 负责把“待投递记录”变成客户端能消费的 OfflineMessagesResponse
class OfflineMessageService {
public:
    OfflineMessageService(IStorage& storage, ICache& cache, OnlineService& online_service,
                          OfflineMessageServiceOptions options = OfflineMessageServiceOptions{});
    Status registerHandlers(MessageRouter& router);
    Status handleOfflineMessages(const MessageRouter::RouterRequest& request, Packet& response);
    const OfflineMessageServiceOptions& options() const noexcept;

private:
    Status currentUserId(const MessageRouter::RouterRequest& request, std::uint64_t& user_id);
    // 解析请求中的 limit 参数，决定本次拉取的消息数量
    Status requestLimit(const MessageRouter::RouterRequest& request, std::uint32_t& limit) const;
    // 把消息列表写成 TLV response body
    Status appendMessages(const std::vector<OfflineMessageRecord>& messages, Packet& response);
    // 清 Redis 未读数
    Status clearUnreadForMessages(std::uint64_t user_id,
                                  const std::vector<OfflineMessageRecord>& messages);

    IStorage& storage_;
    ICache& cache_;
    OnlineService& online_service_;
    OfflineMessageServiceOptions options_;
};

}  // namespace liteim
