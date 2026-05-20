#include "ClientCli.hpp"

#include "liteim/net/UniqueFd.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;

std::uint64_t uint64Field(const liteim::Packet& packet, liteim::TlvType type) {
    liteim::TlvMap fields;
    EXPECT_TRUE(liteim::parseTlvMap(packet.body, fields).isOk());
    std::uint64_t value = 0;
    EXPECT_TRUE(liteim::getUint64(fields, type, value).isOk());
    return value;
}

std::string stringField(const liteim::Packet& packet, liteim::TlvType type) {
    liteim::TlvMap fields;
    EXPECT_TRUE(liteim::parseTlvMap(packet.body, fields).isOk());
    std::string value;
    EXPECT_TRUE(liteim::getString(fields, type, value).isOk());
    return value;
}

liteim::Bytes readExact(int fd, std::size_t len) {
    liteim::Bytes output(len);
    std::size_t read_bytes = 0;
    while (read_bytes < len) {
        const auto rc = ::read(fd, output.data() + read_bytes, len - read_bytes);
        if (rc > 0) {
            read_bytes += static_cast<std::size_t>(rc);
            continue;
        }
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        output.resize(read_bytes);
        return output;
    }
    return output;
}

std::optional<liteim::Packet> readPacket(int fd) {
    const auto header_bytes = readExact(fd, liteim::kPacketHeaderSize);
    if (header_bytes.size() != liteim::kPacketHeaderSize) {
        return std::nullopt;
    }

    liteim::PacketHeader header;
    const auto header_status =
        liteim::parseHeader(header_bytes.data(), header_bytes.size(), header);
    EXPECT_TRUE(header_status.isOk()) << header_status.message();
    if (!header_status.isOk()) {
        return std::nullopt;
    }

    liteim::Packet packet;
    packet.header = header;
    packet.body = readExact(fd, header.body_len);
    if (packet.body.size() != header.body_len) {
        return std::nullopt;
    }
    return packet;
}

class OnePacketServer {
public:
    OnePacketServer() {
        listen_fd_.reset(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
        EXPECT_TRUE(static_cast<bool>(listen_fd_)) << "socket errno=" << errno;

        int reuse = 1;
        EXPECT_EQ(::setsockopt(listen_fd_.fd(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)),
                  0);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        EXPECT_EQ(::bind(listen_fd_.fd(), reinterpret_cast<sockaddr*>(&address), sizeof(address)),
                  0)
            << "bind errno=" << errno;
        EXPECT_EQ(::listen(listen_fd_.fd(), 1), 0) << "listen errno=" << errno;

        socklen_t len = sizeof(address);
        EXPECT_EQ(
            ::getsockname(listen_fd_.fd(), reinterpret_cast<sockaddr*>(&address), &len), 0);
        port_ = ntohs(address.sin_port);

        thread_ = std::thread([this]() {
            liteim::UniqueFd accepted(::accept4(listen_fd_.fd(), nullptr, nullptr, SOCK_CLOEXEC));
            if (!accepted) {
                return;
            }
            packet_ = readPacket(accepted.fd());
            done_.store(true);
        });
    }

    OnePacketServer(const OnePacketServer&) = delete;
    OnePacketServer& operator=(const OnePacketServer&) = delete;

    ~OnePacketServer() {
        listen_fd_.reset();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::uint16_t port() const noexcept {
        return port_;
    }

    void wait() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    const std::optional<liteim::Packet>& packet() const noexcept {
        return packet_;
    }

private:
    liteim::UniqueFd listen_fd_;
    std::uint16_t port_{0};
    std::atomic<bool> done_{false};
    std::optional<liteim::Packet> packet_;
    std::thread thread_;
};

}  // namespace

TEST(ClientCliCommandTest, LoginCommandBuildsLoginPacket) {
    liteim::Packet packet;

    const auto status = liteim::cli::buildPacketFromLine("login alice secret", 42, packet);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::LoginRequest);
    EXPECT_EQ(packet.header.seq_id, 42U);
    EXPECT_EQ(stringField(packet, liteim::TlvType::Username), "alice");
    EXPECT_EQ(stringField(packet, liteim::TlvType::Password), "secret");
}

