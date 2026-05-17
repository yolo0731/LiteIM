#include "ClientCli.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/protocol/MessageType.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim/storage/StorageTypes.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

namespace liteim::cli {
namespace {

Status invalidCommand(const std::string& message) {
    return Status::error(ErrorCode::InvalidArgument, message);
}

Status ioError(const std::string& message) {
    return Status::error(ErrorCode::IoError, message);
}

std::string trim(const std::string& input) {
    const auto begin = input.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = input.find_last_not_of(" \t\r\n");
    return input.substr(begin, end - begin + 1U);
}

bool readToken(std::istringstream& stream, std::string& token) {
    return static_cast<bool>(stream >> token);
}

std::string remainingText(std::istringstream& stream) {
    std::string rest;
    std::getline(stream, rest);
    return trim(rest);
}

Status parseUint64(const std::string& token, const char* field_name, std::uint64_t& value) {
    if (token.empty()) {
        return invalidCommand(std::string(field_name) + " is required");
    }
    if (token.front() == '-') {
        return invalidCommand(std::string(field_name) + " must be an unsigned integer");
    }

    char* end = nullptr;
    errno = 0;
    const auto parsed = std::strtoull(token.c_str(), &end, 10);
    if (errno != 0 || end == token.c_str() || *end != '\0') {
        return invalidCommand(std::string(field_name) + " must be an unsigned integer");
    }
    value = static_cast<std::uint64_t>(parsed);
    return Status::ok();
}

void resetPacket(MessageType type, std::uint64_t seq_id, Packet& packet) {
    packet = Packet{};
    packet.header.msg_type = type;
    packet.header.seq_id = seq_id;
}

Status appendUint64Field(TlvType type, std::uint64_t value, Packet& packet) {
    return appendUint64(type, value, packet.body);
}

Status appendStringField(TlvType type, const std::string& value, Packet& packet) {
    return appendString(type, value, packet.body);
}

Status requireToken(std::istringstream& stream, const char* field_name, std::string& token) {
    if (!readToken(stream, token)) {
        return invalidCommand(std::string(field_name) + " is required");
    }
    return Status::ok();
}

Status requireRest(std::istringstream& stream, const char* field_name, std::string& text) {
    text = remainingText(stream);
    if (text.empty()) {
        return invalidCommand(std::string(field_name) + " is required");
    }
    return Status::ok();
}

Status buildRegister(std::istringstream& stream, std::uint64_t seq_id, Packet& packet) {
    std::string username;
    auto status = requireToken(stream, "username", username);
    if (!status.isOk()) {
        return status;
    }

    std::string password;
    status = requireToken(stream, "password", password);
    if (!status.isOk()) {
        return status;
    }

    resetPacket(MessageType::RegisterRequest, seq_id, packet);
    status = appendStringField(TlvType::Username, username, packet);
    if (!status.isOk()) {
        return status;
    }
    status = appendStringField(TlvType::Password, password, packet);
    if (!status.isOk()) {
        return status;
    }

    const auto nickname = remainingText(stream);
    if (!nickname.empty()) {
        return appendStringField(TlvType::Nickname, nickname, packet);
    }
    return Status::ok();
}

Status buildLogin(std::istringstream& stream, std::uint64_t seq_id, Packet& packet) {
    std::string username;
    auto status = requireToken(stream, "username", username);
    if (!status.isOk()) {
        return status;
    }

    std::string password;
    status = requireToken(stream, "password", password);
    if (!status.isOk()) {
        return status;
    }

    resetPacket(MessageType::LoginRequest, seq_id, packet);
    status = appendStringField(TlvType::Username, username, packet);
    if (!status.isOk()) {
        return status;
    }
    return appendStringField(TlvType::Password, password, packet);
}

Status buildOneIdCommand(std::istringstream& stream,
                         std::uint64_t seq_id,
                         MessageType message_type,
                         TlvType field_type,
                         const char* field_name,
                         Packet& packet) {
    std::string token;
    auto status = requireToken(stream, field_name, token);
    if (!status.isOk()) {
        return status;
    }
    std::uint64_t value = 0;
    status = parseUint64(token, field_name, value);
    if (!status.isOk()) {
        return status;
    }
    resetPacket(message_type, seq_id, packet);
    return appendUint64Field(field_type, value, packet);
}

Status buildPrivate(std::istringstream& stream, std::uint64_t seq_id, Packet& packet) {
    std::string receiver_token;
    auto status = requireToken(stream, "receiver_id", receiver_token);
    if (!status.isOk()) {
        return status;
    }

    std::uint64_t receiver_id = 0;
    status = parseUint64(receiver_token, "receiver_id", receiver_id);
    if (!status.isOk()) {
        return status;
    }

    std::string text;
    status = requireRest(stream, "message text", text);
    if (!status.isOk()) {
        return status;
    }

    resetPacket(MessageType::PrivateMessageRequest, seq_id, packet);
    status = appendUint64Field(TlvType::ReceiverId, receiver_id, packet);
    if (!status.isOk()) {
        return status;
    }
    return appendStringField(TlvType::MessageText, text, packet);
}

Status buildCreateGroup(std::istringstream& stream, std::uint64_t seq_id, Packet& packet) {
    std::string group_name;
    const auto status = requireRest(stream, "group name", group_name);
    if (!status.isOk()) {
        return status;
    }

    resetPacket(MessageType::CreateGroupRequest, seq_id, packet);
    return appendStringField(TlvType::GroupName, group_name, packet);
}

Status buildGroupMessage(std::istringstream& stream, std::uint64_t seq_id, Packet& packet) {
    std::string group_token;
    auto status = requireToken(stream, "group_id", group_token);
    if (!status.isOk()) {
        return status;
    }

    std::uint64_t group_id = 0;
    status = parseUint64(group_token, "group_id", group_id);
    if (!status.isOk()) {
        return status;
    }

    std::string text;
    status = requireRest(stream, "message text", text);
    if (!status.isOk()) {
        return status;
    }

    resetPacket(MessageType::GroupMessageRequest, seq_id, packet);
    status = appendUint64Field(TlvType::GroupId, group_id, packet);
    if (!status.isOk()) {
        return status;
    }
    return appendStringField(TlvType::MessageText, text, packet);
}

Status buildHistory(std::istringstream& stream, std::uint64_t seq_id, Packet& packet) {
    std::string type_token;
    auto status = requireToken(stream, "conversation type", type_token);
    if (!status.isOk()) {
        return status;
    }

    ConversationType conversation_type = ConversationType::kPrivate;
    if (type_token == "private") {
        conversation_type = ConversationType::kPrivate;
    } else if (type_token == "group") {
        conversation_type = ConversationType::kGroup;
    } else {
        return invalidCommand("conversation type must be private or group");
    }

    std::string conversation_token;
    status = requireToken(stream, "conversation_id", conversation_token);
    if (!status.isOk()) {
        return status;
    }
    std::uint64_t conversation_id = 0;
    status = parseUint64(conversation_token, "conversation_id", conversation_id);
    if (!status.isOk()) {
        return status;
    }

    resetPacket(MessageType::HistoryRequest, seq_id, packet);
    status = appendUint64Field(TlvType::ConversationType,
                               static_cast<std::uint64_t>(conversation_type), packet);
    if (!status.isOk()) {
        return status;
    }
    status = appendUint64Field(TlvType::ConversationId, conversation_id, packet);
    if (!status.isOk()) {
        return status;
    }

    std::string limit_token;
    if (readToken(stream, limit_token)) {
        std::uint64_t limit = 0;
        status = parseUint64(limit_token, "limit", limit);
        if (!status.isOk()) {
            return status;
        }
        status = appendUint64Field(TlvType::Limit, limit, packet);
        if (!status.isOk()) {
            return status;
        }
    }

    std::string before_token;
    if (readToken(stream, before_token)) {
        std::uint64_t before_message_id = 0;
        status = parseUint64(before_token, "before_message_id", before_message_id);
        if (!status.isOk()) {
            return status;
        }
        return appendUint64Field(TlvType::MessageId, before_message_id, packet);
    }

    return Status::ok();
}

Status buildOffline(std::istringstream& stream, std::uint64_t seq_id, Packet& packet) {
    resetPacket(MessageType::OfflineMessagesRequest, seq_id, packet);

    std::string limit_token;
    if (!readToken(stream, limit_token)) {
        return Status::ok();
    }

    std::uint64_t limit = 0;
    const auto status = parseUint64(limit_token, "limit", limit);
    if (!status.isOk()) {
        return status;
    }
    return appendUint64Field(TlvType::Limit, limit, packet);
}

Status parseHeaderFromSocket(int fd, PacketHeader& header) {
    Bytes header_bytes(kPacketHeaderSize);
    std::size_t read_bytes = 0;
    while (read_bytes < header_bytes.size()) {
        const auto rc = ::read(fd, header_bytes.data() + read_bytes, header_bytes.size() - read_bytes);
        if (rc > 0) {
            read_bytes += static_cast<std::size_t>(rc);
            continue;
        }
        if (rc == 0) {
            return ioError("connection closed while reading packet header");
        }
        if (errno == EINTR) {
            continue;
        }
        return ioError(std::string("read packet header failed: ") + std::strerror(errno));
    }
    return parseHeader(header_bytes.data(), header_bytes.size(), header);
}

Status readBodyFromSocket(int fd, std::uint32_t body_len, Bytes& body) {
    body.assign(body_len, Byte{0});
    std::size_t read_bytes = 0;
    while (read_bytes < body.size()) {
        const auto rc = ::read(fd, body.data() + read_bytes, body.size() - read_bytes);
        if (rc > 0) {
            read_bytes += static_cast<std::size_t>(rc);
            continue;
        }
        if (rc == 0) {
            return ioError("connection closed while reading packet body");
        }
        if (errno == EINTR) {
            continue;
        }
        return ioError(std::string("read packet body failed: ") + std::strerror(errno));
    }
    return Status::ok();
}

Status writeAll(int fd, const Bytes& bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto rc = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (rc > 0) {
            written += static_cast<std::size_t>(rc);
            continue;
        }
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        return ioError(std::string("write packet failed: ") + std::strerror(errno));
    }
    return Status::ok();
}

void appendStringFieldDescription(const TlvMap& fields,
                                  TlvType type,
                                  const char* label,
                                  std::ostringstream& stream) {
    std::vector<std::string> values;
    const auto status = getRepeatedString(fields, type, values);
    if (!status.isOk()) {
        return;
    }
    for (const auto& value : values) {
        stream << ' ' << label << "=\"" << value << '"';
    }
}

void appendUint64FieldDescription(const TlvMap& fields,
                                  TlvType type,
                                  const char* label,
                                  std::ostringstream& stream) {
    std::vector<std::uint64_t> values;
    const auto status = getRepeatedUint64(fields, type, values);
    if (!status.isOk()) {
        return;
    }
    for (const auto value : values) {
        stream << ' ' << label << '=' << value;
    }
}

}  // namespace

