#pragma once

#include "liteim/storage/ICache.hpp"

namespace liteim::storage {

class NullCache final : public ICache {
public:
    void setOnline(UserId user_id, int session_fd) override;
    void setOffline(UserId user_id) override;
    std::optional<int> findOnlineSession(UserId user_id) const override;
    void clear() override;
};

}  // namespace liteim::storage
