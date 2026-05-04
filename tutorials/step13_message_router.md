# Step 13：实现 MessageRouter 心跳基础

本步骤目标：新增业务层分发器 `MessageRouter`，让服务端收到 `Packet` 后可以根据 `msg_type` 做第一版业务分发。

Step 13 只处理两个结果：

- 收到 `HEARTBEAT_REQ`，返回 `HEARTBEAT_RESP`。
- 收到暂不支持的 `msg_type`，返回 `ERROR_RESP`。

本 Step 不做注册、登录、私聊、群聊、数据库、用户绑定，也不做心跳超时清理。它只打通“网络层收到完整 Packet 后交给业务层，业务层再通过 Session 回包”的最小链路。

## 1. 为什么需要 MessageRouter

Step 11 的 `Session` 已经可以从 TCP 字节流中解出完整 `Packet`，Step 12 的 `TcpServer` 已经可以管理所有连接，并把 `Session` 收到的消息通过 message callback 交给上层。

但是还缺一个业务入口：

- 谁判断 `packet.header.msg_type` 是心跳、登录还是聊天？
- 未知消息类型应该静默丢弃，还是明确返回错误？
- 网络层和业务层的边界应该放在哪里？

如果把这些逻辑写进 `Session`，`Session` 就会同时负责 socket I/O、协议解包和业务处理，职责会变混乱。

所以 Step 13 新增 `MessageRouter`：

```text
Session
    ↓ 解出 Packet
TcpServer
    ↓ message callback
MessageRouter
    ↓ route by msg_type
Session::sendPacket(response)
```

这样网络层只负责连接和收发，业务层只负责根据消息类型分发。

## 2. 本 Step 新增和修改的文件

新增：

```text
include/liteim/service/MessageRouter.hpp
src/service/MessageRouter.cpp
tests/test_message_router.cpp
tutorials/step13_message_router.md
```

修改：

```text
src/CMakeLists.txt
server/CMakeLists.txt
server/main.cpp
tests/CMakeLists.txt
tests/test_main.cpp
README.md
docs/architecture.md
docs/project_layout.md
docs/interview_notes.md
tutorials/README.md
```

`MessageRouter` 放在 `service` 模块中，因为它已经开始处理业务语义。它依赖 `net::Session` 的发送接口，但网络层不会反过来依赖 service 层。

## 3. 整体调用流程

服务端入口现在会创建 router：

```cpp
liteim::net::EventLoop loop;
liteim::net::TcpServer server(&loop, "0.0.0.0", 9000);
liteim::service::MessageRouter router;
```

然后把 router 接到 `TcpServer` 的消息回调：

```cpp
server.setMessageCallback([&router](
                              liteim::net::Session& session,
                              const liteim::protocol::Packet& packet) {
    router.route(session, packet);
});
```

客户端发来完整包之后：

```text
conn fd 可读
    ↓
Session::handleRead()
    ↓
FrameDecoder 解出 Packet
    ↓
TcpServer::handleSessionMessage()
    ↓
MessageRouter::route()
    ↓
Session::sendPacket()
```

注意：`MessageRouter` 没有拿到 fd，也没有访问 `TcpServer` 的 sessions map。它只拿到当前消息所属的 `Session&`，然后通过 `Session::sendPacket()` 回复。

## 4. MessageRouter.hpp 讲解

### route()

```cpp
void route(net::Session& session, const protocol::Packet& packet) const;
```

作用：

- 接收一个已经解码完成的请求包。
- 查看 `packet.header.msg_type`。
- 根据消息类型构造响应包。
- 通过 `session.sendPacket()` 把响应交回网络层。

输入：

- `session`：当前请求来自哪个连接。
- `packet`：当前请求包。

输出：

- 没有返回值。响应通过 `Session::sendPacket()` 异步写出。

副作用：