Status buildPacketFromLine(const std::string& line, std::uint64_t seq_id, Packet& packet) {
    std::istringstream stream(trim(line));
    std::string command;
    if (!readToken(stream, command)) {
        return invalidCommand("command is empty");
    }

    if (command == "heartbeat") {
        resetPacket(MessageType::HeartbeatRequest, seq_id, packet);
        return Status::ok();
    }
    if (command == "register") {
        return buildRegister(stream, seq_id, packet);
    }
    if (command == "login") {
        return buildLogin(stream, seq_id, packet);
    }
    if (command == "add-friend") {
        return buildOneIdCommand(stream, seq_id, MessageType::AddFriendRequest,
                                 TlvType::TargetUserId, "target_user_id", packet);
    }
    if (command == "friends") {
        resetPacket(MessageType::ListFriendsRequest, seq_id, packet);
        return Status::ok();
    }
    if (command == "private") {
        return buildPrivate(stream, seq_id, packet);
    }
    if (command == "create-group") {
        return buildCreateGroup(stream, seq_id, packet);
    }
    if (command == "join-group") {
        return buildOneIdCommand(stream, seq_id, MessageType::JoinGroupRequest, TlvType::GroupId,
                                 "group_id", packet);
    }
    if (command == "groups") {
        resetPacket(MessageType::ListGroupsRequest, seq_id, packet);
        return Status::ok();
    }
    if (command == "group") {
        return buildGroupMessage(stream, seq_id, packet);
    }
    if (command == "history") {
        return buildHistory(stream, seq_id, packet);
    }
    if (command == "offline") {
        return buildOffline(stream, seq_id, packet);
    }

    return invalidCommand("unknown command: " + command);
}

