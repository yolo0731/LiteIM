#include "liteim/cache/ICache.hpp"

#include <chrono>
#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

class NullCache final : public liteim::ICache {
public:
    liteim::Status setUserOnline(const liteim::OnlineSession&, std::chrono::seconds) override {
        return liteim::Status::ok();
    }

    liteim::Status refreshUserOnline(std::uint64_t, std::chrono::seconds) override {
        return liteim::Status::ok();
    }

    liteim::Status setUserOffline(std::uint64_t) override {
        return liteim::Status::ok();
    }

    liteim::Status isUserOnline(std::uint64_t, bool& online) override {
        online = false;
        return liteim::Status::ok();
    }

    liteim::Status getOnlineSession(std::uint64_t, liteim::OnlineSession&) override {
        return liteim::Status::error(liteim::ErrorCode::NotFound, "user is offline");
    }

    liteim::Status incrUnread(const liteim::UnreadKey&, std::uint64_t delta,
                              std::uint64_t& unread_count) override {
        unread_count += delta;
        return liteim::Status::ok();
    }

    liteim::Status getUnread(const liteim::UnreadKey&, std::uint64_t& unread_count) override {
        unread_count = 0;
        return liteim::Status::ok();
    }

    liteim::Status clearUnread(const liteim::UnreadKey&) override {
        return liteim::Status::ok();
    }

    liteim::Status allowLoginAttempt(const liteim::LoginAttemptKey&, std::uint32_t,
                                     bool& allowed) override {
        allowed = true;
        return liteim::Status::ok();
    }

    liteim::Status recordLoginFailure(const liteim::LoginAttemptKey&,
                                      std::chrono::seconds) override {
        return liteim::Status::ok();
    }

    liteim::Status clearLoginFailure(const liteim::LoginAttemptKey&) override {
        return liteim::Status::ok();
    }
};

}  // namespace

TEST(CacheInterfaceTest, HeaderIsSelfContained) {
    static_assert(std::is_abstract_v<liteim::ICache>);
    static_assert(std::has_virtual_destructor_v<liteim::ICache>);

    liteim::UnreadKey key;
    key.user_id = 42;
    key.conversation = {liteim::ConversationType::kPrivate, 7};

    EXPECT_EQ(key.user_id, 42U);
    EXPECT_EQ(key.conversation.id, 7U);
}

TEST(CacheInterfaceTest, NullCacheCanBeUsedAsTestDouble) {
    NullCache cache;
    liteim::ICache& interface = cache;

    bool online = true;
    const auto online_status = interface.isUserOnline(42, online);
    ASSERT_TRUE(online_status.isOk()) << online_status.message();
    EXPECT_FALSE(online);

    std::uint64_t unread = 0;
    const auto incr_status =
        interface.incrUnread({42, {liteim::ConversationType::kPrivate, 7}}, 3, unread);
    ASSERT_TRUE(incr_status.isOk()) << incr_status.message();
    EXPECT_EQ(unread, 3U);

    bool allowed = false;
    const auto allow_status = interface.allowLoginAttempt({"alice", "127.0.0.1"}, 5, allowed);
    ASSERT_TRUE(allow_status.isOk()) << allow_status.message();
    EXPECT_TRUE(allowed);

    liteim::OnlineSession session;
    const auto session_status = interface.getOnlineSession(42, session);
    EXPECT_FALSE(session_status.isOk());
    EXPECT_EQ(session_status.code(), liteim::ErrorCode::NotFound);
}
