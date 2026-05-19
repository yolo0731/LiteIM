#include "liteim_client/auth/AuthController.hpp"
#include "liteim_client/app/ClientApp.hpp"
#include "liteim_client/app/ClientRuntime.hpp"
#include "liteim_client/chat/ChatController.hpp"
#include "liteim_client/model/ConversationModel.hpp"
#include "liteim_client/network/ClientSession.hpp"
#include "liteim_client/ui/LoginWindow.hpp"
#include "liteim_client/ui/MainWindow.hpp"
#include "liteim_client/protocol/PacketCodec.hpp"
#include "liteim_client/ui/ChatInputBar.hpp"
#include "liteim_client/ui/ChatPage.hpp"
#include "liteim_client/ui/ContactListWidget.hpp"
#include "liteim_client/ui/MessageBubble.hpp"
#include "liteim_client/ui/RegisterDialog.hpp"
#include "liteim_client/network/TcpClient.hpp"

#include "liteim/protocol/FrameDecoder.hpp"
#include "liteim/protocol/MessageType.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim/protocol/TlvCodec.hpp"

#include <QAbstractSocket>
#include <QAbstractItemView>
#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QHostAddress>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QSpinBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextEdit>
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

liteim::Packet makeWireMessage(liteim::MessageType type,
                               std::uint64_t seq_id,
                               std::uint64_t message_id,
                               std::uint64_t conversation_type,
                               std::uint64_t conversation_id,
                               std::uint64_t sender_id,
                               std::uint64_t receiver_id,
                               const QString& text) {
    liteim::Packet packet;
    packet.header.msg_type = type;
    packet.header.seq_id = seq_id;
    EXPECT_TRUE(liteim::client::PacketCodec::appendUint64Field(
                    liteim::TlvType::MessageId, message_id, packet)
                    .isOk());
    EXPECT_TRUE(liteim::client::PacketCodec::appendUint64Field(
                    liteim::TlvType::ConversationType, conversation_type, packet)
                    .isOk());
    EXPECT_TRUE(liteim::client::PacketCodec::appendUint64Field(
                    liteim::TlvType::ConversationId, conversation_id, packet)
                    .isOk());
    EXPECT_TRUE(liteim::client::PacketCodec::appendUint64Field(
                    liteim::TlvType::SenderId, sender_id, packet)
                    .isOk());
    EXPECT_TRUE(liteim::client::PacketCodec::appendUint64Field(
                    liteim::TlvType::ReceiverId, receiver_id, packet)
                    .isOk());
    EXPECT_TRUE(liteim::client::PacketCodec::appendStringField(
                    liteim::TlvType::MessageText, text, packet)
                    .isOk());
    EXPECT_TRUE(liteim::client::PacketCodec::appendUint64Field(
                    liteim::TlvType::TimestampMs, 1800000000000ULL, packet)
                    .isOk());
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

liteim::TlvMap fieldsFor(const liteim::Packet& packet) {
    liteim::TlvMap fields;
    EXPECT_TRUE(liteim::client::PacketCodec::parseFields(packet, fields).isOk());
    return fields;
}

std::uint64_t uint64Field(const liteim::Packet& packet, liteim::TlvType type) {
    const auto fields = fieldsFor(packet);
    std::uint64_t value = 0;
    EXPECT_TRUE(liteim::client::PacketCodec::getUint64Field(fields, type, value).isOk());
    return value;
}

QString stringField(const liteim::Packet& packet, liteim::TlvType type) {
    const auto fields = fieldsFor(packet);
    QString value;
    EXPECT_TRUE(liteim::client::PacketCodec::getStringField(fields, type, value).isOk());
    return value;
}

liteim::client::ChatMessage makeChatMessage(const QString& conversation_id,
                                            quint64 message_id,
                                            liteim::client::MessageDirection direction,
                                            const QString& text) {
    liteim::client::ChatMessage message;
    message.conversation_id = conversation_id;
    message.message_id = message_id;
    message.sender_id = direction == liteim::client::MessageDirection::Outgoing ? 1001 : 1002;
    message.sender_name =
        direction == liteim::client::MessageDirection::Outgoing ? QStringLiteral("Alice")
                                                                : QStringLiteral("Bob");
    message.text = text;
    message.sent_at = QDateTime::fromString(QStringLiteral("2026-05-19T10:30:00"), Qt::ISODate);
    message.direction = direction;
    message.status = direction == liteim::client::MessageDirection::Outgoing
                         ? liteim::client::MessageSendStatus::Sending
                         : liteim::client::MessageSendStatus::Succeeded;
    return message;
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
    EXPECT_NE(window.findChild<QPushButton*>("navSettingsButton"), nullptr);
    EXPECT_EQ(window.findChild<QPushButton*>("navAgentButton"), nullptr);

    auto* nickname = window.findChild<QLabel*>("currentUserNicknameLabel");
    auto* online_status = window.findChild<QLabel*>("onlineStatusLabel");
    ASSERT_NE(nickname, nullptr);
    ASSERT_NE(online_status, nullptr);
    EXPECT_FALSE(nickname->text().isEmpty());
    EXPECT_FALSE(online_status->text().isEmpty());

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
    auto* conversation_list = window.findChild<QListView*>("conversationListItems");
    auto* contacts = window.findChild<QPushButton*>("navContactsButton");
    auto* groups = window.findChild<QPushButton*>("navGroupsButton");
    auto* settings = window.findChild<QPushButton*>("navSettingsButton");
    ASSERT_NE(title, nullptr);
    ASSERT_NE(conversation_list, nullptr);
    ASSERT_NE(contacts, nullptr);
    ASSERT_NE(groups, nullptr);
    ASSERT_NE(settings, nullptr);

    EXPECT_EQ(title->text(), QStringLiteral("Messages"));
    ASSERT_NE(conversation_list->model(), nullptr);
    EXPECT_GT(conversation_list->model()->rowCount(), 0);

    contacts->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(title->text(), QStringLiteral("Contacts"));
    auto* contact_list = window.findChild<QListWidget*>("contactListItems");
    ASSERT_NE(contact_list, nullptr);
    ASSERT_GT(contact_list->count(), 0);
    EXPECT_TRUE(contact_list->item(0)->text().contains(QStringLiteral("Alice")));

    groups->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(title->text(), QStringLiteral("Groups"));
    auto* group_list = window.findChild<QListWidget*>("groupListItems");
    ASSERT_NE(group_list, nullptr);
    ASSERT_GT(group_list->count(), 0);
    EXPECT_TRUE(group_list->item(0)->text().contains(QStringLiteral("LiteIM Dev Group")));

    settings->click();
    QCoreApplication::processEvents();
    EXPECT_EQ(title->text(), QStringLiteral("Settings"));
    auto* settings_list = window.findChild<QListWidget*>("settingsListItems");
    ASSERT_NE(settings_list, nullptr);
    ASSERT_GT(settings_list->count(), 0);
    EXPECT_TRUE(settings_list->item(0)->text().contains(QStringLiteral("Settings")));
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

TEST(QtConversationModelTest, IncomingMessageUpdatesSummaryAndUnreadCount) {
    liteim::client::ConversationModel model;
    model.setConversations({
        {QStringLiteral("private:1002"),
         liteim::client::ConversationKind::Private,
         QStringLiteral("Bob"),
         QStringLiteral("old message"),
         QStringLiteral("09:30"),
         QStringLiteral("B"),
         1},
    });

    model.applyIncomingMessage({QStringLiteral("private:1002"),
                                liteim::client::ConversationKind::Private,
                                QStringLiteral("Bob"),
                                QStringLiteral("new message"),
                                QStringLiteral("09:41"),
                                QStringLiteral("B")});

    ASSERT_EQ(model.rowCount(), 1);
    const auto index = model.index(0, 0);
    EXPECT_EQ(model.data(index, liteim::client::ConversationModel::TitleRole).toString(),
              QStringLiteral("Bob"));
    EXPECT_EQ(model.data(index, liteim::client::ConversationModel::LastMessageRole).toString(),
              QStringLiteral("new message"));
    EXPECT_EQ(model.data(index, liteim::client::ConversationModel::TimestampRole).toString(),
              QStringLiteral("09:41"));
    EXPECT_EQ(model.data(index, liteim::client::ConversationModel::UnreadCountRole).toInt(), 2);
    EXPECT_EQ(model.totalUnreadCount(), 2);
}

TEST(QtConversationModelTest, ActiveConversationDoesNotIncreaseUnreadAndCanBeMarkedRead) {
    liteim::client::ConversationModel model;
    model.setConversations({
        {QStringLiteral("private:1002"),
         liteim::client::ConversationKind::Private,
         QStringLiteral("Bob"),
         QStringLiteral("old message"),
         QStringLiteral("09:30"),
         QStringLiteral("B"),
         3},
    });

    model.applyIncomingMessage({QStringLiteral("private:1002"),
                                liteim::client::ConversationKind::Private,
                                QStringLiteral("Bob"),
                                QStringLiteral("visible message"),
                                QStringLiteral("09:42"),
                                QStringLiteral("B")},
                               QStringLiteral("private:1002"));

    auto index = model.index(0, 0);
    EXPECT_EQ(model.data(index, liteim::client::ConversationModel::UnreadCountRole).toInt(), 3);

    model.markConversationRead(QStringLiteral("private:1002"));

    index = model.index(0, 0);
    EXPECT_EQ(model.data(index, liteim::client::ConversationModel::UnreadCountRole).toInt(), 0);
    EXPECT_EQ(model.totalUnreadCount(), 0);
}

TEST(QtConversationModelTest, IncomingMessageMovesConversationToTopOrCreatesOne) {
    liteim::client::ConversationModel model;
    model.setConversations({
        {QStringLiteral("private:1001"),
         liteim::client::ConversationKind::Private,
         QStringLiteral("Alice"),
         QStringLiteral("first"),
         QStringLiteral("09:10"),
         QStringLiteral("A"),
         0},
        {QStringLiteral("group:2001"),
         liteim::client::ConversationKind::Group,
         QStringLiteral("LiteIM Dev Group"),
         QStringLiteral("group old"),
         QStringLiteral("09:00"),
         QStringLiteral("G"),
         0},
    });

    model.applyIncomingMessage({QStringLiteral("group:2001"),
                                liteim::client::ConversationKind::Group,
                                QStringLiteral("LiteIM Dev Group"),
                                QStringLiteral("group new"),
                                QStringLiteral("10:00"),
                                QStringLiteral("G")});

    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.data(model.index(0, 0), liteim::client::ConversationModel::ConversationIdRole)
                  .toString(),
              QStringLiteral("group:2001"));
    EXPECT_EQ(model.data(model.index(0, 0), liteim::client::ConversationModel::UnreadCountRole)
                  .toInt(),
              1);

    model.applyIncomingMessage({QStringLiteral("private:3001"),
                                liteim::client::ConversationKind::Private,
                                QStringLiteral("PersonaAgent"),
                                QStringLiteral("future bot account placeholder"),
                                QStringLiteral("10:01"),
                                QStringLiteral("P")});

    ASSERT_EQ(model.rowCount(), 3);
    EXPECT_EQ(model.data(model.index(0, 0), liteim::client::ConversationModel::ConversationIdRole)
                  .toString(),
              QStringLiteral("private:3001"));
    EXPECT_EQ(model.data(model.index(0, 0), liteim::client::ConversationModel::UnreadCountRole)
                  .toInt(),
              1);
}

TEST(QtContactListWidgetTest, RendersContactsWithOnlineStateAndUnreadBadge) {
    liteim::client::ContactListWidget widget(QStringLiteral("contactListItems"), nullptr);
    widget.setItems({
        {QStringLiteral("Alice"), QStringLiteral("Online"), QStringLiteral("A"), true, 0},
        {QStringLiteral("PersonaAgent"),
         QStringLiteral("Future BotClient account"),
         QStringLiteral("P"),
         false,
         2},
    });

    auto* list = widget.findChild<QListWidget*>("contactListItems");
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 2);
    EXPECT_TRUE(list->item(0)->text().contains(QStringLiteral("Alice")));
    EXPECT_TRUE(list->item(0)->text().contains(QStringLiteral("Online")));
    EXPECT_TRUE(list->item(1)->text().contains(QStringLiteral("PersonaAgent")));
    EXPECT_TRUE(list->item(1)->text().contains(QStringLiteral("2")));
}

