# Step 33：MessageRouter 异步分发框架

## 0. 本 Step 结论

- 目标：Step 33 的目标是给业务层补一个统一入口：Session 收到完整 Packet 后，不再直接 echo，而是交给 MessageRouter 按 msg_type 分发。
- 前置依赖：依赖 Step 0-32 已建立的工程、协议或运行时基础。
- 主要交付：`MessageRouter 异步分发框架` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

Step 33 的目标是给业务层补一个统一入口：`Session` 收到完整 `Packet` 后，不再直接 echo，而是交给 `MessageRouter` 按 `msg_type` 分发。

这一 Step 只实现“路由框架”，不实现真实注册登录和聊天业务。AuthService 从 Step 34 开始，ChatService 私聊从 Step 36 开始。

### 概念

网络层已经能做到：

```text
socket read
    -> FrameDecoder
    -> Packet
    -> TcpServer::setMessageCallback()
```

但 `TcpServer` 是网络模块，它只应该管理连接、I/O loop、Session 生命周期和心跳超时，不应该知道“登录请求要查 MySQL、私聊要写消息表、群聊要查成员”这些业务规则。

所以 Step 33 增加 `MessageRouter`：

```text
TcpServer / Session
    -> MessageRouter
        -> inline handler
        -> business ThreadPool handler
            -> Session::sendPacket()
```

Router 解决四个问题：

- 按 `MessageType` 找到对应 handler。
- 统一解析 TLV body，handler 不再重复写 parse 入口。
- 决定轻量请求留在当前线程，重业务请求投递到业务线程池。
- 统一错误响应和 `seq_id` 回填，客户端能用请求 seq 匹配响应。

本 Step 默认只提供一个真实 handler：

```text
HeartbeatRequest -> HeartbeatResponse
```

它不访问 Redis。完整的“已登录用户心跳刷新在线 TTL”留到 Step 40 的 HeartbeatService。

边界：

- 不实现 `RegisterRequest` / `LoginRequest` 的真实业务。
- 不调用 MySQL / Redis。
- 不在业务线程直接操作 fd、`Channel` 或 `Buffer`。
- 不让 `TcpServer` 持有 Router，避免 `net` 模块依赖 `service` 模块。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `MessageRouter 异步分发框架` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/service/MessageRouter.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/service/MessageRouter.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/service/message_router_test.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/tutorials/step33_message_router.md` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/service/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `server/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `server/main.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `/home/yolo/jianli/PROJECT_MEMORY.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `PROJECT_MEMORY.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

### MessageRouter.hpp

```cpp
class MessageRouter {
public:
    enum class DispatchMode {
        Inline,
        BusinessThread,
    };

    struct RouterRequest {
        Session::Ptr session;
        Packet packet;
        TlvMap fields;
    };

    using Handler = std::function<Status(const RouterRequest&, Packet&)>;

    explicit MessageRouter(ThreadPool& business_pool);

