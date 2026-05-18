#include "liteim_client/AuthController.hpp"
#include "liteim_client/ClientApp.hpp"
#include "liteim_client/ClientSession.hpp"
#include "liteim_client/LoginWindow.hpp"
#include "liteim_client/MainWindow.hpp"
#include "liteim_client/PacketCodec.hpp"
#include "liteim_client/RegisterDialog.hpp"
#include "liteim_client/TcpClient.hpp"

#include "liteim/protocol/FrameDecoder.hpp"
#include "liteim/protocol/MessageType.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"

#include <QAbstractSocket>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QHostAddress>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QSpinBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVariant>
#include <QWidget>

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

liteim::Packet makeAuthResponse(liteim::MessageType type, std::uint64_t seq_id) {
    liteim::Packet packet;
    packet.header.msg_type = type;
    packet.header.seq_id = seq_id;
    EXPECT_TRUE(liteim::client::PacketCodec::appendUint64Field(
                    liteim::TlvType::UserId, 1001, packet)
                    .isOk());
    EXPECT_TRUE(liteim::client::PacketCodec::appendStringField(
                    liteim::TlvType::Username, QStringLiteral("alice"), packet)
                    .isOk());
    EXPECT_TRUE(liteim::client::PacketCodec::appendStringField(
                    liteim::TlvType::Nickname, QStringLiteral("Alice"), packet)
                    .isOk());
    if (type == liteim::MessageType::LoginResponse) {
        EXPECT_TRUE(liteim::client::PacketCodec::appendUint64Field(
                        liteim::TlvType::SessionId, 5001, packet)
                        .isOk());
    }
    return packet;
}

liteim::Packet makeErrorResponse(std::uint64_t seq_id, const QString& message) {
    liteim::Packet packet;
    packet.header.msg_type = liteim::MessageType::ErrorResponse;
    packet.header.seq_id = seq_id;
    EXPECT_TRUE(liteim::client::PacketCodec::appendStringField(
                    liteim::TlvType::ErrorMessage, message, packet)
                    .isOk());
    return packet;
}

bool writePacket(QTcpSocket& socket, const liteim::Packet& packet) {
    QByteArray encoded;
    if (!liteim::client::PacketCodec::encode(packet, encoded).isOk()) {
        return false;
    }
    return socket.write(encoded) == encoded.size() && socket.waitForBytesWritten(2000);
}

