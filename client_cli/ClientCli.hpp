#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/Packet.hpp"

#include <cstdint>
#include <mutex>
#include <string>

namespace liteim::cli {

Status buildPacketFromLine(const std::string& line, std::uint64_t seq_id, Packet& packet);

std::string describePacket(const Packet& packet);

std::string helpText();

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
    UniqueFd socket_;
    mutable std::mutex socket_mutex_;
};

}  // namespace liteim::cli
