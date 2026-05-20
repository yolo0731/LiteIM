# Step 34：AuthService 注册登录登出

## 0. 本 Step 结论

- 目标：Step 34 的目标是把注册、登录和登出从“协议占位”推进到真实业务入口：RegisterRequest / LoginRequest / LogoutRequest 经 MessageRouter 投递到业务线程池，然后由 AuthService 访问 MySQL / Redis / OnlineService，并在登录成功后绑定在线 session、登出时解除当前 session 绑定。
- 前置依赖：依赖 Step 0-33 已建立的工程、协议或运行时基础。
- 主要交付：`AuthService 注册登录登出` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：本 Step 不实现好友、私聊、群聊、离线消息、历史消息、JWT、OAuth、短信或邮箱验证码。

## 1. 为什么需要这个 Step

Step 34 的目标是把注册、登录和登出从“协议占位”推进到真实业务入口：`RegisterRequest` / `LoginRequest` / `LogoutRequest` 经 `MessageRouter` 投递到业务线程池，然后由 `AuthService` 访问 MySQL / Redis / OnlineService，并在登录成功后绑定在线 session、登出时解除当前 session 绑定。

### 概念

前面几步已经准备好了注册登录需要的基础能力：

```text
Step 25 UserDao / AuthDao
Step 30 LoginRateLimiter
Step 31 MySqlStorage / RedisCache
Step 32 SessionManager / OnlineService
Step 33 MessageRouter
```

`AuthService` 把这些能力组合起来：

```text
RegisterRequest
    -> parse username / password / nickname
    -> generate salt
    -> PBKDF2-HMAC-SHA256 password hash
    -> IStorage::createUser()
    -> RegisterResponse

LoginRequest
    -> parse username / password
    -> ICache::allowLoginAttempt()
    -> IStorage::findUserByUsername()
    -> verify password hash
    -> ICache::clearLoginFailure()
    -> OnlineService::bindUser()
    -> LoginResponse

LogoutRequest
    -> OnlineService::unbindSession()
    -> LogoutResponse
```

关键边界：

- MySQL / Redis 调用只在 business `ThreadPool` handler 中执行。
- `AuthService` 不直接读写 fd、`Channel` 或 `Buffer`。
- 业务线程通过 `OnlineService` / `SessionManager` 做登录态绑定；`Session::close()` 仍通过 owner loop 执行。
- 密码哈希是课程项目基础实现，使用 OpenSSL `PBKDF2-HMAC-SHA256`，不是完整生产级认证体系。
- 登录限流优先使用 `Session::peerIp()` 里的真实客户端 IP；如果测试 session 或异常路径没有 peer IP，才退回 `AuthServiceOptions::default_remote_ip`。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `AuthService 注册登录登出` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不实现好友、私聊、群聊、离线消息、历史消息、JWT、OAuth、短信或邮箱验证码。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/service/AuthService.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/service/AuthService.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/service/auth_service_test.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/tutorials/step34_auth_service.md` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/service/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `server/main.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

### AuthServiceOptions

```cpp
struct AuthServiceOptions {
    std::uint32_t max_login_failures{3};
    std::chrono::seconds login_failure_ttl{std::chrono::minutes{5}};
    std::string default_remote_ip{"unknown"};
};
```

`max_login_failures` 是登录失败次数阈值。当前语义来自 Step 30：Redis 中的失败次数小于阈值时允许继续尝试，达到阈值后短时间拒绝。

`login_failure_ttl` 是失败窗口 TTL。每次失败都会刷新 Redis key 的过期时间。

`default_remote_ip` 是兜底来源标识。真实运行时 `TcpServer` 会从 accept 得到的 peer address 写入 `Session::peerIp()`，`AuthService` 按 username + peer IP 做登录失败窗口；只有测试 session 或缺失 peer IP 的路径才使用这个默认值。Step56 已增加 Redis integration 测试，验证同一 username 在不同真实 peer IP 下使用独立登录失败窗口。

### AuthService

```cpp
class AuthService {
public:
    AuthService(IStorage& storage, ICache& cache, OnlineService& online_service,
                AuthServiceOptions options = AuthServiceOptions{});

    Status registerHandlers(MessageRouter& router);
    Status handleRegister(const MessageRouter::RouterRequest& request, Packet& response);
    Status handleLogin(const MessageRouter::RouterRequest& request, Packet& response);
    Status handleLogout(const MessageRouter::RouterRequest& request, Packet& response);

    const AuthServiceOptions& options() const noexcept;
};
```

`AuthService` 不拥有 `IStorage`、`ICache` 或 `OnlineService`，只保存引用。server main 负责保证这些对象生命周期长于 Router handler 和业务线程任务。

### `registerHandlers()`

```cpp
Status registerHandlers(MessageRouter& router);
```

注册三个 handler：

