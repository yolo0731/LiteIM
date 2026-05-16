#include "liteim/service/AuthService.hpp"

#include "liteim/base/ErrorCode.hpp"
#include "liteim/protocol/TlvCodec.hpp"
#include "liteim/service/Validation.hpp"

// 链接openssl库,用于注册 / 登录里的密码盐和密码哈希
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace liteim {
namespace {

constexpr std::size_t kPasswordSaltBytes = 16;  // 16 字节盐
constexpr std::size_t kPasswordHashBytes = 32;  // 32 字节哈希
constexpr int kPbkdf2Iterations = 10000;        // PBKDF2 迭代次数

Status invalidCredentialsStatus() {
    return Status::error(ErrorCode::InvalidArgument, "invalid username or password");
}

Status loginLimitedStatus() {
    return Status::error(ErrorCode::InvalidArgument, "too many login failures");
}

Status emptyFieldStatus(const char* field_name) {
    return Status::error(ErrorCode::InvalidArgument,
                         std::string(field_name) + " must not be empty");
}

// 二进制转十六进制字符串,openssl生成的盐和哈希都是二进制的，这里转成十六进制字符串存储
std::string toHex(const unsigned char* data, std::size_t len) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string output;
    output.resize(len * 2U);
    for (std::size_t i = 0; i < len; ++i) {
        output[i * 2U] = kHex[(data[i] >> 4U) & 0x0FU];
        output[i * 2U + 1U] = kHex[data[i] & 0x0FU];
    }
    return output;
}

// 生成随机 salt
Status generateSalt(std::string& salt) {
    std::array<unsigned char, kPasswordSaltBytes> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        return Status::error(ErrorCode::InternalError, "failed to generate password salt");
    }
    salt = toHex(bytes.data(), bytes.size());
    return Status::ok();
}

// 哈希密码，使用 PBKDF2-HMAC-SHA256 算法，输入密码和盐，输出哈希值
Status hashPassword(const std::string& password, const std::string& salt,
                    std::string& password_hash) {
    std::array<unsigned char, kPasswordHashBytes> bytes{};
    const auto rc = PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                                      reinterpret_cast<const unsigned char*>(salt.data()),
                                      static_cast<int>(salt.size()), kPbkdf2Iterations,
                                      EVP_sha256(), static_cast<int>(bytes.size()), bytes.data());
    if (rc != 1) {
        return Status::error(ErrorCode::InternalError, "failed to hash password");
    }
    // 把二进制哈希转成十六进制字符串存储
    password_hash = toHex(bytes.data(), bytes.size());
    return Status::ok();
}

// 安全比较哈希
bool secureEquals(const std::string& lhs, const std::string& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    unsigned char diff = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        diff |= static_cast<unsigned char>(lhs[i] ^ rhs[i]);  // 两个字符做异或
    }
    return diff == 0;
}

// 从 TLV 里取必填字符串,TlvMap& fields表示已经解析好的 TLV 字段，TlvType type 表示要取哪个字段，field_name 用于错误信息，output 用于输出结果
Status requiredString(const TlvMap& fields, TlvType type, const char* field_name,
                      std::string& output) {
    const auto status = getString(fields, type, output);
    if (!status.isOk()) {
        return status;
    }
    if (output.empty()) {
        return emptyFieldStatus(field_name);
    }
    return Status::ok();
}

// 取可选昵称，如果没有提供昵称，就用用户名当昵称
Status optionalNickname(const TlvMap& fields, const std::string& username, std::string& nickname) {
    const auto it = fields.find(TlvType::Nickname);
    if (it == fields.end() || it->second.empty()) {
        nickname = username;
        return Status::ok();
    }

    const auto status = getString(fields, TlvType::Nickname, nickname);
    if (!status.isOk()) {
        return status;
    }
    if (nickname.empty()) {
        return emptyFieldStatus("nickname");
    }
    return Status::ok();
}

// 组装response的Packet body
Status appendUserFields(const UserRecord& user, Packet& response) {
    const auto id_status = appendUint64(TlvType::UserId, user.user_id, response.body);
    if (!id_status.isOk()) {
        return id_status;
    }
    const auto username_status = appendString(TlvType::Username, user.username, response.body);
    if (!username_status.isOk()) {
        return username_status;
    }
    return appendString(TlvType::Nickname, user.nickname, response.body);
}

Status appendLoginFields(const UserRecord& user, std::uint64_t session_id, Packet& response) {
    const auto user_status = appendUserFields(user, response);
    if (!user_status.isOk()) {
        return user_status;
    }
    return appendUint64(TlvType::SessionId, session_id, response.body);
}

// 记录登录失败
Status recordLoginFailure(ICache& cache, const LoginAttemptKey& key,
                          std::chrono::seconds failure_ttl, const Status& login_status) {
    const auto record_status = cache.recordLoginFailure(key, failure_ttl);
    if (!record_status.isOk()) {
        return record_status;
    }
    return login_status;
}

}  // namespace

AuthService::AuthService(IStorage& storage, ICache& cache, OnlineService& online_service,
                         AuthServiceOptions options)
    : storage_(storage), cache_(cache), online_service_(online_service),
      options_(std::move(options)) {
    if (options_.max_login_failures == 0U) {
        throw std::invalid_argument("max login failures must be greater than zero");
    }
    if (options_.login_failure_ttl.count() <= 0) {
        throw std::invalid_argument("login failure ttl must be positive");
    }
    if (options_.default_remote_ip.empty()) {
        throw std::invalid_argument("default remote ip must not be empty");
    }
}

