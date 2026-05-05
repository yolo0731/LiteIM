#include "liteim/base/Config.hpp"
#include "liteim/base/Logger.hpp"

int main() {
    const auto config = liteim::Config::defaults();
    liteim::Logger::init(liteim::parseLogLevel(config.log_level));
    liteim::Logger::get()->info("LiteIM server scaffold is running on {}:{}",
                                config.server_host,
                                config.server_port);
    return 0;
}