```text
RegisterRequest -> AuthService::handleRegister(), BusinessThread
LoginRequest    -> AuthService::handleLogin(), BusinessThread
LogoutRequest   -> AuthService::handleLogout(), BusinessThread
```

这保证注册、登录、登出里的 MySQL / Redis / 密码哈希 / 在线状态变更不会在 Reactor I/O 线程执行。

### `handleRegister()`

输入字段：

```text
Username
Password
Nickname 可选；缺失时使用 username
```

执行步骤：

1. 校验 username/password 非空。
2. 读取 nickname；缺失时默认等于 username。
3. 生成 16 字节随机 salt，保存为 hex 字符串。
4. 使用 `PBKDF2-HMAC-SHA256` 计算 32 字节 hash，保存为 hex 字符串。
5. 调用 `IStorage::createUser()` 写 MySQL。
6. 返回 `RegisterResponse`，body 写入 `UserId`、`Username`、`Nickname`。

重复用户名由 MySQL unique key 和 `UserDao::createUser()` 转成 `ErrorCode::AlreadyExists`。

### `handleLogin()`

输入字段：

```text
Username
Password
```

执行步骤：

1. 校验 username/password 非空。
2. 用 `ICache::allowLoginAttempt()` 检查登录失败限制。
3. 从 `IStorage::findUserByUsername()` 查询用户。
4. 用户不存在时记录一次失败，并返回统一的 `invalid username or password`。
5. 用保存的 `password_salt` 重新计算 password hash。
6. hash 不匹配时记录一次失败，并返回统一错误。
7. hash 匹配时清除 Redis 登录失败计数。
8. 调用 `OnlineService::bindUser()` 写 Redis 在线状态并绑定 `SessionManager`。
9. 返回 `LoginResponse`，body 写入 `UserId`、`Username`、`Nickname`、`SessionId`。

登录失败不关闭连接。错误由 `MessageRouter` 统一转成 `ErrorResponse`。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

客户端注册：

```text
客户端发送 RegisterRequest
    TLV: Username=alice, Password=secret, Nickname=Alice
服务端返回 RegisterResponse
    TLV: UserId=10001, Username=alice, Nickname=Alice
```

客户端登录：

```text
客户端发送 LoginRequest
    TLV: Username=alice, Password=secret
服务端返回 LoginResponse
    TLV: UserId=10001, Username=alice, Nickname=Alice, SessionId=7
```

后续 FriendService / ChatService 会依赖 `SessionManager` 判断当前连接是否已登录。

客户端登出：

```text
客户端发送 LogoutRequest
服务端调用 OnlineService::unbindSession(session_id)
服务端返回 LogoutResponse
    TLV: SessionId=7
```

登出后同一连接继续发送需要登录态的请求，会由对应业务 service 返回 `ErrorResponse`。

### 2. 上下层调用连接

完整 runtime 链路：

```text
Session::handleRead()
    -> FrameDecoder::feed()
    -> TcpServer::handleMessage()
    -> MessageRouter::route()
    -> business ThreadPool
    -> AuthService::handleRegister() / handleLogin() / handleLogout()
    -> MySqlStorage / RedisCache / OnlineService
    -> Session::sendPacket()
    -> EventLoop::queueInLoop()
```

`MessageRouter` 负责 TLV parse、错误响应和 seq_id 保留；`AuthService` 只负责认证业务。

### 3. 整体运行链路

server main 现在会创建：

```text
MySqlPool
RedisPool
MySqlStorage
RedisCache
SessionManager
OnlineService
ThreadPool
MessageRouter
AuthService
TcpServer
```

启动顺序保留 Step 33 的信号约束：

```text
SignalWatcher::start()
    -> MySqlPool::start()
    -> RedisPool::start()
    -> AuthService::registerHandlers()
    -> ThreadPool::start()
    -> TcpServer::start()
    -> EventLoop::loop()
```

`SignalWatcher` 仍然先于业务线程池启动，避免 SIGINT/SIGTERM 被 worker 按默认动作处理。

### 4. AuthService 自身内部运行流程

注册内部流程：

```text
read Username / Password / Nickname
    -> generateSalt()
    -> hashPassword()
    -> storage.createUser()
    -> append RegisterResponse fields
```

登录内部流程：

```text
read Username / Password
    -> cache.allowLoginAttempt()
    -> storage.findUserByUsername()
    -> hashPassword()
    -> secureEquals()
    -> cache.clearLoginFailure()
    -> online_service.bindUser()
    -> append LoginResponse fields
```

`secureEquals()` 使用固定长度遍历比较两个 hash 字符串，避免普通字符串比较提前退出。

### 5. 该项目代码在实际应用中的具体数据例子

假设 Alice 第一次注册：

```text
RegisterRequest seq_id=11
Username=alice
Password=secret
Nickname=Alice
```

服务端可能写入 MySQL：

