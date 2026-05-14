#include "liteim/base/Config.hpp"
#include "liteim/base/Logger.hpp"
#include "liteim/cache/RedisCache.hpp"
#include "liteim/cache/RedisPool.hpp"
#include "liteim/concurrency/ThreadPool.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SignalWatcher.hpp"
#include "liteim/net/TcpServer.hpp"
#include "liteim/service/AuthService.hpp"
#include "liteim/service/ChatService.hpp"
#include "liteim/service/FriendService.hpp"
#include "liteim/service/MessageRouter.hpp"
#include "liteim/service/OnlineService.hpp"
#include "liteim/service/SessionManager.hpp"
#include "liteim/storage/MySqlPool.hpp"
#include "liteim/storage/MySqlStorage.hpp"

#include <chrono>
#include <csignal>
#include <vector>

int main() {
    const auto config = liteim::Config::defaults();
    liteim::Logger::init(liteim::parseLogLevel(config.log_level));

    liteim::EventLoop loop;
    liteim::SignalWatcher signal_watcher(&loop, std::vector<int>{SIGINT, SIGTERM}, [&](int signo) {
        liteim::Logger::get()->info("LiteIM server received signal {}, shutting down", signo);
        loop.quit();
    });
    const auto signal_status = signal_watcher.start();
    if (!signal_status.isOk()) {
        liteim::Logger::get()->error("Failed to start signal watcher: {}", signal_status.message());
        return 1;
    }

    liteim::MySqlPool mysql_pool(config.mysql);
    const auto mysql_status = mysql_pool.start();
    if (!mysql_status.isOk()) {
        liteim::Logger::get()->error("Failed to start MySQL pool: {}", mysql_status.message());
        signal_watcher.stop();
        return 1;
    }

    liteim::RedisPool redis_pool(config.redis);
    const auto redis_status = redis_pool.start();
    if (!redis_status.isOk()) {
        liteim::Logger::get()->error("Failed to start Redis pool: {}", redis_status.message());
        mysql_pool.close();
        signal_watcher.stop();
        return 1;
    }

    liteim::MySqlStorage storage(mysql_pool);
    liteim::RedisCache cache(redis_pool);
    liteim::SessionManager sessions;
    liteim::OnlineService online_service(sessions, cache, "liteim-server", std::chrono::seconds{60});
    liteim::ThreadPool business_pool(config.business_threads);
    liteim::MessageRouter router(business_pool);
    liteim::AuthService auth_service(storage, cache, online_service);
    liteim::FriendService friend_service(storage, cache, online_service);
    liteim::ChatService chat_service(storage, cache, online_service);
    const auto auth_status = auth_service.registerHandlers(router);
    if (!auth_status.isOk()) {
        liteim::Logger::get()->error("Failed to register auth handlers: {}", auth_status.message());
        redis_pool.close();
        mysql_pool.close();
        signal_watcher.stop();
        return 1;
    }
    const auto friend_status = friend_service.registerHandlers(router);
    if (!friend_status.isOk()) {
        liteim::Logger::get()->error("Failed to register friend handlers: {}",
                                     friend_status.message());
        redis_pool.close();
        mysql_pool.close();
        signal_watcher.stop();
        return 1;
    }
    const auto chat_status = chat_service.registerHandlers(router);
    if (!chat_status.isOk()) {
        liteim::Logger::get()->error("Failed to register chat handlers: {}", chat_status.message());
        redis_pool.close();
        mysql_pool.close();
        signal_watcher.stop();
        return 1;
    }

    liteim::TcpServer server(&loop, config.server_host, config.server_port, config.io_threads);
    server.setSessionOutputHighWaterMark(config.session_output_high_water_mark);
    server.setMessageCallback([&router](const liteim::Session::Ptr& session,
                                         const liteim::Packet& packet) {
        router.route(session, packet);
    });

    const auto business_pool_status = business_pool.start();
    if (!business_pool_status.isOk()) {
        liteim::Logger::get()->error("Failed to start business thread pool: {}",
                                     business_pool_status.message());
        redis_pool.close();
        mysql_pool.close();
        signal_watcher.stop();
        return 1;
    }

    server.start();
    liteim::Logger::get()->info("LiteIM server is listening on {}:{}", config.server_host,
                                server.port());

    loop.loop();
    server.stop();
    business_pool.stop();
    redis_pool.close();
    mysql_pool.close();
    signal_watcher.stop();
    return 0;
}
