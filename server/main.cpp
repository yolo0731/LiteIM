#include "liteim/base/Config.hpp"
#include "liteim/base/Logger.hpp"
#include "liteim/concurrency/ThreadPool.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SignalWatcher.hpp"
#include "liteim/net/TcpServer.hpp"
#include "liteim/service/MessageRouter.hpp"

#include <csignal>
#include <vector>

int main() {
    const auto config = liteim::Config::defaults();
    liteim::Logger::init(liteim::parseLogLevel(config.log_level));

    liteim::EventLoop loop;
    liteim::ThreadPool business_pool(config.business_threads);
    liteim::MessageRouter router(business_pool);
    liteim::TcpServer server(&loop, config.server_host, config.server_port, config.io_threads);
    server.setSessionOutputHighWaterMark(config.session_output_high_water_mark);
    server.setMessageCallback([&router](const liteim::Session::Ptr& session,
                                         const liteim::Packet& packet) {
        router.route(session, packet);
    });
    liteim::SignalWatcher signal_watcher(&loop, std::vector<int>{SIGINT, SIGTERM}, [&](int signo) {
        liteim::Logger::get()->info("LiteIM server received signal {}, shutting down", signo);
        server.stop();
        business_pool.stop();
        loop.quit();
    });
    const auto signal_status = signal_watcher.start();
    if (!signal_status.isOk()) {
        liteim::Logger::get()->error("Failed to start signal watcher: {}", signal_status.message());
        return 1;
    }

    const auto business_pool_status = business_pool.start();
    if (!business_pool_status.isOk()) {
        liteim::Logger::get()->error("Failed to start business thread pool: {}",
                                     business_pool_status.message());
        signal_watcher.stop();
        return 1;
    }

    server.start();
    liteim::Logger::get()->info("LiteIM server is listening on {}:{}", config.server_host,
                                server.port());

    loop.loop();
    business_pool.stop();
    signal_watcher.stop();
    return 0;
}
