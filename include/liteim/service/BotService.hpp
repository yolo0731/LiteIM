#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/cache/ICache.hpp"
#include "liteim/net/Session.hpp"
#include "liteim/service/BotGateway.hpp"
#include "liteim/service/OnlineService.hpp"
#include "liteim/storage/IStorage.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace liteim {

class BotService {
public:
    BotService(IStorage& storage, ICache& cache, OnlineService& online_service,
               BotGateway& gateway, BotOptions options = {});

    const BotOptions& options() const noexcept;
    bool isBotUser(std::uint64_t user_id) const noexcept;
    bool isBotMentioned(const std::string& text) const;
    bool shouldHandleGroupMention(const MessageRecord& message,
                                  const std::vector<GroupMemberRecord>& members) const;

    Status handlePrivateMessageToBot(const MessageRecord& user_message,
                                     const Session::Ptr& sender_session);
    Status handleGroupMention(const MessageRecord& user_message,
                              const std::vector<GroupMemberRecord>& members);

private:
    IStorage& storage_;
    ICache& cache_;
    OnlineService& online_service_;
    BotGateway& gateway_;
    BotOptions options_;
};

}  // namespace liteim
