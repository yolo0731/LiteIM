#include "liteim_client/chat/ChatController.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim_client/app/ClientRuntime.hpp"
#include "liteim_client/protocol/PacketCodec.hpp"

#include <QDateTime>
#include <QMetaType>

#include <algorithm>
#include <limits>

namespace liteim::client {
namespace {

constexpr std::uint64_t kSmallUserIdConversationBase = 10000;
constexpr std::uint64_t kConversationTypePrivate = 1;
constexpr std::uint64_t kConversationTypeGroup = 2;

QString statusMessage(const Status& status) {
    return QString::fromStdString(status.message());
}

Status invalidArgument(const char* message) {
    return Status::error(ErrorCode::InvalidArgument, message);
}

std::uint64_t conversationTypeValue(ConversationKind kind) {
    return kind == ConversationKind::Group ? kConversationTypeGroup : kConversationTypePrivate;
}

QString conversationIdForMessage(ConversationKind kind,
                                 std::uint64_t self_user_id,
                                 std::uint64_t conversation_id,
                                 std::uint64_t sender_id,
                                 std::uint64_t receiver_id) {
    if (kind == ConversationKind::Group) {
        return QStringLiteral("group:") + QString::number(conversation_id);
    }

    const auto peer_id = sender_id == self_user_id ? receiver_id : sender_id;
    return QStringLiteral("private:") + QString::number(peer_id);
}

QString senderNameFor(std::uint64_t sender_id, std::uint64_t self_user_id) {
    if (sender_id == self_user_id) {
        return QStringLiteral("Me");
    }
    return QStringLiteral("User ") + QString::number(sender_id);
}

bool isChatRequest(MessageType type) {
    switch (type) {
        case MessageType::AddFriendRequest:
        case MessageType::CreateGroupRequest:
        case MessageType::JoinGroupRequest:
        case MessageType::PrivateMessageRequest:
        case MessageType::GroupMessageRequest:
        case MessageType::HistoryRequest:
            return true;
        default:
            return false;
    }
}

}  // namespace

ChatController::ChatController(ClientRuntime& runtime, QObject* parent)
    : QObject(parent), runtime_(runtime) {
    qRegisterMetaType<liteim::client::ChatMessage>("liteim::client::ChatMessage");
    qRegisterMetaType<QVector<liteim::client::ChatMessage>>(
        "QVector<liteim::client::ChatMessage>");

    connect(&runtime_.client(), &TcpClient::packetReceived, this,
            &ChatController::handlePacketReceived);
    connect(&runtime_.client(), &TcpClient::errorOccurred, this, &ChatController::requestFailed);
}

Status ChatController::addFriend(std::uint64_t target_user_id) {
    if (target_user_id == 0) {
        return invalidArgument("target user id is invalid");
    }

    Packet packet;
    const auto status = PacketCodec::appendUint64Field(TlvType::TargetUserId, target_user_id, packet);
    if (!status.isOk()) {
        return status;
    }
    return sendPacket(MessageType::AddFriendRequest, packet);
}

Status ChatController::createGroup(const QString& group_name) {
    const auto clean_name = group_name.trimmed();
    if (clean_name.isEmpty()) {
        return invalidArgument("group name must not be empty");
    }

    Packet packet;
    const auto status = PacketCodec::appendStringField(TlvType::GroupName, clean_name, packet);
    if (!status.isOk()) {
        return status;
    }
    return sendPacket(MessageType::CreateGroupRequest, packet);
}

Status ChatController::joinGroup(std::uint64_t group_id) {
    if (group_id == 0) {
        return invalidArgument("group id is invalid");
    }

    Packet packet;
    const auto status = PacketCodec::appendUint64Field(TlvType::GroupId, group_id, packet);
    if (!status.isOk()) {
        return status;
    }
    return sendPacket(MessageType::JoinGroupRequest, packet);
}

Status ChatController::sendPrivateMessage(std::uint64_t receiver_id, const QString& text) {
    const auto clean_text = text.trimmed();
    if (receiver_id == 0) {
        return invalidArgument("receiver id is invalid");
    }
    if (clean_text.isEmpty()) {
        return invalidArgument("message text must not be empty");
    }

    Packet packet;
    auto status = PacketCodec::appendUint64Field(TlvType::ReceiverId, receiver_id, packet);
    if (!status.isOk()) {
        return status;
    }
    status = PacketCodec::appendStringField(TlvType::MessageText, clean_text, packet);
    if (!status.isOk()) {
        return status;
    }
    return sendPacket(MessageType::PrivateMessageRequest, packet);
}

Status ChatController::sendGroupMessage(std::uint64_t group_id, const QString& text) {
    const auto clean_text = text.trimmed();
    if (group_id == 0) {
        return invalidArgument("group id is invalid");
    }
    if (clean_text.isEmpty()) {
        return invalidArgument("message text must not be empty");
    }

    Packet packet;
    auto status = PacketCodec::appendUint64Field(TlvType::GroupId, group_id, packet);
    if (!status.isOk()) {
        return status;
    }
    status = PacketCodec::appendStringField(TlvType::MessageText, clean_text, packet);
    if (!status.isOk()) {
        return status;
    }
    return sendPacket(MessageType::GroupMessageRequest, packet);
}

Status ChatController::requestHistory(ConversationKind kind,
                                      std::uint64_t conversation_id,
                                      std::uint64_t before_message_id,
                                      std::uint64_t limit) {
    if (conversation_id == 0) {
        return invalidArgument("conversation id is invalid");
    }
    if (limit == 0) {
        return invalidArgument("history limit is invalid");
    }

    Packet packet;
    auto status =
        PacketCodec::appendUint64Field(TlvType::ConversationType, conversationTypeValue(kind), packet);
    if (!status.isOk()) {
        return status;
    }
    status = PacketCodec::appendUint64Field(TlvType::ConversationId, conversation_id, packet);
    if (!status.isOk()) {
        return status;
    }
    if (before_message_id != 0) {
        status = PacketCodec::appendUint64Field(TlvType::MessageId, before_message_id, packet);
        if (!status.isOk()) {
            return status;
        }
    }
    status = PacketCodec::appendUint64Field(TlvType::Limit, limit, packet);
    if (!status.isOk()) {
        return status;
    }
    return sendPacket(MessageType::HistoryRequest, packet);
}

void ChatController::reportFailure(const QString& message) {
    emit requestFailed(message);
}

std::uint64_t ChatController::privateConversationId(std::uint64_t left_user_id,
                                                    std::uint64_t right_user_id) noexcept {
    if (left_user_id == 0 || right_user_id == 0 || left_user_id == right_user_id) {
        return 0;
    }

    const auto left = std::min(left_user_id, right_user_id);
    const auto right = std::max(left_user_id, right_user_id);
    if (left < kSmallUserIdConversationBase && right < kSmallUserIdConversationBase) {
        return left * kSmallUserIdConversationBase + right;
    }
    if (right > std::numeric_limits<std::uint32_t>::max()) {
        return 0;
    }
    return (left << 32U) | right;
}

Status ChatController::sendPacket(MessageType type, Packet& packet) {
    packet.header.msg_type = type;
    packet.header.seq_id = runtime_.session().trackRequest(type);

    const auto status = runtime_.client().sendPacket(packet);
    if (!status.isOk()) {
        runtime_.session().takePending(packet.header.seq_id);
        emit requestFailed(statusMessage(status));
    }
    return status;
}

void ChatController::handlePacketReceived(const Packet& packet) {
    if (packet.header.msg_type == MessageType::PrivateMessagePush ||
        packet.header.msg_type == MessageType::GroupMessagePush) {
        ChatMessage message;
        const auto status = parseMessagePacket(packet, message);
        if (!status.isOk()) {
            emit requestFailed(statusMessage(status));
            return;
        }
        emit messageReceived(message);
        return;
    }

    const auto pending = runtime_.session().pendingRequest(packet.header.seq_id);
    if (!pending.has_value()) {
        return;
    }
    if (!isChatRequest(pending->request_type)) {
        return;
    }
    runtime_.session().takePending(packet.header.seq_id);
    handlePendingResponse(packet, *pending);
}

void ChatController::handlePendingResponse(const Packet& packet, const PendingRequest& pending) {
    if (packet.header.msg_type == MessageType::ErrorResponse) {
        emit requestFailed(parseErrorMessage(packet));
        return;
    }

    if ((pending.request_type == MessageType::PrivateMessageRequest &&
         packet.header.msg_type == MessageType::PrivateMessageResponse) ||
        (pending.request_type == MessageType::GroupMessageRequest &&
         packet.header.msg_type == MessageType::GroupMessageResponse)) {
        ChatMessage message;
        const auto status = parseMessagePacket(packet, message);
        if (!status.isOk()) {
            emit requestFailed(statusMessage(status));
            return;
        }
        emit messageDelivered(message);
        return;
    }

    if (pending.request_type == MessageType::HistoryRequest &&
        packet.header.msg_type == MessageType::HistoryResponse) {
        QVector<ChatMessage> messages;
        const auto status = parseRepeatedMessages(packet, messages);
        if (!status.isOk()) {
            emit requestFailed(statusMessage(status));
            return;
        }
        emit historyLoaded(messages);
        return;
    }

    TlvMap fields;
    if (!PacketCodec::parseFields(packet, fields).isOk()) {
        emit requestFailed(QStringLiteral("failed to parse server response"));
        return;
    }

    if (pending.request_type == MessageType::AddFriendRequest &&
        packet.header.msg_type == MessageType::AddFriendResponse) {
        std::uint64_t user_id = 0;
        QString nickname;
        if (!PacketCodec::getUint64Field(fields, TlvType::FriendId, user_id).isOk()) {
            emit requestFailed(QStringLiteral("add-friend response missing user id"));
            return;
        }
        if (!PacketCodec::getStringField(fields, TlvType::Nickname, nickname).isOk()) {
            nickname = QStringLiteral("User ") + QString::number(user_id);
        }
        emit friendAdded(user_id, nickname);
        return;
    }

    if ((pending.request_type == MessageType::CreateGroupRequest &&
         packet.header.msg_type == MessageType::CreateGroupResponse) ||
        (pending.request_type == MessageType::JoinGroupRequest &&
         packet.header.msg_type == MessageType::JoinGroupResponse)) {
        std::uint64_t group_id = 0;
        QString group_name;
        if (!PacketCodec::getUint64Field(fields, TlvType::GroupId, group_id).isOk()) {
            emit requestFailed(QStringLiteral("group response missing group id"));
            return;
        }
        if (!PacketCodec::getStringField(fields, TlvType::GroupName, group_name).isOk()) {
            group_name = QStringLiteral("Group ") + QString::number(group_id);
        }
        if (pending.request_type == MessageType::CreateGroupRequest) {
            emit groupCreated(group_id, group_name);
        } else {
            emit groupJoined(group_id, group_name);
        }
    }
}

Status ChatController::parseMessagePacket(const Packet& packet, ChatMessage& message) const {
    TlvMap fields;
    auto status = PacketCodec::parseFields(packet, fields);
    if (!status.isOk()) {
        return status;
    }

    std::uint64_t conversation_type = 0;
    std::uint64_t conversation_id = 0;
    std::uint64_t message_id = 0;
    std::uint64_t sender_id = 0;
    std::uint64_t timestamp_ms = 0;
    status = PacketCodec::getUint64Field(fields, TlvType::MessageId, message_id);
    if (!status.isOk()) {
        return status;
    }
    status = PacketCodec::getUint64Field(fields, TlvType::ConversationType, conversation_type);
    if (!status.isOk()) {
        return status;
    }
    status = PacketCodec::getUint64Field(fields, TlvType::ConversationId, conversation_id);
    if (!status.isOk()) {
        return status;
    }
    status = PacketCodec::getUint64Field(fields, TlvType::SenderId, sender_id);
    if (!status.isOk()) {
        return status;
    }
    std::uint64_t receiver_id = 0;
    status = PacketCodec::getUint64Field(fields, TlvType::ReceiverId, receiver_id);
    if (!status.isOk()) {
        return status;
    }
    status = PacketCodec::getStringField(fields, TlvType::MessageText, message.text);
    if (!status.isOk()) {
        return status;
    }
    status = PacketCodec::getUint64Field(fields, TlvType::TimestampMs, timestamp_ms);
    if (!status.isOk()) {
        return status;
    }

    const auto kind = conversation_type == kConversationTypeGroup ? ConversationKind::Group
                                                                  : ConversationKind::Private;
    const auto self_user_id = runtime_.session().userId();
    message.message_id = message_id;
    message.sender_id = sender_id;
    message.conversation_id =
        conversationIdForMessage(kind, self_user_id, conversation_id, sender_id, receiver_id);
    message.sender_name = senderNameFor(sender_id, self_user_id);
    message.sent_at = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestamp_ms));
    message.direction = sender_id == self_user_id ? MessageDirection::Outgoing
                                                  : MessageDirection::Incoming;
    message.status = MessageSendStatus::Succeeded;
    return Status::ok();
}