TEST(ClientCliCommandTest, LogoutCommandBuildsLogoutPacket) {
    liteim::Packet packet;

    const auto status = liteim::cli::buildPacketFromLine("logout", 51, packet);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::LogoutRequest);
    EXPECT_EQ(packet.header.seq_id, 51U);
    EXPECT_TRUE(packet.body.empty());
}

TEST(ClientCliCommandTest, PrivateCommandKeepsMessageTextWithSpaces) {
    liteim::Packet packet;

    const auto status =
        liteim::cli::buildPacketFromLine("private 1002 hello bob from cli", 43, packet);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::PrivateMessageRequest);
    EXPECT_EQ(packet.header.seq_id, 43U);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::ReceiverId), 1002U);
    EXPECT_EQ(stringField(packet, liteim::TlvType::MessageText), "hello bob from cli");
}

TEST(ClientCliCommandTest, PrivateIdCommandAddsClientMessageId) {
    liteim::Packet packet;

    const auto status =
        liteim::cli::buildPacketFromLine("private-id 1002 cli-msg-1 hello bob", 47, packet);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::PrivateMessageRequest);
    EXPECT_EQ(packet.header.seq_id, 47U);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::ReceiverId), 1002U);
    EXPECT_EQ(stringField(packet, liteim::TlvType::ClientMessageId), "cli-msg-1");
    EXPECT_EQ(stringField(packet, liteim::TlvType::MessageText), "hello bob");
}

TEST(ClientCliCommandTest, HistoryCommandBuildsCursorRequest) {
    liteim::Packet packet;

    const auto status = liteim::cli::buildPacketFromLine("history group 2001 20 5003", 44, packet);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::HistoryRequest);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::ConversationType),
              static_cast<std::uint64_t>(liteim::ConversationType::kGroup));
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::ConversationId), 2001U);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::Limit), 20U);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::MessageId), 5003U);
}

TEST(ClientCliCommandTest, OfflineAckCommandBuildsBatchAckRequest) {
    liteim::Packet packet;

    const auto status = liteim::cli::buildPacketFromLine("offline-ack 5001 5002", 46, packet);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::OfflineMessagesAckRequest);
    EXPECT_EQ(packet.header.seq_id, 46U);
    liteim::TlvMap fields;
    ASSERT_TRUE(liteim::parseTlvMap(packet.body, fields).isOk());
    std::vector<std::uint64_t> ids;
    ASSERT_TRUE(liteim::getRepeatedUint64(fields, liteim::TlvType::MessageId, ids).isOk());
    EXPECT_EQ(ids, (std::vector<std::uint64_t>{5001, 5002}));
}

TEST(ClientCliCommandTest, AcceptFriendCommandBuildsRequest) {
    liteim::Packet packet;

    const auto status = liteim::cli::buildPacketFromLine("accept-friend 1001", 49, packet);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::AcceptFriendRequest);
    EXPECT_EQ(packet.header.seq_id, 49U);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::TargetUserId), 1001U);
}

TEST(ClientCliCommandTest, RejectFriendCommandBuildsRequest) {
    liteim::Packet packet;

    const auto status = liteim::cli::buildPacketFromLine("reject-friend 1001", 50, packet);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::RejectFriendRequest);
    EXPECT_EQ(packet.header.seq_id, 50U);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::TargetUserId), 1001U);
}

TEST(ClientCliCommandTest, DeliveryAckCommandBuildsRequest) {
    liteim::Packet packet;

    const auto status = liteim::cli::buildPacketFromLine("delivery-ack 5001", 48, packet);

    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::DeliveryAckRequest);
    EXPECT_EQ(packet.header.seq_id, 48U);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::MessageId), 5001U);
}

