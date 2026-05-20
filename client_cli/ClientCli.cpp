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

// 去掉字符串前后空白
std::string trim(const std::string& input) {
    const auto begin = input.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = input.find_last_not_of(" \t\r\n");
    return input.substr(begin, end - begin + 1U);
}

// 从输入流里读取一个单词
bool readToken(std::istringstream& stream, std::string& token) {
    return static_cast<bool>(stream >> token);
}
// 读取后面所有文本
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
// 设置协议头里的 msg_type 和 seq_id
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
// 从输入流里读取一个token
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

// 这个函数负责解析用户输入的注册命令，解析register <username> <password> [nickname...]
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

    // 初始化协议包
    resetPacket(MessageType::RegisterRequest, seq_id, packet);
    status = appendStringField(TlvType::Username, username, packet);
    if (!status.isOk()) {
        return status;
    }
    // 把密码放进 TLV body
    status = appendStringField(TlvType::Password, password, packet);
    if (!status.isOk()) {
        return status;
    }

    // 可选的昵称参数，如果有的话也放进 TLV body
    const auto nickname = remainingText(stream);
    if (!nickname.empty()) {
        return appendStringField(TlvType::Nickname, nickname, packet);
    }
    return Status::ok();
}

// 这个函数负责解析用户输入的登录命令，解析login <username> <password>
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