TEST(QtMainWindowStep49Test, ShowsModelBackedConversationContactAndGroupLists) {
    liteim::client::MainWindow window;
    window.resize(1200, 760);
    window.show();
    QCoreApplication::processEvents();

    auto* conversation_list = window.findChild<QListView*>("conversationListItems");
    ASSERT_NE(conversation_list, nullptr);
    ASSERT_NE(conversation_list->model(), nullptr);
    ASSERT_GT(conversation_list->model()->rowCount(), 0);
    const auto first = conversation_list->model()->index(0, 0);
    EXPECT_FALSE(conversation_list->model()
                     ->data(first, liteim::client::ConversationModel::TitleRole)
                     .toString()
                     .isEmpty());
    EXPECT_GE(conversation_list->model()
                  ->data(first, liteim::client::ConversationModel::UnreadCountRole)
                  .toInt(),
              0);

    auto* contacts = window.findChild<QPushButton*>("navContactsButton");
    ASSERT_NE(contacts, nullptr);
    contacts->click();
    QCoreApplication::processEvents();

    auto* contact_list = window.findChild<QListWidget*>("contactListItems");
    ASSERT_NE(contact_list, nullptr);
    ASSERT_GT(contact_list->count(), 0);
    bool has_agent_contact = false;
    for (int i = 0; i < contact_list->count(); ++i) {
        has_agent_contact = has_agent_contact ||
                            contact_list->item(i)->text().contains(QStringLiteral("PersonaAgent"));
    }
    EXPECT_TRUE(has_agent_contact);

    auto* groups = window.findChild<QPushButton*>("navGroupsButton");
    ASSERT_NE(groups, nullptr);
    groups->click();
    QCoreApplication::processEvents();

    auto* group_list = window.findChild<QListWidget*>("groupListItems");
    ASSERT_NE(group_list, nullptr);
    ASSERT_GT(group_list->count(), 0);
    EXPECT_TRUE(group_list->item(0)->text().contains(QStringLiteral("members")));
}