    Status registerHandler(MessageType type, Handler handler, DispatchMode mode);
    void route(Session::Ptr session, Packet packet);
};
```

#### `DispatchMode`

`DispatchMode::Inline` 表示 handler 在当前调用线程执行。

适合轻量逻辑：

```text
HeartbeatRequest
简单协议校验
不访问 MySQL / Redis 的快速响应
```

`DispatchMode::BusinessThread` 表示 handler 被投递到业务 `ThreadPool`。

适合可能阻塞或较重的逻辑：

```text
LoginRequest
RegisterRequest
PrivateMessageRequest
HistoryRequest
```

这不是“handler 可以随便操作 Session 内部状态”的许可。业务线程只能通过 `Session::sendPacket()` 回包，不能直接访问 fd、`Buffer` 或 `Channel`。

#### `RouterRequest`

```cpp
struct RouterRequest {
    Session::Ptr session;
    Packet packet;
    TlvMap fields;
};
```

`session` 是当前请求来源连接。Inline handler 通常在 I/O 线程中使用它；BusinessThread handler 拿到的是 Router 在任务开始时从 `weak_ptr` 重新锁出来的 `shared_ptr`。

`packet` 是请求包副本，保留原始 header 和 body。handler 可以读取：

```text
packet.header.msg_type
packet.header.seq_id
packet.body
```

`fields` 是 Router 统一解析后的 TLV map。handler 不需要再调用 `parseTlvMap()`，只需要用 `getString()` / `getUint64()` 等读取字段。

#### `Handler`

```cpp
using Handler = std::function<Status(const RouterRequest&, Packet&)>;
```

handler 的职责是：

1. 从 `RouterRequest` 读字段。
2. 填写 `response.header.msg_type`。
3. 填写 `response.body`。
4. 返回 `Status::ok()` 或错误状态。

handler 不需要设置响应 `seq_id`。即使 handler 填错，Router 也会强制覆盖为请求 `seq_id`。

例如 Step 34 的登录 handler 将来大致会是：

```cpp
router.registerHandler(
    MessageType::LoginRequest,
    [&](const MessageRouter::RouterRequest& request, Packet& response) {
        std::string username;
        std::string password;
        const auto username_status = getString(request.fields, TlvType::Username, username);
        if (!username_status.isOk()) {
            return username_status;
        }

        response.header.msg_type = MessageType::LoginResponse;
        return appendUint64(TlvType::UserId, 1001, response.body);
    },
    MessageRouter::DispatchMode::BusinessThread);
```

#### `MessageRouter(ThreadPool&)`

Router 不拥有业务线程池，只保存引用。

构造函数会注册默认心跳 handler：

```text
HeartbeatRequest -> HeartbeatResponse
DispatchMode::Inline
```

所以 server/main 只创建 Router，就已经能处理心跳请求。

#### `registerHandler()`

```cpp
Status registerHandler(MessageType type, Handler handler, DispatchMode mode);
```

注册某个 request 类型的 handler。

失败语义：

- `MessageType::Unknown` 返回 `InvalidArgument`。
- 非 request 类型返回 `InvalidArgument`。
- 空 handler 返回 `InvalidArgument`。

同一个 request 类型重复注册时，新 handler 覆盖旧 handler。这样后续 Step 可以替换默认心跳 handler，或在测试里注册 fake handler。

#### `route()`

```cpp
void route(Session::Ptr session, Packet packet);
```

这是 `TcpServer::setMessageCallback()` 调用的入口。

执行顺序：

1. session 为空或已关闭：直接返回。
2. `msg_type` 不是 request：发送 `ErrorResponse`。
3. TLV body parse 失败：发送 `ErrorResponse`。
4. 查找 handler；未注册：发送 `ErrorResponse`。
5. Inline：当前线程直接执行 handler。
6. BusinessThread：把任务投递到 `ThreadPool`。
7. handler 成功：发送 handler 填好的 response，并强制 response `seq_id` 等于请求 `seq_id`。
8. handler 失败：发送 `ErrorResponse`。

#### 关键 private 成员

```cpp
ThreadPool& business_pool_;
std::mutex mutex_;
std::unordered_map<std::uint16_t, HandlerEntry> handlers_;
```

`business_pool_` 是外部业务线程池引用。Router 不负责创建或销毁线程池。

`handlers_` 保存 `msg_type -> handler + dispatch mode`。key 使用 `std::uint16_t`，避免 enum hash 的兼容性问题。

`mutex_` 保护 handler 表。通常 server 启动前注册 handler，但测试和后续扩展允许并发 route 时读表。

#### 关键 private helper

- `executeHandler()`：统一捕获 handler 异常、处理 handler 返回状态、发送成功或错误响应。
- `sendError()`：统一把 `Status` 编成 `ErrorResponse`，body 写入 `TlvType::ErrorCode` 和 `TlvType::ErrorMessage`。
- `sendResponse()`：统一设置 packet magic/version/flags/seq_id，并通过 `Session::sendPacket()` 回写。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

Step 33 之后，server 收到 Packet 的默认路径变成：

```text
Session::handleRead()
    -> message_callback_(session, packet)
    -> TcpServer::handleMessage()
    -> MessageRouter::route(session, packet)
