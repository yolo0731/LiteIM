#include "liteim_client/ClientSession.hpp"
#include "liteim_client/PacketCodec.hpp"
#include "liteim_client/TcpClient.hpp"

#include "liteim/protocol/FrameDecoder.hpp"
#include "liteim/protocol/MessageType.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"

#include <QAbstractSocket>
#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVariant>

#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

namespace {

bool waitForSpyCount(QSignalSpy& spy, int count, int timeout_ms = 2000) {
    QElapsedTimer timer;
    timer.start();
    while (spy.count() < count && timer.elapsed() < timeout_ms) {
        spy.wait(static_cast<int>(timeout_ms - timer.elapsed()));
    }
    return spy.count() >= count;
}

bool waitForBytes(QTcpSocket& socket, qint64 count, int timeout_ms = 2000) {
    QElapsedTimer timer;
    timer.start();
    while (socket.bytesAvailable() < count && timer.elapsed() < timeout_ms) {
        if (!socket.waitForReadyRead(static_cast<int>(timeout_ms - timer.elapsed()))) {
            break;
        }
    }
    return socket.bytesAvailable() >= count;
}

liteim::Packet makeLoginPacket(std::uint64_t seq_id) {
    liteim::Packet packet;
    packet.header.msg_type = liteim::MessageType::LoginRequest;
    packet.header.seq_id = seq_id;
    EXPECT_TRUE(liteim::client::PacketCodec::appendStringField(
                    liteim::TlvType::Username, QStringLiteral("alice"), packet)
                    .isOk());
    EXPECT_TRUE(liteim::client::PacketCodec::appendStringField(
                    liteim::TlvType::Password, QStringLiteral("secret"), packet)
                    .isOk());
    return packet;
}

}  // namespace

TEST(QtPacketCodecTest, EncodedPacketUsesServerWireFormat) {
    const auto packet = makeLoginPacket(42);
    QByteArray encoded;

    const auto status = liteim::client::PacketCodec::encode(packet, encoded);

    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_GE(encoded.size(), static_cast<int>(liteim::kPacketHeaderSize));

    liteim::PacketHeader header;
    const auto header_status = liteim::parseHeader(
        reinterpret_cast<const liteim::Byte*>(encoded.constData()),
        static_cast<std::size_t>(encoded.size()),
        header);
    ASSERT_TRUE(header_status.isOk()) << header_status.message();
    EXPECT_EQ(header.msg_type, liteim::MessageType::LoginRequest);
    EXPECT_EQ(header.seq_id, 42U);

    liteim::TlvMap fields;
    const auto body = reinterpret_cast<const liteim::Byte*>(encoded.constData()) +
                      liteim::kPacketHeaderSize;
    ASSERT_TRUE(liteim::parseTlvMap(body, header.body_len, fields).isOk());
    std::string username;
    ASSERT_TRUE(liteim::getString(fields, liteim::TlvType::Username, username).isOk());
    EXPECT_EQ(username, "alice");
}

TEST(QtPacketCodecTest, DecodesServerPacketsAcrossHalfAndStickyFrames) {
    const auto first = makeLoginPacket(100);
    const auto second = makeLoginPacket(101);

    liteim::Bytes first_bytes;
    ASSERT_TRUE(liteim::encodePacket(first, first_bytes).isOk());
    liteim::Bytes second_bytes;
    ASSERT_TRUE(liteim::encodePacket(second, second_bytes).isOk());

    QByteArray first_frame(reinterpret_cast<const char*>(first_bytes.data()),
                           static_cast<int>(first_bytes.size()));
    QByteArray sticky_tail = first_frame.mid(5);
    sticky_tail.append(reinterpret_cast<const char*>(second_bytes.data()),
                       static_cast<int>(second_bytes.size()));

    liteim::client::PacketCodec codec;
    std::vector<liteim::Packet> packets;

    auto status = codec.feed(first_frame.left(5), packets);
    ASSERT_TRUE(status.isOk()) << status.message();
    EXPECT_TRUE(packets.empty());

    status = codec.feed(sticky_tail, packets);
    ASSERT_TRUE(status.isOk()) << status.message();
    ASSERT_EQ(packets.size(), 2U);
    EXPECT_EQ(packets[0].header.seq_id, 100U);
    EXPECT_EQ(packets[1].header.seq_id, 101U);
}

