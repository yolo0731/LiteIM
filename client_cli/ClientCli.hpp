#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/Packet.hpp"

#include <cstdint>
#include <mutex>
#include <string>

namespace liteim::cli {

// 这个函数将用户输入的命令行文本解析成一个Packet对象
Status buildPacketFromLine(const std::string& line, std::uint64_t seq_id, Packet& packet);

// 这个函数将Packet对象转换成人类可读的文本，用于调试和日志输出。
std::string describePacket(const Packet& packet);

// 这个函数返回CLI的帮助文本。
std::string helpText();

// 真正负责连接、发送、接收、关闭 TCP socket
class ProtocolClient {
public:
    ProtocolClient() = default;
    ~ProtocolClient();

    ProtocolClient(const ProtocolClient&) = delete;
    ProtocolClient& operator=(const ProtocolClient&) = delete;

    Status connectTo(const std::string& host, std::uint16_t port);
    Status sendPacket(const Packet& packet);
    Status readPacket(Packet& packet);

    void close() noexcept;
    bool connected() const noexcept;

private:
    UniqueFd socket_;  // 这是客户端与服务器通信的socket
    mutable std::mutex socket_mutex_;
};

}  // namespace liteim::cli
