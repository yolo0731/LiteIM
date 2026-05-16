#pragma once

#include "liteim/cache/ICache.hpp"

#include <gmock/gmock.h>

namespace liteim::test {

class MockCache : public ICache {
public:
    MOCK_METHOD(Status, setUserOnline,
                (const OnlineSession& session, std::chrono::seconds ttl), (override));
    MOCK_METHOD(Status, refreshUserOnline,
                (std::uint64_t user_id, std::chrono::seconds ttl), (override));
    MOCK_METHOD(Status, setUserOffline, (std::uint64_t user_id), (override));
    MOCK_METHOD(Status, isUserOnline, (std::uint64_t user_id, bool& online), (override));
    MOCK_METHOD(Status, getOnlineSession,
                (std::uint64_t user_id, OnlineSession& session), (override));
    MOCK_METHOD(Status, incrUnread,
                (const UnreadKey& key, std::uint64_t delta, std::uint64_t& unread_count),
                (override));
    MOCK_METHOD(Status, getUnread,
                (const UnreadKey& key, std::uint64_t& unread_count), (override));
    MOCK_METHOD(Status, clearUnread, (const UnreadKey& key), (override));
    MOCK_METHOD(Status, allowLoginAttempt,
                (const LoginAttemptKey& key, std::uint32_t max_failures, bool& allowed),
                (override));
    MOCK_METHOD(Status, recordLoginFailure,
                (const LoginAttemptKey& key, std::chrono::seconds ttl), (override));
    MOCK_METHOD(Status, clearLoginFailure, (const LoginAttemptKey& key), (override));
};

}  // namespace liteim::test