```

Router 是 Step 34-40 的业务入口骨架：

- Step 34：注册登录 handler。
- Step 35：好友 handler。
- Step 36：私聊 handler。
- Step 38：群聊 handler。
- Step 39：历史消息 handler。
- Step 40：完整心跳 service handler。

### 2. 上下层调用关系

```text
上层业务 service
    AuthService / ChatService / GroupService

MessageRouter
    统一分发 Packet
    统一解析 TLV
    统一错误响应
    统一 ThreadPool 投递

下层网络
    TcpServer
    Session
    EventLoop
```

`TcpServer` 只通过 callback 知道 Router：

```cpp
server.setMessageCallback([&router](const Session::Ptr& session, const Packet& packet) {
    router.route(session, packet);
});
```

这样 `liteim_net` 不需要链接 `liteim_service`，模块方向仍然干净。

### 3. 整体运行链路

以登录请求为例，后续 Step 34 会注册：

```text
LoginRequest -> BusinessThread handler
```

运行时：

```text
client sends LoginRequest(seq_id=7)
    -> IO thread decodes Packet
    -> MessageRouter parses TLV fields
    -> MessageRouter submits task to business ThreadPool
    -> business worker runs AuthService
    -> AuthService fills LoginResponse
    -> MessageRouter forces seq_id=7
    -> session->sendPacket(response)
    -> Session queues write back to owner EventLoop
    -> IO thread writes encoded response to socket
```

### 4. MessageRouter 自身内部运行流程

`route()` 的核心流程可以看成：

```text
validate session
validate request msg_type
parse TLV body
lookup handler
    no handler -> ErrorResponse
dispatch
    Inline -> execute now
    BusinessThread -> ThreadPool::submit()
handler result
    ok -> response
    error -> ErrorResponse
```

业务线程里的 session 处理是两次检查：

```text
task starts
    weak_ptr.lock()
    if null/closed -> return

handler done
    if session closed -> return
    session->sendPacket(response)
```

这保证了客户端断开后，慢业务 handler 完成时不会向已关闭 session 发送，也不会因为悬空裸指针崩溃。

### 5. 该项目代码在实际应用中的具体数据例子

客户端发送登录请求：

```text
msg_type = LoginRequest
seq_id = 7
body:
    Username = "alice"
    Password = "123456"
```

Step 33 只负责路由：

```text
fields[Username] = "alice"
fields[Password] = "123456"
dispatch = BusinessThread
```

Step 34 的 AuthService 将来返回：

```text
msg_type = LoginResponse
seq_id = 7
body:
    UserId = 1001
    SessionId = 42
```

如果当前没有注册登录 handler，Step 33 会返回：

```text
msg_type = ErrorResponse
seq_id = 7
body:
    ErrorCode = NotFound
    ErrorMessage = "no message handler registered"
```

心跳请求现在走轻量路径：

```text
HeartbeatRequest(seq_id=8)
    -> HeartbeatResponse(seq_id=8)
