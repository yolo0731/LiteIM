#pragma once

#include "liteim/base/Status.hpp"
#include "liteim/protocol/Packet.hpp"
#include "liteim_client/model/ConversationModel.hpp"
#include "liteim_client/network/ClientSession.hpp"
#include "liteim_client/ui/MessageBubble.hpp"

#include <QObject>
#include <QString>
#include <QVector>

#include <cstdint>

namespace liteim::client {

class ClientRuntime;

class ChatController final : public QObject {
    Q_OBJECT

public:
    explicit ChatController(ClientRuntime& runtime, QObject* parent = nullptr);

    Status addFriend(std::uint64_t target_user_id);
    Status createGroup(const QString& group_name);
    Status joinGroup(std::uint64_t group_id);
    Status sendPrivateMessage(std::uint64_t receiver_id, const QString& text);
    Status sendGroupMessage(std::uint64_t group_id, const QString& text);
    Status requestHistory(ConversationKind kind,
                          std::uint64_t conversation_id,
                          std::uint64_t before_message_id,
                          std::uint64_t limit = 20);
    void reportFailure(const QString& message);

    static std::uint64_t privateConversationId(std::uint64_t left_user_id,
                                               std::uint64_t right_user_id) noexcept;

signals:
    void messageReceived(liteim::client::ChatMessage message);
    void messageDelivered(liteim::client::ChatMessage message);
    void historyLoaded(QVector<liteim::client::ChatMessage> messages);
    void requestFailed(QString message);
    void friendAdded(std::uint64_t user_id, QString title);
    void groupCreated(std::uint64_t group_id, QString title);
    void groupJoined(std::uint64_t group_id, QString title);

private:
    Status sendPacket(MessageType type, Packet& packet);
    void handlePacketReceived(const Packet& packet);
    void handlePendingResponse(const Packet& packet, const PendingRequest& pending);
    Status parseMessagePacket(const Packet& packet, ChatMessage& message) const;
    Status parseRepeatedMessages(const Packet& packet, QVector<ChatMessage>& messages) const;
    QString parseErrorMessage(const Packet& packet) const;

    ClientRuntime& runtime_;
};

}  // namespace liteim::client

Q_DECLARE_METATYPE(liteim::client::ChatMessage)
Q_DECLARE_METATYPE(QVector<liteim::client::ChatMessage>)