TEST(QtChatPageTest, OpenConversationRequestsRecentHistory) {
    liteim::client::ChatPage page;
    QSignalSpy history_spy(&page, &liteim::client::ChatPage::historyRequested);

    page.openConversation(QStringLiteral("private:1002"),
                          QStringLiteral("Bob"),
                          liteim::client::ConversationKind::Private);

    ASSERT_EQ(history_spy.count(), 1);
    EXPECT_EQ(history_spy.at(0).at(0).toString(), QStringLiteral("private:1002"));
    EXPECT_EQ(history_spy.at(0).at(1).toULongLong(), 0ULL);

    auto* title = page.findChild<QLabel*>("chatTitleLabel");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("Bob"));
}

TEST(QtChatPageTest, SendTextAppendsOutgoingBubbleAndRejectsEmptyInput) {
    liteim::client::ChatPage page;
    page.openConversation(QStringLiteral("private:1002"),
                          QStringLiteral("Bob"),
                          liteim::client::ConversationKind::Private);
    QSignalSpy send_spy(&page, &liteim::client::ChatPage::sendMessageRequested);

    auto* input = page.findChild<QTextEdit*>("chatInputEdit");
    auto* send_button = page.findChild<QPushButton*>("chatSendButton");
    ASSERT_NE(input, nullptr);
    ASSERT_NE(send_button, nullptr);

    input->setPlainText(QStringLiteral("   \n"));
    QCoreApplication::processEvents();
    EXPECT_FALSE(send_button->isEnabled());
    send_button->click();
    EXPECT_EQ(send_spy.count(), 0);

    input->setPlainText(QStringLiteral("hello bob"));
    QCoreApplication::processEvents();
    ASSERT_TRUE(send_button->isEnabled());
    send_button->click();

    ASSERT_EQ(send_spy.count(), 1);
    EXPECT_EQ(send_spy.at(0).at(0).toString(), QStringLiteral("private:1002"));
    EXPECT_EQ(send_spy.at(0).at(1).toString(), QStringLiteral("hello bob"));

    const auto bubbles = page.findChildren<liteim::client::MessageBubble*>("messageBubble");
    ASSERT_EQ(bubbles.size(), 1);
    EXPECT_EQ(bubbles.front()->property("messageDirection").toString(), QStringLiteral("outgoing"));
    EXPECT_EQ(bubbles.front()->property("messageStatus").toString(), QStringLiteral("sending"));
    auto* text = bubbles.front()->findChild<QLabel*>("messageBubbleText");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text(), QStringLiteral("hello bob"));
}