```

它不会刷新 `online:user:1001` 的 Redis TTL。这个行为要等 Step 40。

### 6. 线程 / 生命周期 / 所有权注意点

- Inline handler 通常运行在 `Session` 所属 I/O 线程，所以只能做轻量工作。
- BusinessThread handler 运行在业务线程池，不能直接碰 `Session` 内部状态。
- Router 异步任务捕获 `weak_ptr<Session>`，不延长已关闭连接的生命周期。
- 成功响应和错误响应都通过 `Session::sendPacket()` 回到 owner loop。
- Router 不读写 fd，不调用 `pendingOutputBytes()`，不操作 `Channel`，不访问 `Buffer`。

## 6. 关键实现点

- 保持模块职责单一。
- 失败时返回清晰错误，不吞掉异常状态。
- 不跨越本 Step 边界提前实现后续业务。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `MessageRouter 异步分发框架` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

新增 `tests/service/message_router_test.cpp`，覆盖 8 个行为：

- `MessageRouterTest.HeaderIsSelfContained`：头文件可独立包含，public API 形状稳定。
- `MessageRouterTest.HeartbeatRequestUsesInlineHandlerWithoutStartingBusinessPool`：心跳不启动业务线程池也能返回 `HeartbeatResponse`，并保留 `seq_id`。
- `MessageRouterTest.BusinessThreadHandlerRunsOnWorkerAndSendsResponse`：`LoginRequest` 注册为业务线程 handler 后确实在 worker 线程执行，并通过 session 回包。
- `MessageRouterTest.UnknownMessageTypeReturnsErrorResponse`：未知消息类型返回 `ErrorResponse`。
- `MessageRouterTest.MalformedTlvReturnsErrorResponse`：TLV body 解析失败返回 `ErrorResponse`。
- `MessageRouterTest.HandlerErrorReturnsErrorResponse`：handler 返回错误时转换成 `ErrorResponse`。
- `MessageRouterTest.HandlerResponseSeqIdIsOverriddenWithRequestSeqId`：handler 填错 seq 时 Router 强制使用请求 seq。
- `MessageRouterTest.ClosedSessionBeforeAsyncCompletionDoesNotSendResponse`：异步 handler 完成前 session 已关闭时不发送、不崩溃。

TDD 过程：

```bash
cmake --build build --target liteim_tests -j2
```

RED 失败点：

```text
fatal error: liteim/service/MessageRouter.hpp: No such file or directory
```

实现后验证：

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "MessageRouter" --output-on-failure
```

Step33 完整验证命令：

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "MessageRouter|Service|Session|TcpServer" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
timeout 1s ./build/server/liteim_server || test $? -eq 124
```

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "MessageRouter" --output-on-failure
ctest --test-dir build -R "MessageRouter|Service|Session|TcpServer" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
timeout 1s ./build/server/liteim_server || test $? -eq 124
```

## 9. 面试表达

### 一句话

本 Step 的核心是把 `MessageRouter 异步分发框架` 做成边界清楚、可测试、可继续扩展的一层。

### 展开说

围绕为什么需要 `MessageRouter 异步分发框架`、它依赖哪些前置 Step、它暴露什么接口、失败时怎么返回、线程和生命周期边界在哪里展开。

### 容易被追问

- 为什么不让 TcpServer 直接 switch msg_type？
- 为什么 handler 分 Inline 和 BusinessThread？
- 业务线程为什么还能调用 `session->sendPacket()`？
- 为什么异步任务捕获 weak_ptr？
- 为什么 Router 要覆盖响应 seq_id？
- 为什么 HeartbeatRequest 现在不刷新 Redis？

## 10. 面试常见追问

### 为什么不让 TcpServer 直接 switch msg_type？

因为 `TcpServer` 是网络层对象。它负责连接和 I/O，不应该依赖 AuthService、ChatService、MySQL、Redis 等业务模块。把分发放到 `MessageRouter`，可以保持 `net -> service` 的依赖方向只出现在 executable wiring，而不是写进网络库。

### 为什么 handler 分 Inline 和 BusinessThread？

不是所有请求都值得切线程。心跳这种轻量请求可以 inline 返回；登录、历史消息、私聊落库这类可能阻塞的请求必须进业务线程池，避免卡住 I/O loop。

### 业务线程为什么还能调用 `session->sendPacket()`？

`sendPacket()` 是 `Session` 暴露的跨线程安全发送入口。它会把实际写缓冲操作投递回 Session owner loop。业务线程不能直接操作 fd、`Channel` 或 `Buffer`。

### 为什么异步任务捕获 weak_ptr？

如果捕获 `shared_ptr`，一个很慢的业务任务可能把已经断开的 session 额外延长。捕获 `weak_ptr` 后，任务开始时和发送前都要重新确认 session 存活，关闭后就直接丢弃响应。

### 为什么 Router 要覆盖响应 seq_id？

`seq_id` 是客户端匹配请求和响应的关键字段。handler 可以专注业务 body，不应该有机会误填 seq 导致客户端匹配错响应。

### 为什么 HeartbeatRequest 现在不刷新 Redis？

Step33 只做路由框架。完整心跳语义需要结合登录态和 Redis 在线 TTL，放在 Step 40 的 HeartbeatService 里实现，避免本 Step 偷跑后续业务。