liteim::Packet readPacketFromSocket(QTcpSocket& socket) {
    EXPECT_TRUE(waitForBytes(socket, static_cast<qint64>(liteim::kPacketHeaderSize)));
    const auto header_bytes = socket.peek(static_cast<qint64>(liteim::kPacketHeaderSize));
    liteim::PacketHeader header;
    EXPECT_TRUE(liteim::parseHeader(reinterpret_cast<const liteim::Byte*>(header_bytes.constData()),
                                    static_cast<std::size_t>(header_bytes.size()),
                                    header)
                    .isOk());
    EXPECT_TRUE(waitForBytes(socket,
                             static_cast<qint64>(liteim::kPacketHeaderSize + header.body_len)));
    const auto bytes = socket.read(static_cast<qint64>(liteim::kPacketHeaderSize + header.body_len));
    liteim::FrameDecoder decoder;
    std::vector<liteim::Packet> packets;
    EXPECT_TRUE(decoder
                    .feed(reinterpret_cast<const liteim::Byte*>(bytes.constData()),
                          static_cast<std::size_t>(bytes.size()),
                          packets)
                    .isOk());
    EXPECT_EQ(packets.size(), 1U);
    return packets.empty() ? liteim::Packet{} : packets.front();
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

TEST(QtLoginWindowTest, LoginButtonRequiresServerUsernameAndPassword) {
    liteim::client::LoginWindow window;
    auto* host = window.findChild<QLineEdit*>("serverHostEdit");
    auto* username = window.findChild<QLineEdit*>("usernameEdit");
    auto* password = window.findChild<QLineEdit*>("passwordEdit");
    auto* login = window.findChild<QPushButton*>("loginButton");
    ASSERT_NE(host, nullptr);
    ASSERT_NE(username, nullptr);
    ASSERT_NE(password, nullptr);
    ASSERT_NE(login, nullptr);

    host->clear();
    username->setText(QStringLiteral("alice"));
    password->setText(QStringLiteral("secret"));
    EXPECT_FALSE(login->isEnabled());

    host->setText(QStringLiteral("127.0.0.1"));
    username->clear();
    EXPECT_FALSE(login->isEnabled());

    username->setText(QStringLiteral("alice"));
    password->clear();
    EXPECT_FALSE(login->isEnabled());

    password->setText(QStringLiteral("secret"));
    EXPECT_TRUE(login->isEnabled());
}

TEST(QtRegisterDialogTest, RegisterButtonRequiresUsernameAndPassword) {
    liteim::client::RegisterDialog dialog;
    auto* username = dialog.findChild<QLineEdit*>("registerUsernameEdit");
    auto* password = dialog.findChild<QLineEdit*>("registerPasswordEdit");
    auto* submit = dialog.findChild<QPushButton*>("registerSubmitButton");
    ASSERT_NE(username, nullptr);
    ASSERT_NE(password, nullptr);
    ASSERT_NE(submit, nullptr);

    username->clear();
    password->setText(QStringLiteral("secret"));
    EXPECT_FALSE(submit->isEnabled());

    username->setText(QStringLiteral("alice"));
    password->clear();
    EXPECT_FALSE(submit->isEnabled());

    password->setText(QStringLiteral("secret"));
    EXPECT_TRUE(submit->isEnabled());
}

TEST(QtAuthControllerTest, RegisterSuccessThenLoginSuccessUsesSameWireProtocol) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::AuthController controller;
    QSignalSpy register_spy(&controller, &liteim::client::AuthController::registerSucceeded);
    QSignalSpy login_spy(&controller, &liteim::client::AuthController::loginSucceeded);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    controller.registerUser(QStringLiteral("127.0.0.1"),
                            server.serverPort(),
                            QStringLiteral("alice"),
                            QStringLiteral("secret"),
                            QStringLiteral("Alice"));

    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    const auto register_request = readPacketFromSocket(*server_socket);
    EXPECT_EQ(register_request.header.msg_type, liteim::MessageType::RegisterRequest);
    ASSERT_TRUE(writePacket(*server_socket, makeAuthResponse(liteim::MessageType::RegisterResponse,
                                                             register_request.header.seq_id)));
    ASSERT_TRUE(waitForSpyCount(register_spy, 1));

    controller.login(QStringLiteral("127.0.0.1"),
                     server.serverPort(),
                     QStringLiteral("alice"),
                     QStringLiteral("secret"));

    const auto login_request = readPacketFromSocket(*server_socket);
    EXPECT_EQ(login_request.header.msg_type, liteim::MessageType::LoginRequest);
    ASSERT_TRUE(writePacket(*server_socket,
                            makeAuthResponse(liteim::MessageType::LoginResponse,
                                             login_request.header.seq_id)));
    ASSERT_TRUE(waitForSpyCount(login_spy, 1));

    const auto result = qvariant_cast<liteim::client::AuthResult>(login_spy.takeFirst().at(0));
    EXPECT_EQ(result.user_id, 1001U);
    EXPECT_EQ(result.username, QStringLiteral("alice"));
    EXPECT_EQ(result.nickname, QStringLiteral("Alice"));
    EXPECT_EQ(result.session_id, 5001U);
    EXPECT_TRUE(controller.session().isLoggedIn());
}

TEST(QtAuthControllerTest, ErrorResponseEmitsAuthFailed) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::AuthController controller;
    QSignalSpy failed_spy(&controller, &liteim::client::AuthController::authFailed);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    controller.login(QStringLiteral("127.0.0.1"),
                     server.serverPort(),
                     QStringLiteral("alice"),
                     QStringLiteral("bad-secret"));

    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    const auto request = readPacketFromSocket(*server_socket);
    ASSERT_TRUE(writePacket(*server_socket,
                            makeErrorResponse(request.header.seq_id,
                                              QStringLiteral("invalid credentials"))));

    ASSERT_TRUE(waitForSpyCount(failed_spy, 1));
    EXPECT_EQ(failed_spy.takeFirst().at(0).toString(), QStringLiteral("invalid credentials"));
    EXPECT_FALSE(controller.session().isLoggedIn());
}

TEST(QtClientAppTest, LoginSuccessOpensMainWindowAndClosesLoginWindow) {
    liteim::client::LoginWindow login_window;
    QObject context;
    liteim::client::connectLoginWindowToMainWindow(login_window, context);

    login_window.show();
    ASSERT_TRUE(login_window.isVisible());

    liteim::client::AuthResult result;
    result.user_id = 1001;
    result.username = QStringLiteral("alice");
    result.nickname = QStringLiteral("Alice");
    result.session_id = 5001;
    emit login_window.loginSucceeded(result);
    QCoreApplication::processEvents();

    EXPECT_FALSE(login_window.isVisible());

    liteim::client::MainWindow* opened_window = nullptr;
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* main_window = dynamic_cast<liteim::client::MainWindow*>(widget)) {
            opened_window = main_window;
            break;
        }
    }

    ASSERT_NE(opened_window, nullptr);
    EXPECT_TRUE(opened_window->isVisible());
    opened_window->close();
}