TEST(QtChatPageTest, IncomingPrivatePushAppearsAsLeftBubble) {
    liteim::client::ChatPage page;
    page.openConversation(QStringLiteral("private:1002"),
                          QStringLiteral("Bob"),
                          liteim::client::ConversationKind::Private);

    page.appendMessage(makeChatMessage(QStringLiteral("private:1002"),
                                       42,
                                       liteim::client::MessageDirection::Incoming,
                                       QStringLiteral("hi alice")));

    const auto bubbles = page.findChildren<liteim::client::MessageBubble*>("messageBubble");
    ASSERT_EQ(bubbles.size(), 1);
    EXPECT_EQ(bubbles.front()->property("messageDirection").toString(), QStringLiteral("incoming"));
    auto* text = bubbles.front()->findChild<QLabel*>("messageBubbleText");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text(), QStringLiteral("hi alice"));
}

TEST(QtChatPageTest, GroupIncomingMessageShowsSenderName) {
    liteim::client::ChatPage page;
    page.openConversation(QStringLiteral("group:2001"),
                          QStringLiteral("LiteIM Dev Group"),
                          liteim::client::ConversationKind::Group);

    auto message = makeChatMessage(QStringLiteral("group:2001"),
                                   50,
                                   liteim::client::MessageDirection::Incoming,
                                   QStringLiteral("group hello"));
    message.sender_name = QStringLiteral("Charlie");
    page.appendMessage(message);

    const auto bubbles = page.findChildren<liteim::client::MessageBubble*>("messageBubble");
    ASSERT_EQ(bubbles.size(), 1);
    auto* sender = bubbles.front()->findChild<QLabel*>("messageBubbleSender");
    ASSERT_NE(sender, nullptr);
    EXPECT_FALSE(sender->isHidden());
    EXPECT_EQ(sender->text(), QStringLiteral("Charlie"));
}

TEST(QtChatPageTest, LoadEarlierRequestsBeforeEarliestMessageId) {
    liteim::client::ChatPage page;
    page.openConversation(QStringLiteral("private:1002"),
                          QStringLiteral("Bob"),
                          liteim::client::ConversationKind::Private);
    page.setMessages({makeChatMessage(QStringLiteral("private:1002"),
                                      10,
                                      liteim::client::MessageDirection::Incoming,
                                      QStringLiteral("older")),
                      makeChatMessage(QStringLiteral("private:1002"),
                                      15,
                                      liteim::client::MessageDirection::Outgoing,
                                      QStringLiteral("newer"))});

    QSignalSpy history_spy(&page, &liteim::client::ChatPage::historyRequested);
    auto* load_earlier = page.findChild<QPushButton*>("loadEarlierMessagesButton");
    ASSERT_NE(load_earlier, nullptr);
    load_earlier->click();

    ASSERT_EQ(history_spy.count(), 1);
    EXPECT_EQ(history_spy.at(0).at(0).toString(), QStringLiteral("private:1002"));
    EXPECT_EQ(history_spy.at(0).at(1).toULongLong(), 10ULL);
}