std::string describePacket(const Packet& packet) {
    std::ostringstream stream;
    stream << toString(packet.header.msg_type) << " seq=" << packet.header.seq_id;

    if (packet.body.empty()) {
        return stream.str();
    }

    TlvMap fields;
    const auto parse_status = parseTlvMap(packet.body, fields);
    if (!parse_status.isOk()) {
        stream << " body_bytes=" << packet.body.size() << " parse_error=\""
               << parse_status.message() << '"';
        return stream.str();
    }

    appendUint64FieldDescription(fields, TlvType::UserId, "user_id", stream);
    appendStringFieldDescription(fields, TlvType::Username, "username", stream);
    appendStringFieldDescription(fields, TlvType::Nickname, "nickname", stream);
    appendUint64FieldDescription(fields, TlvType::SessionId, "session_id", stream);
    appendUint64FieldDescription(fields, TlvType::FriendId, "friend_id", stream);
    appendUint64FieldDescription(fields, TlvType::TargetUserId, "target_user_id", stream);
    appendUint64FieldDescription(fields, TlvType::OnlineStatus, "online", stream);
    appendUint64FieldDescription(fields, TlvType::GroupId, "group_id", stream);
    appendStringFieldDescription(fields, TlvType::GroupName, "group_name", stream);
    appendUint64FieldDescription(fields, TlvType::ConversationType, "conversation_type", stream);
    appendUint64FieldDescription(fields, TlvType::ConversationId, "conversation_id", stream);
    appendUint64FieldDescription(fields, TlvType::MessageId, "message_id", stream);
    appendUint64FieldDescription(fields, TlvType::SenderId, "sender_id", stream);
    appendUint64FieldDescription(fields, TlvType::ReceiverId, "receiver_id", stream);
    appendUint64FieldDescription(fields, TlvType::TimestampMs, "timestamp_ms", stream);
    appendUint64FieldDescription(fields, TlvType::Limit, "limit", stream);
    appendUint64FieldDescription(fields, TlvType::UnreadCount, "unread", stream);
    appendUint64FieldDescription(fields, TlvType::ErrorCode, "error_code", stream);
    appendStringFieldDescription(fields, TlvType::ErrorMessage, "error", stream);
    appendStringFieldDescription(fields, TlvType::MessageText, "text", stream);

    return stream.str();
}

