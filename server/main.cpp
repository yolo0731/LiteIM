#include "liteim/net/EventLoop.hpp"
#include "liteim/net/TcpServer.hpp"
#include "liteim/service/MessageRouter.hpp"

#include <exception>
#include <iostream>

int main() {
    try {
        liteim::net::EventLoop loop;
        liteim::net::TcpServer server(&loop, "0.0.0.0", 9000);
        liteim::service::MessageRouter router;

        server.setMessageCallback([&router](
                                      liteim::net::Session& session,
                                      const liteim::protocol::Packet& packet) {
            router.route(session, packet);
        });

        server.start();
        std::cout << "LiteIM server listening on port " << server.port() << std::endl;
        loop.loop();
        std::cout << "LiteIM server stopped" << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "LiteIM server failed: " << ex.what() << std::endl;
        return 1;
    }
}