TEST(QtChatInputBarTest, EnterSendsAndShiftEnterInsertsNewLine) {
    liteim::client::ChatInputBar input_bar;
    QSignalSpy send_spy(&input_bar, &liteim::client::ChatInputBar::sendRequested);

    auto* input = input_bar.findChild<QTextEdit*>("chatInputEdit");
    auto* send_button = input_bar.findChild<QPushButton*>("chatSendButton");
    ASSERT_NE(input, nullptr);
    ASSERT_NE(send_button, nullptr);
    EXPECT_FALSE(send_button->isEnabled());

    input->setPlainText(QStringLiteral("hello"));
    QCoreApplication::processEvents();
    ASSERT_TRUE(send_button->isEnabled());
    QTest::keyClick(input, Qt::Key_Return);

    ASSERT_EQ(send_spy.count(), 1);
    EXPECT_EQ(send_spy.at(0).at(0).toString(), QStringLiteral("hello"));
    EXPECT_TRUE(input->toPlainText().isEmpty());

    input->setPlainText(QStringLiteral("line one"));
    input->moveCursor(QTextCursor::End);
    QCoreApplication::processEvents();
    QTest::keyClick(input, Qt::Key_Return, Qt::ShiftModifier);

    EXPECT_EQ(send_spy.count(), 1);
    EXPECT_EQ(input->toPlainText(), QStringLiteral("line one\n"));
}

TEST(QtChatControllerTest, SendsFriendAndGroupManagementRequests) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::ClientRuntime runtime;
    liteim::client::ChatController controller(runtime);
    QSignalSpy connected_spy(&runtime.client(), &liteim::client::TcpClient::connected);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    runtime.client().connectToHost(QStringLiteral("127.0.0.1"), server.serverPort());
    ASSERT_TRUE(waitForSpyCount(connected_spy, 1));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    ASSERT_TRUE(controller.addFriend(1002).isOk());
    auto packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::AddFriendRequest);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::TargetUserId), 1002U);

    ASSERT_TRUE(controller.createGroup(QStringLiteral("project room")).isOk());
    packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::CreateGroupRequest);
    EXPECT_EQ(stringField(packet, liteim::TlvType::GroupName), QStringLiteral("project room"));

    ASSERT_TRUE(controller.joinGroup(2001).isOk());
    packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::JoinGroupRequest);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::GroupId), 2001U);
}

TEST(QtMainWindowStep51Test, ContactSelectionOpensPrivateChatAndSendsPrivatePacket) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::ClientRuntime runtime;
    runtime.session().markLoggedIn(1001, {}, QStringLiteral("5001"));
    liteim::client::MainWindow window(runtime);
    QSignalSpy connected_spy(&runtime.client(), &liteim::client::TcpClient::connected);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    runtime.client().connectToHost(QStringLiteral("127.0.0.1"), server.serverPort());
    ASSERT_TRUE(waitForSpyCount(connected_spy, 1));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    auto* contacts = window.findChild<QPushButton*>("navContactsButton");
    ASSERT_NE(contacts, nullptr);
    contacts->click();
    auto* contact_list = window.findChild<QListWidget*>("contactListItems");
    ASSERT_NE(contact_list, nullptr);
    ASSERT_GE(contact_list->count(), 2);
    contact_list->setCurrentRow(1);
    emit contact_list->itemClicked(contact_list->item(1));

    auto packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::HistoryRequest);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::ConversationType), 1U);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::ConversationId), 10011002U);

    auto* title = window.findChild<QLabel*>("chatTitleLabel");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("Bob"));

    auto* input = window.findChild<QTextEdit*>("chatInputEdit");
    auto* send_button = window.findChild<QPushButton*>("chatSendButton");
    ASSERT_NE(input, nullptr);
    ASSERT_NE(send_button, nullptr);
    input->setPlainText(QStringLiteral("hello bob"));
    send_button->click();

    packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::PrivateMessageRequest);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::ReceiverId), 1002U);
    EXPECT_EQ(stringField(packet, liteim::TlvType::MessageText), QStringLiteral("hello bob"));
}