std::string helpText() {
    return R"(commands:
  register <username> <password> [nickname...]
  login <username> <password>
  add-friend <user_id>
  friends
  private <receiver_id> <text...>
  create-group <name...>
  join-group <group_id>
  groups
  group <group_id> <text...>
  history private|group <conversation_id> [limit] [before_message_id]
  offline [limit]
  heartbeat
  help
  quit)";
}

ProtocolClient::~ProtocolClient() {
    close();
}

Status ProtocolClient::connectTo(const std::string& host, std::uint16_t port) {
    close();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* raw_results = nullptr;
    const auto port_text = std::to_string(port);
    const auto rc = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &raw_results);
    if (rc != 0) {
        return ioError(std::string("resolve address failed: ") + ::gai_strerror(rc));
    }

    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> results(raw_results, ::freeaddrinfo);
    for (auto* item = results.get(); item != nullptr; item = item->ai_next) {
        UniqueFd candidate(::socket(item->ai_family, item->ai_socktype | SOCK_CLOEXEC,
                                    item->ai_protocol));
        if (!candidate) {
            continue;
        }
        if (::connect(candidate.fd(), item->ai_addr, item->ai_addrlen) == 0) {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            socket_ = std::move(candidate);
            return Status::ok();
        }
    }

    return ioError(std::string("connect failed: ") + std::strerror(errno));
}

Status ProtocolClient::sendPacket(const Packet& packet) {
    Bytes encoded;
    const auto encode_status = encodePacket(packet, encoded);
    if (!encode_status.isOk()) {
        return encode_status;
    }

    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (!socket_) {
        return Status::error(ErrorCode::NotFound, "client is not connected");
    }
    return writeAll(socket_.fd(), encoded);
}

Status ProtocolClient::readPacket(Packet& packet) {
    int fd = kInvalidFd;
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (!socket_) {
            return Status::error(ErrorCode::NotFound, "client is not connected");
        }
        fd = socket_.fd();
    }

    PacketHeader header;
    const auto header_status = parseHeaderFromSocket(fd, header);
    if (!header_status.isOk()) {
        return header_status;
    }

    Bytes body;
    const auto body_status = readBodyFromSocket(fd, header.body_len, body);
    if (!body_status.isOk()) {
        return body_status;
    }

    packet.header = header;
    packet.body = std::move(body);
    return Status::ok();
}

void ProtocolClient::close() noexcept {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (!socket_) {
        return;
    }
    (void)::shutdown(socket_.fd(), SHUT_RDWR);
    socket_.reset();
}

bool ProtocolClient::connected() const noexcept {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    return static_cast<bool>(socket_);
}

}  // namespace liteim::cli
