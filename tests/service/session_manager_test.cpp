#include "liteim/service/SessionManager.hpp"

#include "liteim/net/EventLoop.hpp"
#include "liteim/net/UniqueFd.hpp"

#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <utility>

#include <gtest/gtest.h>

namespace {

struct SocketPair {
    liteim::UniqueFd server;
    liteim::UniqueFd peer;
};

SocketPair makeSocketPair() {
    int fds[2] = {liteim::kInvalidFd, liteim::kInvalidFd};
    const int rc = ::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
    EXPECT_EQ(rc, 0) << "socketpair errno=" << errno;
    return SocketPair{liteim::UniqueFd(fds[0]), liteim::UniqueFd(fds[1])};
}

std::shared_ptr<liteim::Session> makeSession(liteim::EventLoop& loop, std::uint64_t session_id) {
    auto sockets = makeSocketPair();
    return std::make_shared<liteim::Session>(&loop, std::move(sockets.server), session_id);
}

}  // namespace

TEST(SessionManagerTest, HeaderIsSelfContained) {
    liteim::SessionManager manager;
    EXPECT_EQ(manager.userCount(), 0U);
    EXPECT_EQ(manager.sessionCount(), 0U);
}

TEST(SessionManagerTest, BindsAndLooksUpUserAndSession) {
    liteim::EventLoop loop;
    liteim::SessionManager manager;
    auto session = makeSession(loop, 1001);

    const auto bind_status = manager.bindUser(42, session);
    ASSERT_TRUE(bind_status.isOk()) << bind_status.message();

    std::shared_ptr<liteim::Session> found_session;
    ASSERT_TRUE(manager.getSessionByUser(42, found_session).isOk());
    ASSERT_EQ(found_session, session);

    std::uint64_t found_user = 0;
    ASSERT_TRUE(manager.getUserBySession(1001, found_user).isOk());
    EXPECT_EQ(found_user, 42U);
    EXPECT_EQ(manager.userCount(), 1U);
    EXPECT_EQ(manager.sessionCount(), 1U);
}

TEST(SessionManagerTest, DuplicateLoginKicksOldSessionAndKeepsNewBinding) {
    liteim::EventLoop loop;
    liteim::SessionManager manager;
    auto old_session = makeSession(loop, 1001);
    auto new_session = makeSession(loop, 1002);

    ASSERT_TRUE(manager.bindUser(42, old_session).isOk());
    ASSERT_TRUE(manager.bindUser(42, new_session).isOk());

    EXPECT_TRUE(old_session->closed());
    EXPECT_FALSE(new_session->closed());

    std::shared_ptr<liteim::Session> found_session;
    ASSERT_TRUE(manager.getSessionByUser(42, found_session).isOk());
    EXPECT_EQ(found_session, new_session);

    std::uint64_t found_user = 0;
    EXPECT_EQ(manager.getUserBySession(1001, found_user).code(), liteim::ErrorCode::NotFound);
    ASSERT_TRUE(manager.getUserBySession(1002, found_user).isOk());
    EXPECT_EQ(found_user, 42U);
}

TEST(SessionManagerTest, UnbindRemovesOnlyMatchingCurrentSession) {
    liteim::EventLoop loop;
    liteim::SessionManager manager;
    auto old_session = makeSession(loop, 1001);
    auto new_session = makeSession(loop, 1002);

    ASSERT_TRUE(manager.bindUser(42, old_session).isOk());
    ASSERT_TRUE(manager.bindUser(42, new_session).isOk());

    bool removed = true;
    ASSERT_TRUE(manager.unbindUser(42, 1001, removed).isOk());
    EXPECT_FALSE(removed);
    EXPECT_EQ(manager.userCount(), 1U);

    ASSERT_TRUE(manager.unbindUser(42, 1002, removed).isOk());
    EXPECT_TRUE(removed);
    EXPECT_EQ(manager.userCount(), 0U);
    EXPECT_EQ(manager.sessionCount(), 0U);
}

TEST(SessionManagerTest, ExpiredWeakSessionIsCleanedDuringLookup) {
    liteim::EventLoop loop;
    liteim::SessionManager manager;

    {
        auto session = makeSession(loop, 1001);
        ASSERT_TRUE(manager.bindUser(42, session).isOk());
    }

    std::shared_ptr<liteim::Session> found_session;
    const auto status = manager.getSessionByUser(42, found_session);
    EXPECT_EQ(status.code(), liteim::ErrorCode::NotFound);
    EXPECT_EQ(manager.userCount(), 0U);
    EXPECT_EQ(manager.sessionCount(), 0U);
}