TEST(QtMainWindowStep51Test, GroupSelectionOpensGroupChatAndSendsGroupPacket) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::ClientRuntime runtime;
    runtime.session().markLoggedIn(1001, {}, QStringLiteral("5001"));
    liteim::client::MainWindow window(runtime);
    QSignalSpy connected_spy(&runtime.client(), &liteim::client::TcpClient::connected);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    runtime.client().connectToHost(QStringLiteral("127.0.0.1"), server.serverPort());
    ASSERT_TRUE(waitForSpyCount(connected_spy, 1));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    auto* groups = window.findChild<QPushButton*>("navGroupsButton");
    ASSERT_NE(groups, nullptr);
    groups->click();
    auto* group_list = window.findChild<QListWidget*>("groupListItems");
    ASSERT_NE(group_list, nullptr);
    ASSERT_GT(group_list->count(), 0);
    group_list->setCurrentRow(0);
    emit group_list->itemClicked(group_list->item(0));

    auto packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::HistoryRequest);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::ConversationType), 2U);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::ConversationId), 2001U);

    auto* input = window.findChild<QTextEdit*>("chatInputEdit");
    auto* send_button = window.findChild<QPushButton*>("chatSendButton");
    ASSERT_NE(input, nullptr);
    ASSERT_NE(send_button, nullptr);
    input->setPlainText(QStringLiteral("hello team"));
    send_button->click();

    packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::GroupMessageRequest);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::GroupId), 2001U);
    EXPECT_EQ(stringField(packet, liteim::TlvType::MessageText), QStringLiteral("hello team"));
}

TEST(QtMainWindowStep51Test, PersonaAgentContactIsOrdinaryPrivateChat) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::ClientRuntime runtime;
    runtime.session().markLoggedIn(1001, {}, QStringLiteral("5001"));
    liteim::client::MainWindow window(runtime);
    QSignalSpy connected_spy(&runtime.client(), &liteim::client::TcpClient::connected);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    runtime.client().connectToHost(QStringLiteral("127.0.0.1"), server.serverPort());
    ASSERT_TRUE(waitForSpyCount(connected_spy, 1));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    auto* contacts = window.findChild<QPushButton*>("navContactsButton");
    ASSERT_NE(contacts, nullptr);
    contacts->click();
    auto* contact_list = window.findChild<QListWidget*>("contactListItems");
    ASSERT_NE(contact_list, nullptr);

    int agent_row = -1;
    for (int row = 0; row < contact_list->count(); ++row) {
        if (contact_list->item(row)->text().contains(QStringLiteral("PersonaAgent"))) {
            agent_row = row;
            break;
        }
    }
    ASSERT_GE(agent_row, 0);
    contact_list->setCurrentRow(agent_row);
    emit contact_list->itemClicked(contact_list->item(agent_row));

    auto packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::HistoryRequest);

    auto* title = window.findChild<QLabel*>("chatTitleLabel");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text(), QStringLiteral("PersonaAgent"));

    auto* input = window.findChild<QTextEdit*>("chatInputEdit");
    auto* send_button = window.findChild<QPushButton*>("chatSendButton");
    ASSERT_NE(input, nullptr);
    ASSERT_NE(send_button, nullptr);
    input->setPlainText(QStringLiteral("hello agent"));
    send_button->click();

    packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::PrivateMessageRequest);
    EXPECT_EQ(uint64Field(packet, liteim::TlvType::ReceiverId), 3001U);
    EXPECT_EQ(stringField(packet, liteim::TlvType::MessageText), QStringLiteral("hello agent"));
}

TEST(QtMainWindowStep51Test, PrivatePushFromPersonaAgentAppearsInCurrentChat) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::ClientRuntime runtime;
    runtime.session().markLoggedIn(1001, {}, QStringLiteral("5001"));
    liteim::client::MainWindow window(runtime);
    QSignalSpy connected_spy(&runtime.client(), &liteim::client::TcpClient::connected);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    runtime.client().connectToHost(QStringLiteral("127.0.0.1"), server.serverPort());
    ASSERT_TRUE(waitForSpyCount(connected_spy, 1));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    auto* contacts = window.findChild<QPushButton*>("navContactsButton");
    ASSERT_NE(contacts, nullptr);
    contacts->click();
    auto* contact_list = window.findChild<QListWidget*>("contactListItems");
    ASSERT_NE(contact_list, nullptr);
    int agent_row = -1;
    for (int row = 0; row < contact_list->count(); ++row) {
        if (contact_list->item(row)->text().contains(QStringLiteral("PersonaAgent"))) {
            agent_row = row;
            break;
        }
    }
    ASSERT_GE(agent_row, 0);
    emit contact_list->itemClicked(contact_list->item(agent_row));
    static_cast<void>(readPacketFromSocket(*server_socket));

    const auto push = makeWireMessage(liteim::MessageType::PrivateMessagePush,
                                      0,
                                      9001,
                                      1,
                                      10013001,
                                      3001,
                                      1001,
                                      QStringLiteral("ordinary private reply"));
    ASSERT_TRUE(writePacket(*server_socket, push));
    QCoreApplication::processEvents();

    const auto bubbles = window.findChildren<liteim::client::MessageBubble*>("messageBubble");
    ASSERT_EQ(bubbles.size(), 1);
    EXPECT_EQ(bubbles.front()->property("messageDirection").toString(), QStringLiteral("incoming"));
    auto* text = bubbles.front()->findChild<QLabel*>("messageBubbleText");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text(), QStringLiteral("ordinary private reply"));
}