- 会向 `Session` 的输出缓冲区追加响应数据。
- 会让 `Session` 开启写事件，等待 `EventLoop` 派发可写回调。

边界情况：

- 如果请求是 `HEARTBEAT_REQ`，返回 `HEARTBEAT_RESP`。
- 如果请求是未知 `msg_type`，返回 `ERROR_RESP`。
- 如果 `Session` 已经关闭，`Session::sendPacket()` 会抛出异常；router 不吞掉这个错误。

### makeHeartbeatResponse()

```cpp
protocol::Packet makeHeartbeatResponse(const protocol::Packet& request) const;
```

作用：

- 构造心跳响应包。

输入：

- `request`：原始心跳请求包。

输出：

- 一个 `msg_type = HEARTBEAT_RESP` 的 `Packet`。

副作用：

- 没有副作用，只构造对象。

边界情况：

- 当前 Step 的心跳响应 body 为空。
- 响应会保留请求的 `seq_id`，方便客户端做请求/响应关联。

### makeErrorResponse()

```cpp
protocol::Packet makeErrorResponse(const protocol::Packet& request) const;
```

作用：

- 构造未知消息类型的错误响应。

输入：

- `request`：原始请求包。

输出：

- 一个 `msg_type = ERROR_RESP` 的 `Packet`。

副作用：

- 没有副作用，只构造对象。

边界情况：

- 响应 body 固定为 `"unknown message type"`。
- 响应同样保留请求的 `seq_id`。

## 5. MessageRouter.cpp 讲解

### makeResponse()

```cpp
protocol::Packet makeResponse(
    const protocol::Packet& request,
    protocol::MsgType response_type,
    std::string body);
```

这是 `.cpp` 里的匿名命名空间辅助函数。

作用：

- 把“构造响应包”的公共逻辑集中起来。
- 设置响应 `msg_type`。
- 复制请求的 `seq_id`。
- 设置响应 body。

为什么要保留 `seq_id`？

后续客户端可能同时发多个请求。响应回来时，如果没有关联字段，客户端很难知道哪个响应对应哪个请求。保留 `seq_id` 是协议层常见做法。

### route() 的 switch 分发

核心代码是按消息类型分发：

```cpp
switch (packet.header.msg_type) {
case protocol::toUint16(protocol::MsgType::HEARTBEAT_REQ):
    session.sendPacket(makeHeartbeatResponse(packet));
    break;
default:
    session.sendPacket(makeErrorResponse(packet));
    break;
}
```

这里没有提前实现登录、私聊或群聊，因为这些功能需要存储接口、用户身份和业务 service 支撑。Step 13 只先证明分发链路是通的。

## 6. CMake 变化

`src/CMakeLists.txt` 新增了一个库：

```cmake
add_library(liteim_service
    service/MessageRouter.cpp
)
```

它链接：

```cmake
target_link_libraries(liteim_service PUBLIC
    liteim_net
    liteim_protocol
)
```

依赖方向是：

```text
liteim_service -> liteim_net -> liteim_protocol
```

这表示业务层可以使用网络层的 `Session`，但网络层不依赖业务层。这样 `Session` 和 `TcpServer` 仍然是通用网络组件。

`server/CMakeLists.txt` 和 `tests/CMakeLists.txt` 都链接了 `liteim_service`，因为服务端入口和测试都需要使用 `MessageRouter`。

## 7. 测试说明

本 Step 新增：

```text
tests/test_message_router.cpp
```

测试不是只 mock 一个函数调用，而是使用真实的：

- `socketpair()`
- `EventLoop`
- `Session`
- `Channel`
- `FrameDecoder`
- `MessageRouter`

这样可以验证 router 确实通过 `Session::sendPacket()` 走网络层输出缓冲，而不是绕过网络层。

### 测试 1：心跳请求返回心跳响应

用例名：

```text
MessageRouter replies to heartbeat requests
```

验证内容：