```text
user_id=10001
username=alice
password_salt=7ac9f2...
password_hash=9b84b1...
nickname=Alice
```

返回：

```text
RegisterResponse seq_id=11
UserId=10001
Username=alice
Nickname=Alice
```

Alice 登录成功时，当前 TCP 连接的 `session_id=7`：

```text
LoginRequest seq_id=12
Username=alice
Password=secret
```

AuthService 会：

```text
clear login:failure:5:alice:7:unknown
set online:user:10001 = v1:10001:7:liteim-server:<last_active_time_ms>
SessionManager: session_id=7 -> user_id=10001
```

返回：

```text
LoginResponse seq_id=12
UserId=10001
Username=alice
Nickname=Alice
SessionId=7
```

如果 Alice 连续输错密码达到阈值，后续登录会返回 `ErrorResponse`，但连接保持打开，客户端可以稍后重试。

## 6. 关键实现点

- 保持模块职责单一。
- 失败时返回清晰错误，不吞掉异常状态。
- 不跨越本 Step 边界提前实现后续业务。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `AuthService 注册登录登出` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

新增测试文件：

```text
tests/service/auth_service_test.cpp
```

覆盖内容：

- `AuthService` 头文件自包含和 public API。
- 注册成功会生成 salt/hash，且 hash 不等于明文密码。
- 重复用户名注册返回 `AlreadyExists`。
- 正确密码登录成功会返回 `LoginResponse`，清除失败计数，写在线状态，绑定 session。
- 登出成功会返回 `LogoutResponse`，解除 session 绑定并清理在线状态。
- 未携带 session 的登出请求返回 `InvalidArgument`。
- 错误密码登录失败会记录 Redis 登录失败次数，且不绑定 session。
- 连续错误密码达到阈值后，正确密码也会被短时间拒绝。
- MySQL + Redis 集成路径：真实 `MySqlStorage` / `RedisCache` 下注册、登录、在线状态和 session 绑定可用；本地依赖不可用时按现有规则 skip。
- logout 单元路径：当前 session 解绑、Redis 在线态清理和无 session 错误语义可验证。

关键验证命令：

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "AuthService" --output-on-failure
ctest --test-dir build -R "AuthService|MessageRouter|Service|Session|TcpServer" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

因为 Step 34 的 server runtime 会启动 MySQL / Redis pool，server smoke 前需要先启动本地依赖：

```bash
docker compose -f docker/docker-compose.yml up -d --wait
timeout 1s ./build/server/liteim_server || test $? -eq 124
```

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "AuthService" --output-on-failure
ctest --test-dir build -R "AuthService|MessageRouter|Service|Session|TcpServer" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
docker compose -f docker/docker-compose.yml up -d --wait
timeout 1s ./build/server/liteim_server || test $? -eq 124
```

## 9. 面试表达

### 一句话

本 Step 的核心是把 `AuthService 注册登录登出` 做成边界清楚、可测试、可继续扩展的一层。

### 展开说

围绕为什么需要 `AuthService 注册登录登出`、它依赖哪些前置 Step、它暴露什么接口、失败时怎么返回、线程和生命周期边界在哪里展开。

### 容易被追问

- 为什么 AuthService 不直接依赖 MySqlStorage / RedisCache？
- 为什么注册登录 handler 要走 business thread？
- 为什么登录失败返回统一错误？
- 为什么登录成功要绑定 SessionManager？
- 为什么先不做 JWT / OAuth？

## 10. 面试常见追问

### Q1：为什么 AuthService 不直接依赖 MySqlStorage / RedisCache？

`AuthService` 依赖 `IStorage` 和 `ICache`，这样业务层只关心“创建用户、查用户、登录限流、在线状态”这些能力，不关心底层是具体 DAO、Redis 命令还是测试 fake。真实运行时注入 `MySqlStorage` / `RedisCache`，单元测试注入 fake。

### Q2：为什么注册登录 handler 要走 business thread？

注册登录会做密码哈希、MySQL 查询/插入、Redis 读写，这些都是可能阻塞或较重的操作。Reactor I/O 线程只负责连接读写和 packet decode，不能被数据库或密码计算拖住。

### Q3：为什么登录失败返回统一错误？

用户名不存在和密码错误都返回 `invalid username or password`，避免客户端通过错误消息枚举用户是否存在。内部仍会记录登录失败计数。

### Q4：为什么登录成功要绑定 SessionManager？

后续好友、私聊、群聊都需要知道“这个 TCP 连接代表哪个用户”。`SessionManager` 提供 `session_id -> user_id` 和 `user_id -> session` 两个方向查询。

### Q5：为什么先不做 JWT / OAuth？

LiteIM 第一版是自研 TLV 长连接 IM。客户端登录后，服务端通过连接上的 `Session` 维护登录态。JWT / OAuth 适合 HTTP API 或多服务身份传递，不属于当前 Step 的核心目标。