TEST(QtClientRuntimeStep52Test, HeartbeatTimerSendsHeartbeatRequest) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::ClientRuntime runtime;
    runtime.session().markLoggedIn(1001, {}, QStringLiteral("5001"));
    QSignalSpy connected_spy(&runtime.client(), &liteim::client::TcpClient::connected);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    runtime.setConnectionEndpoint(QStringLiteral("127.0.0.1"), server.serverPort());
    runtime.client().connectToHost(QStringLiteral("127.0.0.1"), server.serverPort());
    ASSERT_TRUE(waitForSpyCount(connected_spy, 1));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    runtime.startHeartbeat(10);

    QTRY_VERIFY(server_socket->bytesAvailable() >=
                static_cast<qint64>(liteim::kPacketHeaderSize));
    const auto packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::HeartbeatRequest);
    EXPECT_EQ(runtime.heartbeatIntervalMs(), 10);
}

TEST(QtClientRuntimeStep52Test, DisconnectedSocketReportsOfflineAndManualReconnectWorks) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::ClientRuntime runtime;
    runtime.session().markLoggedIn(1001, {}, QStringLiteral("5001"));
    runtime.setAutoReconnectEnabled(false);
    runtime.setConnectionEndpoint(QStringLiteral("127.0.0.1"), server.serverPort());

    QSignalSpy connected_spy(&runtime.client(), &liteim::client::TcpClient::connected);
    QSignalSpy status_spy(&runtime, &liteim::client::ClientRuntime::connectionStatusChanged);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    runtime.reconnect();
    ASSERT_TRUE(waitForSpyCount(connected_spy, 1));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    server_socket->disconnectFromHost();
    ASSERT_TRUE(waitForSpyCount(status_spy, 3));
    EXPECT_FALSE(runtime.isOnline());
    EXPECT_TRUE(runtime.connectionStatusText().contains(QStringLiteral("Offline")));

    runtime.reconnect();
    ASSERT_TRUE(waitForSpyCount(connected_spy, 2));
    ASSERT_TRUE(waitForSpyCount(server_spy, 2) || server.hasPendingConnections());
    EXPECT_TRUE(runtime.isOnline());
}

TEST(QtClientRuntimeStep52Test, AutoReconnectRunsOnlyOncePerDisconnect) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::ClientRuntime runtime;
    runtime.session().markLoggedIn(1001, {}, QStringLiteral("5001"));
    runtime.setConnectionEndpoint(QStringLiteral("127.0.0.1"), server.serverPort());
    runtime.setAutoReconnectEnabled(true);
    runtime.setReconnectDelayMs(1);

    QSignalSpy connected_spy(&runtime.client(), &liteim::client::TcpClient::connected);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    runtime.reconnect();
    ASSERT_TRUE(waitForSpyCount(connected_spy, 1));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> first_socket(server.nextPendingConnection());
    ASSERT_NE(first_socket, nullptr);

    first_socket->disconnectFromHost();
    ASSERT_TRUE(waitForSpyCount(connected_spy, 2));
    ASSERT_TRUE(waitForSpyCount(server_spy, 2) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> second_socket(server.nextPendingConnection());
    ASSERT_NE(second_socket, nullptr);

    second_socket->disconnectFromHost();
    QTest::qWait(80);
    EXPECT_EQ(connected_spy.count(), 2);
}

TEST(QtClientRuntimeStep52Test, AuthControllerLeavesChatResponsesForChatController) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::AuthController auth_controller;
    liteim::client::ChatController chat_controller(auth_controller.runtime());
    QSignalSpy login_spy(&auth_controller, &liteim::client::AuthController::loginSucceeded);
    QSignalSpy auth_failed_spy(&auth_controller, &liteim::client::AuthController::authFailed);
    QSignalSpy delivered_spy(&chat_controller, &liteim::client::ChatController::messageDelivered);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);

    auth_controller.login(QStringLiteral("127.0.0.1"),
                          server.serverPort(),
                          QStringLiteral("alice"),
                          QStringLiteral("secret"));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);

    auto packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::LoginRequest);
    ASSERT_TRUE(writePacket(*server_socket,
                            makeAuthResponse(liteim::MessageType::LoginResponse,
                                             packet.header.seq_id)));
    ASSERT_TRUE(waitForSpyCount(login_spy, 1));

    ASSERT_TRUE(chat_controller.sendPrivateMessage(1002, QStringLiteral("hello bob")).isOk());
    packet = readPacketFromSocket(*server_socket);
    EXPECT_EQ(packet.header.msg_type, liteim::MessageType::PrivateMessageRequest);

    ASSERT_TRUE(writePacket(*server_socket,
                            makeWireMessage(liteim::MessageType::PrivateMessageResponse,
                                            packet.header.seq_id,
                                            9100,
                                            1,
                                            10011002,
                                            1001,
                                            1002,
                                            QStringLiteral("hello bob"))));

    ASSERT_TRUE(waitForSpyCount(delivered_spy, 1));
    EXPECT_EQ(auth_failed_spy.count(), 0);
}