TEST(QtMainWindowTest, StartsWithThreeColumnChatLayout) {
    liteim::client::MainWindow window;
    window.resize(1200, 760);
    window.show();
    QCoreApplication::processEvents();

    auto* splitter = window.findChild<QSplitter*>("mainSplitter");
    auto* side_bar = window.findChild<QWidget*>("sideBar");
    auto* middle = window.findChild<QWidget*>("conversationListWidget");
    auto* chat_page = window.findChild<QWidget*>("chatPage");
    ASSERT_NE(splitter, nullptr);
    ASSERT_NE(side_bar, nullptr);
    ASSERT_NE(middle, nullptr);
    ASSERT_NE(chat_page, nullptr);

    EXPECT_NE(window.findChild<QPushButton*>("navMessagesButton"), nullptr);
    EXPECT_NE(window.findChild<QPushButton*>("navContactsButton"), nullptr);
    EXPECT_NE(window.findChild<QPushButton*>("navGroupsButton"), nullptr);
    EXPECT_NE(window.findChild<QPushButton*>("navAgentButton"), nullptr);
    EXPECT_NE(window.findChild<QPushButton*>("navSettingsButton"), nullptr);

    auto* nickname = window.findChild<QLabel*>("currentUserNicknameLabel");
    auto* online_status = window.findChild<QLabel*>("onlineStatusLabel");
    ASSERT_NE(nickname, nullptr);
    ASSERT_NE(online_status, nullptr);
    EXPECT_FALSE(nickname->text().isEmpty());
    EXPECT_TRUE(online_status->text().contains(QStringLiteral("Online")));

    EXPECT_GE(side_bar->width(), 60);
    EXPECT_LE(side_bar->width(), 96);
    EXPECT_GE(middle->width(), 260);
    EXPECT_LE(middle->width(), 380);
    EXPECT_GT(chat_page->width(), middle->width());
}

TEST(QtMainWindowTest, SidebarButtonsSwitchMiddleArea) {
    liteim::client::MainWindow window;
    window.resize(1200, 760);
    window.show();
    QCoreApplication::processEvents();

    auto* title = window.findChild<QLabel*>("conversationSectionTitle");
    auto* list = window.findChild<QListWidget*>("conversationListItems");
    auto* contacts = window.findChild<QPushButton*>("navContactsButton");
    auto* groups = window.findChild<QPushButton*>("navGroupsButton");
    auto* agent = window.findChild<QPushButton*>("navAgentButton");
    ASSERT_NE(title, nullptr);
    ASSERT_NE(list, nullptr);
    ASSERT_NE(contacts, nullptr);
    ASSERT_NE(groups, nullptr);
    ASSERT_NE(agent, nullptr);

    EXPECT_EQ(title->text(), QStringLiteral("Messages"));
    EXPECT_GT(list->count(), 0);

    contacts->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(title->text(), QStringLiteral("Contacts"));
    ASSERT_GT(list->count(), 0);
    EXPECT_TRUE(list->item(0)->text().contains(QStringLiteral("Contacts")));

    groups->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(title->text(), QStringLiteral("Groups"));
    ASSERT_GT(list->count(), 0);
    EXPECT_TRUE(list->item(0)->text().contains(QStringLiteral("Groups")));

    agent->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(title->text(), QStringLiteral("Agent"));
    ASSERT_GT(list->count(), 0);
    EXPECT_TRUE(list->item(0)->text().contains(QStringLiteral("Agent")));
}

TEST(QtMainWindowTest, ResizeKeepsColumnsUsable) {
    liteim::client::MainWindow window;
    window.resize(900, 600);
    window.show();
    QCoreApplication::processEvents();

    auto* side_bar = window.findChild<QWidget*>("sideBar");
    auto* middle = window.findChild<QWidget*>("conversationListWidget");
    auto* chat_page = window.findChild<QWidget*>("chatPage");
    ASSERT_NE(side_bar, nullptr);
    ASSERT_NE(middle, nullptr);
    ASSERT_NE(chat_page, nullptr);

    window.resize(1320, 780);
    QCoreApplication::processEvents();

    EXPECT_GE(side_bar->width(), 60);
    EXPECT_LE(side_bar->width(), 96);
    EXPECT_GE(middle->width(), 260);
    EXPECT_GE(chat_page->width(), 500);
    EXPECT_GE(chat_page->height(), 520);
}