Status ChatController::parseRepeatedMessages(const Packet& packet,
                                             QVector<ChatMessage>& messages) const {
    messages.clear();

    TlvMap fields;
    auto status = PacketCodec::parseFields(packet, fields);
    if (!status.isOk()) {
        return status;
    }
    if (fields.find(TlvType::MessageId) == fields.end()) {
        return Status::ok();
    }

    std::vector<std::uint64_t> message_ids;
    std::vector<std::uint64_t> conversation_types;
    std::vector<std::uint64_t> conversation_ids;
    std::vector<std::uint64_t> sender_ids;
    std::vector<std::uint64_t> receiver_ids;
    std::vector<std::uint64_t> timestamp_ms_values;
    std::vector<std::string> texts;

    status = getRepeatedUint64(fields, TlvType::MessageId, message_ids);
    if (!status.isOk()) {
        return status;
    }
    status = getRepeatedUint64(fields, TlvType::ConversationType, conversation_types);
    if (!status.isOk()) {
        return status;
    }
    status = getRepeatedUint64(fields, TlvType::ConversationId, conversation_ids);
    if (!status.isOk()) {
        return status;
    }
    status = getRepeatedUint64(fields, TlvType::SenderId, sender_ids);
    if (!status.isOk()) {
        return status;
    }
    status = getRepeatedUint64(fields, TlvType::ReceiverId, receiver_ids);
    if (!status.isOk()) {
        return status;
    }
    status = getRepeatedString(fields, TlvType::MessageText, texts);
    if (!status.isOk()) {
        return status;
    }
    status = getRepeatedUint64(fields, TlvType::TimestampMs, timestamp_ms_values);
    if (!status.isOk()) {
        return status;
    }

    const auto count = message_ids.size();
    if (conversation_types.size() != count || conversation_ids.size() != count ||
        sender_ids.size() != count || receiver_ids.size() != count || texts.size() != count ||
        timestamp_ms_values.size() != count) {
        return Status::error(ErrorCode::ParseError, "history response message fields mismatch");
    }

    messages.reserve(static_cast<int>(count));
    const auto self_user_id = runtime_.session().userId();
    for (std::size_t i = 0; i < count; ++i) {
        ChatMessage message;
        const auto kind = conversation_types[i] == kConversationTypeGroup
                              ? ConversationKind::Group
                              : ConversationKind::Private;
        message.message_id = message_ids[i];
        message.sender_id = sender_ids[i];
        message.conversation_id = conversationIdForMessage(kind,
                                                           self_user_id,
                                                           conversation_ids[i],
                                                           sender_ids[i],
                                                           receiver_ids[i]);
        message.sender_name = senderNameFor(sender_ids[i], self_user_id);
        message.text = QString::fromStdString(texts[i]);
        message.sent_at =
            QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(timestamp_ms_values[i]));
        message.direction = sender_ids[i] == self_user_id ? MessageDirection::Outgoing
                                                          : MessageDirection::Incoming;
        message.status = MessageSendStatus::Succeeded;
        messages.push_back(std::move(message));
    }
    return Status::ok();
}

QString ChatController::parseErrorMessage(const Packet& packet) const {
    TlvMap fields;
    if (!PacketCodec::parseFields(packet, fields).isOk()) {
        return QStringLiteral("server returned an error");
    }

    QString message;
    if (!PacketCodec::getStringField(fields, TlvType::ErrorMessage, message).isOk()) {
        return QStringLiteral("server returned an error");
    }
    return message;
}

}  // namespace liteim::client