TEST(QtMainWindowStep52Test, StatusBarShowsOfflineAndReconnectButtonReconnects) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    liteim::client::ClientRuntime runtime;
    runtime.session().markLoggedIn(1001, {}, QStringLiteral("5001"));
    runtime.setAutoReconnectEnabled(false);
    runtime.setConnectionEndpoint(QStringLiteral("127.0.0.1"), server.serverPort());

    liteim::client::MainWindow window(runtime);
    auto* status_label = window.findChild<QLabel*>("connectionStatusLabel");
    auto* reconnect_button = window.findChild<QPushButton*>("reconnectButton");
    ASSERT_NE(status_label, nullptr);
    ASSERT_NE(reconnect_button, nullptr);

    QSignalSpy connected_spy(&runtime.client(), &liteim::client::TcpClient::connected);
    QSignalSpy server_spy(&server, &QTcpServer::newConnection);
    runtime.reconnect();
    ASSERT_TRUE(waitForSpyCount(connected_spy, 1));
    ASSERT_TRUE(waitForSpyCount(server_spy, 1) || server.hasPendingConnections());
    std::unique_ptr<QTcpSocket> server_socket(server.nextPendingConnection());
    ASSERT_NE(server_socket, nullptr);
    EXPECT_TRUE(status_label->text().contains(QStringLiteral("Online")));
    EXPECT_FALSE(reconnect_button->isEnabled());

    server_socket->disconnectFromHost();
    QTRY_VERIFY(status_label->text().contains(QStringLiteral("Offline")));
    EXPECT_TRUE(reconnect_button->isEnabled());

    reconnect_button->click();
    ASSERT_TRUE(waitForSpyCount(connected_spy, 2));
    EXPECT_TRUE(status_label->text().contains(QStringLiteral("Online")));
}

TEST(QtMainWindowStep52Test, SendFailureMarksOutgoingBubbleFailed) {
    liteim::client::ClientRuntime runtime;
    runtime.session().markLoggedIn(1001, {}, QStringLiteral("5001"));
    liteim::client::MainWindow window(runtime);

    auto* contacts = window.findChild<QPushButton*>("navContactsButton");
    ASSERT_NE(contacts, nullptr);
    contacts->click();
    auto* contact_list = window.findChild<QListWidget*>("contactListItems");
    ASSERT_NE(contact_list, nullptr);
    contact_list->setCurrentRow(1);
    emit contact_list->itemClicked(contact_list->item(1));

    auto* input = window.findChild<QTextEdit*>("chatInputEdit");
    auto* send_button = window.findChild<QPushButton*>("chatSendButton");
    ASSERT_NE(input, nullptr);
    ASSERT_NE(send_button, nullptr);
    input->setPlainText(QStringLiteral("message while offline"));
    send_button->click();

    const auto bubbles = window.findChildren<liteim::client::MessageBubble*>("messageBubble");
    ASSERT_EQ(bubbles.size(), 1);
    EXPECT_EQ(bubbles.front()->property("messageStatus").toString(), QStringLiteral("failed"));
    auto* status = bubbles.front()->findChild<QLabel*>("messageBubbleStatus");
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->text(), QStringLiteral("Failed"));
}

TEST(QtLoginWindowStep52Test, SavesAndReloadsRecentConnectionSettings) {
    QTemporaryDir settings_dir;
    ASSERT_TRUE(settings_dir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("LiteIMStep52Test"));
    QCoreApplication::setApplicationName(QStringLiteral("LiteIMStep52Test"));

    {
        liteim::client::LoginWindow window;
        auto* host = window.findChild<QLineEdit*>("serverHostEdit");
        auto* port = window.findChild<QSpinBox*>("serverPortSpinBox");
        auto* username = window.findChild<QLineEdit*>("usernameEdit");
        auto* password = window.findChild<QLineEdit*>("passwordEdit");
        auto* login = window.findChild<QPushButton*>("loginButton");
        ASSERT_NE(host, nullptr);
        ASSERT_NE(port, nullptr);
        ASSERT_NE(username, nullptr);
        ASSERT_NE(password, nullptr);
        ASSERT_NE(login, nullptr);

        host->setText(QStringLiteral("192.0.2.52"));
        port->setValue(9052);
        username->setText(QStringLiteral("step52-user"));
        password->setText(QStringLiteral("secret"));
        ASSERT_TRUE(login->isEnabled());
        login->click();
    }

    liteim::client::LoginWindow reloaded;
    auto* reloaded_host = reloaded.findChild<QLineEdit*>("serverHostEdit");
    auto* reloaded_port = reloaded.findChild<QSpinBox*>("serverPortSpinBox");
    auto* reloaded_username = reloaded.findChild<QLineEdit*>("usernameEdit");
    ASSERT_NE(reloaded_host, nullptr);
    ASSERT_NE(reloaded_port, nullptr);
    ASSERT_NE(reloaded_username, nullptr);

    EXPECT_EQ(reloaded_host->text(), QStringLiteral("192.0.2.52"));
    EXPECT_EQ(reloaded_port->value(), 9052);
    EXPECT_EQ(reloaded_username->text(), QStringLiteral("step52-user"));
}