- 请求 `msg_type` 是 `HEARTBEAT_REQ`。
- 响应 `msg_type` 必须是 `HEARTBEAT_RESP`。
- 响应 `seq_id` 必须等于请求 `seq_id`。
- 响应 body 为空。

为什么要测：

这是 Step 13 的核心功能。如果心跳响应不通，后续客户端在线保持和 Qt 心跳都无法建立在这条链路上。

### 测试 2：空 body 心跳也能响应

用例名：

```text
MessageRouter accepts empty heartbeat bodies
```

验证内容：

- 心跳请求 body 为空时仍然正常返回 `HEARTBEAT_RESP`。
- `seq_id` 仍然保留。
- 响应 body 仍然为空。

为什么要测：

心跳包通常不需要业务负载，空 body 是正常路径，不应该被当成错误。

### 测试 3：未知消息类型返回错误响应

用例名：

```text
MessageRouter rejects unknown message types
```

验证内容：

- 未知 `msg_type` 返回 `ERROR_RESP`。
- 响应 `seq_id` 保留请求值。
- 响应 body 是 `"unknown message type"`。

为什么要测：

未知消息不能静默失败。返回确定的错误响应后，客户端和测试都能知道服务端已经收到请求，只是当前不支持这个类型。

### 如何运行测试

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
```

测试通过说明：

- `MessageRouter` 能正确识别 `HEARTBEAT_REQ`。
- 未知类型有明确错误响应。
- 响应包能通过真实 `Session` 写到对端。
- 新增 `liteim_service` 与现有 `net` / `protocol` 模块链接关系正确。

## 8. 面试时怎么讲

可以这样说：

> 我在网络层之上加了一层 `MessageRouter`，它只负责根据 `Packet.header.msg_type` 做业务分发。`Session` 负责 socket 读写和解包，`TcpServer` 负责连接管理，`MessageRouter` 负责业务入口。第一版只实现了 `HEARTBEAT_REQ -> HEARTBEAT_RESP`，未知消息返回 `ERROR_RESP`。router 不直接操作 fd，也不保存 Session，只通过 `Session::sendPacket()` 回包，这样网络层和业务层边界比较清楚。

重点是讲清楚分层：

- `Session`：一个连接的读、写、解包、关闭。
- `TcpServer`：连接集合和服务端生命周期。
- `MessageRouter`：按消息类型分发业务请求。
- 后续 `AuthService` / `ChatService`：具体业务规则。
- 后续 `IStorage` / SQLite：数据持久化。

## 9. 面试常见追问

### 为什么 MessageRouter 不直接操作 fd？

因为 fd 生命周期属于网络层。router 如果直接拿 fd 写数据，就会绕过 `Session` 的输出缓冲区、写事件管理和关闭状态检查，导致短写、关闭和事件状态都难维护。

### 为什么不是让 TcpServer 直接 switch msg_type？

`TcpServer` 的职责是组合 `Acceptor` 和 `Session`，维护连接集合。业务类型会越来越多，如果都塞进 `TcpServer`，它会变成一个混杂的上帝类。单独拆 `MessageRouter` 后，业务分发可以独立测试，也方便后续接 `AuthService`、`ChatService`。

### 为什么未知消息要返回 ERROR_RESP？

静默丢弃会让客户端不知道是网络问题、协议问题还是服务端不支持。返回 `ERROR_RESP` 能给客户端明确反馈，也方便自动化测试定位问题。

### 为什么心跳响应 body 为空？

当前 Step 只是证明心跳链路打通，不需要携带额外业务数据。后续如果客户端需要服务端时间戳或延迟统计，可以在协议层约定 body 内容。

### 心跳超时清理为什么不在 Step 13 做？

心跳响应和心跳超时是两个问题。Step 13 只负责收到心跳后回包；超时清理需要定时器结构和时间轮/堆，后续 Step 20 会用 `timerfd` / `TimerHeap` 接入。