TEST(ClientCliCommandTest, DescribePacketIncludesMessageFields) {
    liteim::Packet packet;
    packet.header.msg_type = liteim::MessageType::PrivateMessagePush;
    packet.header.seq_id = 0;
    ASSERT_TRUE(liteim::appendUint64(liteim::TlvType::MessageId, 5001, packet.body).isOk());
    ASSERT_TRUE(liteim::appendUint64(liteim::TlvType::SenderId, 1001, packet.body).isOk());
    ASSERT_TRUE(liteim::appendUint64(liteim::TlvType::ReceiverId, 1002, packet.body).isOk());
    ASSERT_TRUE(liteim::appendString(liteim::TlvType::ClientMessageId, "cli-msg-1", packet.body)
                    .isOk());
    ASSERT_TRUE(liteim::appendString(liteim::TlvType::MessageText, "hello", packet.body).isOk());

    const auto description = liteim::cli::describePacket(packet);

    EXPECT_NE(description.find("PRIVATE_MESSAGE_PUSH"), std::string::npos);
    EXPECT_NE(description.find("message_id=5001"), std::string::npos);
    EXPECT_NE(description.find("sender_id=1001"), std::string::npos);
    EXPECT_NE(description.find("receiver_id=1002"), std::string::npos);
    EXPECT_NE(description.find("client_msg_id=\"cli-msg-1\""), std::string::npos);
    EXPECT_NE(description.find("text=\"hello\""), std::string::npos);
}

TEST(ClientCliCommandTest, DescribePacketIncludesDeliveryStatus) {
    liteim::Packet packet;
    packet.header.msg_type = liteim::MessageType::OfflineMessagesAckResponse;
    packet.header.seq_id = 46;
    ASSERT_TRUE(liteim::appendUint64(liteim::TlvType::MessageId, 5001, packet.body).isOk());
    ASSERT_TRUE(liteim::appendUint64(liteim::TlvType::DeliveryStatus, 2, packet.body).isOk());

    const auto description = liteim::cli::describePacket(packet);

    EXPECT_NE(description.find("OFFLINE_MESSAGES_ACK_RESPONSE"), std::string::npos);
    EXPECT_NE(description.find("message_id=5001"), std::string::npos);
    EXPECT_NE(description.find("delivery_status=2"), std::string::npos);
}

TEST(ClientCliCommandTest, DescribePacketIncludesFriendRequestStatus) {
    liteim::Packet packet;
    packet.header.msg_type = liteim::MessageType::AddFriendResponse;
    packet.header.seq_id = 49;
    ASSERT_TRUE(liteim::appendUint64(liteim::TlvType::FriendId, 1002, packet.body).isOk());
    ASSERT_TRUE(liteim::appendUint64(liteim::TlvType::FriendRequestStatus, 0, packet.body).isOk());

    const auto description = liteim::cli::describePacket(packet);

    EXPECT_NE(description.find("ADD_FRIEND_RESPONSE"), std::string::npos);
    EXPECT_NE(description.find("friend_id=1002"), std::string::npos);
    EXPECT_NE(description.find("friend_request_status=0"), std::string::npos);
}

TEST(ClientCliProtocolClientTest, ConnectsAndSendsPacketToLoopbackServer) {
    OnePacketServer server;
    liteim::cli::ProtocolClient client;
    liteim::Packet packet;
    ASSERT_TRUE(liteim::cli::buildPacketFromLine("heartbeat", 45, packet).isOk());

    const auto connect_status = client.connectTo("127.0.0.1", server.port());
    ASSERT_TRUE(connect_status.isOk()) << connect_status.message();
    const auto send_status = client.sendPacket(packet);
    ASSERT_TRUE(send_status.isOk()) << send_status.message();
    client.close();
    server.wait();

    const auto& received = server.packet();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->header.msg_type, liteim::MessageType::HeartbeatRequest);
    EXPECT_EQ(received->header.seq_id, 45U);
}
