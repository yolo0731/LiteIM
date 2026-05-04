#include "liteim/storage/NullCache.hpp"

namespace liteim::storage {

void NullCache::setOnline(UserId user_id, int session_fd) {
    (void)user_id;
    (void)session_fd;
}

void NullCache::setOffline(UserId user_id) {
    (void)user_id;
}

std::optional<int> NullCache::findOnlineSession(UserId user_id) const {
    (void)user_id;
    return std::nullopt;
}

void NullCache::clear() {}

}  // namespace liteim::storage
