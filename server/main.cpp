#include "liteim/base/Config.hpp"
#include "liteim/base/Logger.hpp"
#include "liteim/net/EventLoop.hpp"
#include "liteim/net/TcpServer.hpp"

int main() {
    const auto config = liteim::Config::defaults();
    liteim::Logger::init(liteim::parseLogLevel(config.log_level));

    liteim::EventLoop loop;
    liteim::TcpServer server(&loop,
                             config.server_host,
                             config.server_port,
                             config.io_threads);
    server.start();
    liteim::Logger::get()->info("LiteIM server is listening on {}:{}",
                                config.server_host,
                                server.port());

    loop.loop();
    return 0;
}