// 解析类似 add-friend <user_id> 这种只有一个 ID 参数的命令
Status buildOneIdCommand(std::istringstream& stream, std::uint64_t seq_id, MessageType message_type,
                         TlvType field_type, const char* field_name, Packet& packet) {
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

// 解析类似 private <receiver_id> <text...> 这种有一个 ID 参数和一段文本的私聊命令
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

// 解析类似 create-group <group_name...> 这种创建群聊的命令
Status buildCreateGroup(std::istringstream& stream, std::uint64_t seq_id, Packet& packet) {
    std::string group_name;
    const auto status = requireRest(stream, "group name", group_name);
    if (!status.isOk()) {
        return status;
    }

    resetPacket(MessageType::CreateGroupRequest, seq_id, packet);
    return appendStringField(TlvType::GroupName, group_name, packet);
}

// 解析类似 group <group_id> <text...> 这种群消息命令
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

// 解析历史消息命令，history <private|group> <conversation_id> [limit] [before_message_id]
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

// 解析离线消息拉取命令
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

Status buildOfflineAck(std::istringstream& stream, std::uint64_t seq_id, Packet& packet) {
    resetPacket(MessageType::OfflineMessagesAckRequest, seq_id, packet);

    std::string token;
    bool has_message_id = false;
    while (readToken(stream, token)) {
        std::uint64_t message_id = 0;
        const auto status = parseUint64(token, "message_id", message_id);
        if (!status.isOk()) {
            return status;
        }
        const auto append_status = appendUint64Field(TlvType::MessageId, message_id, packet);
        if (!append_status.isOk()) {
            return append_status;
        }
        has_message_id = true;
    }

    if (!has_message_id) {
        return invalidCommand("message_id is required");
    }
    return Status::ok();
}

// 这个函数负责从socket里解析出一个PacketHeader，保存在header参数里
Status parseHeaderFromSocket(int fd, PacketHeader& header) {
    Bytes header_bytes(kPacketHeaderSize);
    std::size_t read_bytes = 0;
    while (read_bytes < header_bytes.size()) {
        const auto rc =
            ::read(fd, header_bytes.data() + read_bytes, header_bytes.size() - read_bytes);
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

// 这个函数负责从socket里读取指定长度的body数据，保存在body参数里
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

// 把 bytes 里的数据完整写入 socket
Status writeAll(int fd, const Bytes& bytes) {
    std::size_t written = 0;
    while (written < bytes.size()) {
        const auto rc = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (rc > 0) {  // 写成功了，继续写剩下的数据
            written += static_cast<std::size_t>(rc);
            continue;
        }
        if (rc < 0 && errno == EINTR) {  // 写操作被信号中断了，重试
            continue;
        }
        return ioError(std::string("write packet failed: ") + std::strerror(errno));
    }
    return Status::ok();
}

// 从 TLV 字段里取出字符串字段，拼到CLI输出文本里，例如：Username = alice
void appendStringFieldDescription(const TlvMap& fields, TlvType type, const char* label,
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

// 从 TLV 字段里取出整数字段，拼到CLI输出文本里，例如：UserId=12345
void appendUint64FieldDescription(const TlvMap& fields, TlvType type, const char* label,
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

// 命令分发函数，根据用户输入的命令行文本构造不同的Packet对象
Status buildPacketFromLine(const std::string& line, std::uint64_t seq_id, Packet& packet) {
    std::istringstream stream(trim(line));  // 创建一个输入流来解析用户输入的文本并去掉前后空白
    std::string command;                    // 从输入流里读取第一个单词作为命令
    if (!readToken(stream, command)) {
        return invalidCommand("command is empty");
    }

    // 根据命令的不同，调用不同的解析函数来构造Packet对象
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
    if (command == "offline-ack") {
        return buildOfflineAck(stream, seq_id, packet);
    }

    return invalidCommand("unknown command: " + command);
}

// 这个函数将Packet对象转换成人类可读的文本，用于调试和日志输出。
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
    appendUint64FieldDescription(fields, TlvType::DeliveryStatus, "delivery_status", stream);
    appendUint64FieldDescription(fields, TlvType::ErrorCode, "error_code", stream);
    appendStringFieldDescription(fields, TlvType::ErrorMessage, "error", stream);
    appendStringFieldDescription(fields, TlvType::MessageText, "text", stream);

    return stream.str();
}

// 这个函数返回CLI的帮助文本。
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
  offline-ack <message_id> [message_id...]
  heartbeat
  help
  quit)";
}

ProtocolClient::~ProtocolClient() {
    close();
}

// 负责连接服务器，成功返回Status::ok()，失败返回错误状态
Status ProtocolClient::connectTo(const std::string& host, std::uint16_t port) {
    close();  // 先关闭之前的连接（如果有的话）

    // 用的是addrinfo而不是sockaddr_in，拥有域名解析功能
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;  // 不限制使用 IPv4 还是 IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* raw_results = nullptr;
    const auto port_text = std::to_string(port);
    // 调用 getaddrinfo 来解析服务器地址，得到一个 addrinfo 结构的地址链表
    const auto rc = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &raw_results);
    if (rc != 0) {
        return ioError(std::string("resolve address failed: ") + ::gai_strerror(rc));
    }

    // 用 unique_ptr 管理 getaddrinfo() 返回的链表
    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> results(raw_results, ::freeaddrinfo);
    // 遍历所有解析出来的地址。一个 host 可能解析出多个地址，例如 IPv4 和 IPv6 地址
    for (auto* item = results.get(); item != nullptr; item = item->ai_next) {
        // 尝试用这个地址创建一个 socket
        UniqueFd candidate(
            ::socket(item->ai_family, item->ai_socktype | SOCK_CLOEXEC, item->ai_protocol));
        if (!candidate) {
            continue;
        }
        // 尝试连接服务器，连接成功就把这个 socket 赋值给成员变量 socket_
        if (::connect(candidate.fd(), item->ai_addr, item->ai_addrlen) == 0) {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            socket_ = std::move(candidate);
            return Status::ok();
        }
    }

    return ioError(std::string("connect failed: ") + std::strerror(errno));
}

// 把内存里的 Packet 编码成二进制，然后写入 TCP 连接
Status ProtocolClient::sendPacket(const Packet& packet) {
    Bytes encoded;
    const auto encode_status = encodePacket(packet, encoded);
    if (!encode_status.isOk()) {
        return encode_status;
    }

    // 加锁，确保在发送数据的过程中连接不会被关闭或者替换掉
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (!socket_) {
        return Status::error(ErrorCode::NotFound, "client is not connected");
    }
    return writeAll(socket_.fd(), encoded);
}

// 从 TCP 连接中读出一个完整的 Packet
Status ProtocolClient::readPacket(Packet& packet) {
    // 先加锁检查是否已连接，然后取出 fd
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

// 主动断开当前 socket连接
void ProtocolClient::close() noexcept {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (!socket_) {
        return;
    }
    // 关闭读写方向，如果另一个线程正在阻塞读，通常也会被唤醒
    (void)::shutdown(socket_.fd(), SHUT_RDWR);
    socket_.reset();  // 释放 UniqueFd里的 fd，真正关闭连接
}

// 查询是否已连接
bool ProtocolClient::connected() const noexcept {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    return static_cast<bool>(socket_);
}

}  // namespace liteim::cli
