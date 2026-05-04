#pragma once

#include "liteim/storage/StorageTypes.hpp"

#include <optional>

namespace liteim::storage {

class ICache {
public:
    virtual ~ICache() = default;

    virtual void setOnline(UserId user_id, int session_fd) = 0;
    virtual void setOffline(UserId user_id) = 0;
    virtual std::optional<int> findOnlineSession(UserId user_id) const = 0;
    virtual void clear() = 0;
};

}  // namespace liteim::storage