TEST(QtClientSessionTest, TracksPendingRequestsAndLoginState) {
    liteim::client::ClientSession session;

    const auto login_seq = session.trackRequest(liteim::MessageType::LoginRequest);
    const auto private_seq = session.trackRequest(liteim::MessageType::PrivateMessageRequest);

    EXPECT_NE(login_seq, private_seq);
    EXPECT_TRUE(session.hasPending(login_seq));
    EXPECT_EQ(session.pendingCount(), 2U);

    const auto pending = session.takePending(login_seq);
    ASSERT_TRUE(pending.has_value());
    EXPECT_EQ(pending->request_type, liteim::MessageType::LoginRequest);
    EXPECT_FALSE(session.hasPending(login_seq));
    EXPECT_EQ(session.pendingCount(), 1U);

    session.markLoggedIn(1001, QStringLiteral("token-1"), QStringLiteral("session-1"));
    EXPECT_TRUE(session.isLoggedIn());
    EXPECT_EQ(session.userId(), 1001U);
    EXPECT_EQ(session.token(), QStringLiteral("token-1"));
    EXPECT_EQ(session.sessionId(), QStringLiteral("session-1"));

    session.reset();
    EXPECT_FALSE(session.isLoggedIn());
    EXPECT_EQ(session.pendingCount(), 0U);
}

TEST(QtTcpClientTest, EmitsErrorForConnectionFailure) {
    QTcpServer closed_server;
    ASSERT_TRUE(closed_server.listen(QHostAddress::LocalHost, 0));
    const auto port = closed_server.serverPort();
    closed_server.close();

    liteim::client::TcpClient client;
    QSignalSpy error_spy(&client, &liteim::client::TcpClient::errorOccurred);

    client.connectToHost(QStringLiteral("127.0.0.1"), port);

    ASSERT_TRUE(waitForSpyCount(error_spy, 1));
    EXPECT_FALSE(error_spy.takeFirst().at(0).toString().isEmpty());
}

TEST(QtTcpClientTest, SendsPacketToLoopbackServer) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::TcpClient client;
    QSignalSpy connected_spy(&client, &liteim::client::TcpClient::connected);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    client.connectToHost(QStringLiteral("127.0.0.1"), server.serverPort());

    ASSERT_TRUE(waitForSpyCount(connected_spy, 1));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    const auto send_status = client.sendPacket(makeLoginPacket(77));
    ASSERT_TRUE(send_status.isOk()) << send_status.message();

    ASSERT_TRUE(waitForBytes(*server_socket, static_cast<qint64>(liteim::kPacketHeaderSize)));
    const auto bytes = server_socket->readAll();

    liteim::FrameDecoder decoder;
    std::vector<liteim::Packet> packets;
    const auto decode_status = decoder.feed(
        reinterpret_cast<const liteim::Byte*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size()),
        packets);
    ASSERT_TRUE(decode_status.isOk()) << decode_status.message();
    ASSERT_EQ(packets.size(), 1U);
    EXPECT_EQ(packets[0].header.msg_type, liteim::MessageType::LoginRequest);
    EXPECT_EQ(packets[0].header.seq_id, 77U);
}

TEST(QtTcpClientTest, EmitsPacketReceivedForServerPacket) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::TcpClient client;
    QSignalSpy connected_spy(&client, &liteim::client::TcpClient::connected);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);
    QSignalSpy packet_spy(&client, &liteim::client::TcpClient::packetReceived);

    client.connectToHost(QStringLiteral("127.0.0.1"), server.serverPort());

    ASSERT_TRUE(waitForSpyCount(connected_spy, 1));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    QByteArray encoded;
    ASSERT_TRUE(liteim::client::PacketCodec::encode(makeLoginPacket(88), encoded).isOk());
    ASSERT_EQ(server_socket->write(encoded), encoded.size());
    ASSERT_TRUE(server_socket->waitForBytesWritten(2000));

    ASSERT_TRUE(waitForSpyCount(packet_spy, 1));
    const auto received = qvariant_cast<liteim::Packet>(packet_spy.takeFirst().at(0));
    EXPECT_EQ(received.header.msg_type, liteim::MessageType::LoginRequest);
    EXPECT_EQ(received.header.seq_id, 88U);
}
