#include "liteim/base/Config.hpp"
#include "liteim/base/Logger.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/SignalWatcher.hpp"
#include "liteim/net/TcpServer.hpp"

#include <csignal>
#include <vector>

int main() {
    const auto config = liteim::Config::defaults();
    liteim::Logger::init(liteim::parseLogLevel(config.log_level));

    liteim::EventLoop loop;
    liteim::TcpServer server(&loop,
                             config.server_host,
                             config.server_port,
                             config.io_threads);
    liteim::SignalWatcher signal_watcher(&loop, std::vector<int>{SIGINT, SIGTERM}, [&](int signo) {
        liteim::Logger::get()->info("LiteIM server received signal {}, shutting down", signo);
        server.stop();
        loop.quit();
    });
    const auto signal_status = signal_watcher.start();
    if (!signal_status.isOk()) {
        liteim::Logger::get()->error("Failed to start signal watcher: {}", signal_status.message());
        return 1;
    }

    server.start();
    liteim::Logger::get()->info("LiteIM server is listening on {}:{}",
                                config.server_host,
                                server.port());

    loop.loop();
    signal_watcher.stop();
    return 0;
}