// 注册 登录/注册handler
Status AuthService::registerHandlers(MessageRouter& router) {
    const auto register_status = router.registerHandler(
        MessageType::RegisterRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleRegister(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
    if (!register_status.isOk()) {
        return register_status;
    }

    return router.registerHandler(
        MessageType::LoginRequest,
        [this](const MessageRouter::RouterRequest& request, Packet& response) {
            return handleLogin(request, response);
        },
        MessageRouter::DispatchMode::BusinessThread);
}

// 处理注册请求,从 TLV 里取用户名和密码，生成盐和哈希，保存到数据库，构建响应包
Status AuthService::handleRegister(const MessageRouter::RouterRequest& request, Packet& response) {
    std::string username;
    const auto username_status =
        requiredString(request.fields, TlvType::Username, "username", username);
    if (!username_status.isOk()) {
        return username_status;
    }
    const auto username_length_status =
        validateMaxBytes(username, kMaxUsernameBytes, "username");
    if (!username_length_status.isOk()) {
        return username_length_status;
    }

    std::string password;
    const auto password_status =
        requiredString(request.fields, TlvType::Password, "password", password);
    if (!password_status.isOk()) {
        return password_status;
    }
    const auto password_length_status =
        validateMaxBytes(password, kMaxPasswordBytes, "password");
    if (!password_length_status.isOk()) {
        return password_length_status;
    }

    std::string nickname;
    const auto nickname_status = optionalNickname(request.fields, username, nickname);
    if (!nickname_status.isOk()) {
        return nickname_status;
    }
    const auto nickname_length_status =
        validateMaxBytes(nickname, kMaxNicknameBytes, "nickname");
    if (!nickname_length_status.isOk()) {
        return nickname_length_status;
    }

    std::string salt;
    const auto salt_status = generateSalt(salt);
    if (!salt_status.isOk()) {
        return salt_status;
    }

    std::string password_hash;
    const auto hash_status = hashPassword(password, salt, password_hash);
    if (!hash_status.isOk()) {
        return hash_status;
    }

    CreateUserRequest create_request;
    create_request.username = std::move(username);
    create_request.password_hash = std::move(password_hash);
    create_request.password_salt = std::move(salt);
    create_request.nickname = std::move(nickname);

    UserRecord created_user;
    const auto create_status = storage_.createUser(create_request, created_user);
    if (!create_status.isOk()) {
        return create_status;
    }

    response.header.msg_type = MessageType::RegisterResponse;
    response.header.seq_id = request.packet.header.seq_id;
    return appendUserFields(created_user, response);
}

// 处理登录请求,从 TLV 里取用户名和密码，检查登录限制，验证密码，绑定在线session，构建响应包
Status AuthService::handleLogin(const MessageRouter::RouterRequest& request, Packet& response) {
    std::string username;
    const auto username_status =
        requiredString(request.fields, TlvType::Username, "username", username);
    if (!username_status.isOk()) {
        return username_status;
    }
    const auto username_length_status =
        validateMaxBytes(username, kMaxUsernameBytes, "username");
    if (!username_length_status.isOk()) {
        return username_length_status;
    }

    std::string password;
    const auto password_status =
        requiredString(request.fields, TlvType::Password, "password", password);
    if (!password_status.isOk()) {
        return password_status;
    }
    const auto password_length_status =
        validateMaxBytes(password, kMaxPasswordBytes, "password");
    if (!password_length_status.isOk()) {
        return password_length_status;
    }

    const LoginAttemptKey login_key{username, options_.default_remote_ip};
    bool allowed = false;
    const auto allow_status =
        cache_.allowLoginAttempt(login_key, options_.max_login_failures, allowed);
    if (!allow_status.isOk()) {
        return allow_status;
    }
    if (!allowed) {
        return loginLimitedStatus();
    }

    UserRecord user;
    const auto find_status = storage_.findUserByUsername(username, user);
    if (!find_status.isOk()) {
        if (find_status.code() == ErrorCode::NotFound) {
            return recordLoginFailure(cache_, login_key, options_.login_failure_ttl,
                                      invalidCredentialsStatus());
        }
        return find_status;
    }

    std::string candidate_hash;
    const auto hash_status = hashPassword(password, user.password_salt, candidate_hash);
    if (!hash_status.isOk()) {
        return hash_status;
    }
    if (!secureEquals(candidate_hash, user.password_hash)) {
        return recordLoginFailure(cache_, login_key, options_.login_failure_ttl,
                                  invalidCredentialsStatus());
    }

    const auto clear_status = cache_.clearLoginFailure(login_key);
    if (!clear_status.isOk()) {
        return clear_status;
    }

    const auto bind_status = online_service_.bindUser(user.user_id, request.session);
    if (!bind_status.isOk()) {
        return bind_status;
    }

    response.header.msg_type = MessageType::LoginResponse;
    response.header.seq_id = request.packet.header.seq_id;
    return appendLoginFields(user, request.session->id(), response);
}

const AuthServiceOptions& AuthService::options() const noexcept {
    return options_;
}

}  // namespace liteim
