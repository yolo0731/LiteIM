# Step 41：CLI 测试客户端

## 0. 本 Step 结论

- 目标：Step 41 增加 `liteim_cli`，作为服务端协议调试和手工联调工具。
- 前置依赖：依赖 Step 3-6 的 MessageType / TLV / Packet / FrameDecoder 协议契约，以及 Step 34-40 的业务请求处理。
- 主要交付：新增 `client_cli/`、`liteim_client_cli` helper 库、`liteim_cli` 可执行文件和 CLI 协议测试。
- 运行边界：CLI 连接真实 TCP server，发送普通 TLV 请求，打印 response / push，不绕过服务端协议。
- 心跳边界：CLI 连接后后台每 30 秒发送一次 `HeartbeatRequest`；用户也可以手动输入 `heartbeat`。

## 1. 为什么需要这个 Step

到 Step 40 为止，LiteIM 服务端已经能处理注册、登录、好友、私聊、群聊、离线消息、历史消息和心跳。但是没有一个稳定的命令行工具直接按 TLV 协议连上 server。

Step 41 的作用是：

- 给服务端提供一个稳定的协议调试入口。
- 在 Qt 客户端完成前，可以手动验证业务链路。
- 让后续 PersonaAgent BotClient 参考一套最小的普通客户端收发流程。
- 把“协议构包”和“命令行交互”分开，方便测试。

## 2. 本 Step 边界

### 本 Step 做

- 新增 `client_cli/ClientCli.hpp` 和 `ClientCli.cpp`。
- 新增 `buildPacketFromLine()`，把文本命令转换成 `Packet`。
- 新增 `describePacket()`，把 response / push TLV 字段打印成人可读文本。
- 新增 `ProtocolClient`，提供阻塞 TCP `connectTo()`、`sendPacket()`、`readPacket()` 和 `close()`。
- 新增 `liteim_cli` 入口，支持 `--host` / `--port`。
- 支持 register、login、logout、add-friend、accept-friend、reject-friend、friends、private、private-id、create-group、join-group、groups、group、history、offline、offline-ack、delivery-ack、heartbeat、help、quit。
- 新增 CLI 单元测试和本地 loopback TCP 发送测试。

### 本 Step 不做

- 不做 curses/TUI 图形化终端界面。
- 不做本地联系人缓存、会话列表持久化或消息数据库。
- 不做自动重连、断点续传或 read receipt。Step 53 之后 CLI 已支持离线消息 `offline-ack`；Step 54 之后 CLI 已支持带 `client_msg_id` 的 `private-id` 调试命令；Step 55 之后 CLI 已支持私聊接收方 `delivery-ack`。
- 不做 PersonaAgent BotClient、Qt 客户端 或 benchmark。
- 不改变服务端协议类型、TLV 字段或业务 handler。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `client_cli/CMakeLists.txt` | 新增 | 生成 `liteim_client_cli` 和 `liteim_cli` |
| `client_cli/ClientCli.hpp` | 新增 | 暴露 CLI 命令构包、包描述和阻塞 TCP client |
| `client_cli/ClientCli.cpp` | 新增 | 实现命令解析、TLV 构造、Packet 打印和 socket 收发 |
| `client_cli/main.cpp` | 新增 | 实现交互循环、接收线程和 30 秒心跳线程 |
| `tests/client_cli/cli_protocol_test.cpp` | 新增 | 覆盖命令构包、packet 描述和 loopback 发送 |
| `CMakeLists.txt` | 修改 | 接入 `client_cli/` |
| `tests/CMakeLists.txt` | 修改 | 编译 CLI 测试并链接 `liteim_client_cli` |
| `README.md` / planning 文件 | 更新 | 记录 Step 41 CLI 用法、边界和验证结果 |

## 4. 核心接口与契约

### `buildPacketFromLine()`

```cpp
Status buildPacketFromLine(const std::string& line,
                           std::uint64_t seq_id,
                           Packet& packet);
```

契约：

- 输入一行文本命令，输出一个普通 LiteIM `Packet`。
- `seq_id` 由调用者提供，函数只把它写入 header。
- 命令参数不合法时返回 `InvalidArgument`。
- 不连接网络、不访问 MySQL/Redis、不保存 CLI 状态。

示例：

```text
private 1002 hello bob from cli
```

会生成：

```text
MessageType = PrivateMessageRequest
seq_id = 调用者传入的值
ReceiverId = 1002
MessageText = "hello bob from cli"
```

如果要手工验证 Step 54 的重试幂等，可以使用：

```text
private-id 1002 cli-msg-1 hello bob from cli
```

它会在同一个 `PrivateMessageRequest` 中额外携带：

```text
ClientMessageId = "cli-msg-1"
```

### `describePacket()`

```cpp
std::string describePacket(const Packet& packet);
```

契约：

- 读取 packet header 和 TLV body，输出一行调试文本。
- 支持重复 TLV 字段，例如 history/offline response 中的多条消息。
- 如果 TLV body 解析失败，不抛异常，返回包含 `parse_error` 的文本。

### `ProtocolClient`

```cpp
class ProtocolClient {
public:
    Status connectTo(const std::string& host, std::uint16_t port);
    Status sendPacket(const Packet& packet);
    Status readPacket(Packet& packet);
    void close() noexcept;
};
```

契约：

- 使用阻塞 TCP socket，适合作为调试客户端。
- `sendPacket()` 先调用 `encodePacket()`，再完整写入 socket。
- `readPacket()` 先读固定 20 字节 header，再按 `body_len` 读完整 body。
- `close()` 会 `shutdown()` 并关闭 fd，便于退出时打断接收线程。

## 5. 运行流程

### 1. 手动私聊

真实命令例子：

```text
register cli_alice secret CLI Alice
login cli_alice secret
logout
private 1002 hello bob
```

流程：

```text
stdin line
    -> buildPacketFromLine()
    -> Packet(LoginRequest / LogoutRequest / PrivateMessageRequest)
    -> ProtocolClient::sendPacket()
    -> liteim_server MessageRouter
    -> AuthService / ChatService business handler
    -> server response / push
    -> ProtocolClient::readPacket()
    -> describePacket()
    -> stdout
```

### 2. 手动群聊

真实命令例子：

```text
register cli_alice secret CLI Alice
login cli_alice secret
join-group 2001
group 2001 hello team
```

如果新注册用户成功加入 `group_id=2001`，服务端会保存用户群消息，并向在线群成员发送普通 `GroupMessagePush`。CLI 只看到普通 `GroupMessageResponse` 和 `GroupMessagePush`，不需要理解任何额外专用协议。

### 3. 后台心跳

CLI 连接成功后启动一个后台线程：

```text
every 30s
    -> build HeartbeatRequest
    -> sendPacket()
```

这对应 Step 40 的协议语义：`HeartbeatResponse` 表示服务端收到合法心跳包；Redis TTL 刷新是服务端对已登录用户的降级副作用，不由 CLI 判断。

## 6. 关键实现点

### 1. 命令解析只生成普通协议包

CLI 不新增协议。比如：

```text
friends       -> ListFriendsRequest
logout        -> LogoutRequest
accept-friend 1001
              -> AcceptFriendRequest + TargetUserId(requester_id)
reject-friend 1001
              -> RejectFriendRequest + TargetUserId(requester_id)
groups        -> ListGroupsRequest
offline 20    -> OfflineMessagesRequest + Limit
history group 2001 20 5003
              -> HistoryRequest + ConversationType + ConversationId + Limit + MessageId
offline-ack 5001 5002
              -> OfflineMessagesAckRequest + repeated MessageId
private-id 1002 cli-msg-1 hello
              -> PrivateMessageRequest + ReceiverId + ClientMessageId + MessageText
delivery-ack 5001
              -> DeliveryAckRequest + MessageId
```

这样 CLI 先固定一套普通账号协议；后续 Qt Client 和 PersonaAgent BotClient 必须复用这套协议，而不是让服务端为外部 Agent 增加专用分支。

### 2. 发送和接收分离

`main.cpp` 中主线程只读 stdin 并发送请求；接收线程持续调用 `readPacket()` 打印 response / push。原因是 IM 客户端会收到服务端主动 push，例如 `PrivateMessagePush` 和 `GroupMessagePush`。

### 3. helper 库可测试，入口只做编排

核心逻辑放在 `liteim_client_cli`：

- 命令行到 `Packet` 的转换。
- `Packet` 到文本的转换。
- TCP 连接和收发。

`main.cpp` 只负责参数解析、线程启动和用户输入循环。这样测试不用启动完整 server 也能验证 CLI 的协议行为。

### 4. 第一版使用阻塞 socket

这是调试客户端，不是高性能 server。阻塞 socket 让实现直接、可读，也避免在 Step 41 引入另一个客户端 Reactor。服务端的非阻塞 Reactor 设计不受影响。

## 7. 测试设计

| 风险 | 测试覆盖 |
| --- | --- |
| login 命令 TLV 错误 | 断言 `LoginRequest` 包含 `Username` / `Password` |
| logout 命令误注册但 CLI 不能构造 | 断言 `logout` 生成空 body 的 `LogoutRequest` |
| 私聊正文丢失空格 | 断言 `private 1002 hello bob from cli` 的 `MessageText` 保留完整文本 |
| 私聊幂等字段漏带 | 断言 `private-id 1002 cli-msg-1 hello bob` 生成 `ClientMessageId` 和 `MessageText` |
| history 游标字段错配 | 断言 group history 生成 `ConversationType` / `ConversationId` / `Limit` / `MessageId` |
| offline ACK 字段错配 | 断言 `offline-ack 5001 5002` 生成 `OfflineMessagesAckRequest` 和重复 `MessageId` |
| private delivery ACK 字段错配 | 断言 `delivery-ack 5001` 生成 `DeliveryAckRequest` 和 `MessageId` |
| push 打印看不出关键字段 | 断言 `describePacket()` 包含 message id、sender、receiver、client message id 和 text |
| TCP 编码发送失败 | loopback fake server 接收 CLI 发出的 `HeartbeatRequest` |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
./build/tests/liteim_tests --gtest_filter='ClientCli*'
cmake --build build --target liteim_cli -j2
./build/client_cli/liteim_cli --help
ctest --test-dir build --output-on-failure
git diff --check
```

如果要手工联调真实服务端：

```bash
docker compose -f docker/docker-compose.yml up -d --wait
./build/server/liteim_server
./build/client_cli/liteim_cli --host 127.0.0.1 --port 9000
```

## 9. 面试表达

> Step 41 增加了一个命令行协议客户端。它不是 TUI，而是稳定的 TLV 调试工具：把文本命令转成普通 `Packet`，通过阻塞 TCP 连接发给 LiteIM server，后台线程持续读取 response 和 push 并打印字段，同时每 30 秒发送心跳。核心构包和收发逻辑放在 `liteim_client_cli` 库里，入口只负责交互编排，所以可以用单元测试验证 login/private/history 构包、packet 描述和 loopback 发送。

## 10. 面试常见追问

### 为什么 CLI 不复用服务端的 Reactor？

CLI 是调试工具，目标是简单可靠地验证协议，不是承载大量连接。服务端已经用非阻塞 Reactor 解决高并发问题；客户端第一版用阻塞 socket 可以降低复杂度，也更适合作为协议参考。

### 为什么需要接收线程？

IM 连接不是纯 request/response。用户发送私聊后，另一个客户端可能收到 `PrivateMessagePush`；群聊消息也会主动 push。如果 CLI 只在发送后同步等一次响应，就会漏掉这些服务端主动消息。

### 为什么 `describePacket()` 不把 history/offline 消息分组？

当前 TLV body 是重复字段流，不带嵌套 message 对象。`describePacket()` 的目标是调试可见性，不改变协议结构。后续如果客户端 UI 需要严格分组，可以在客户端侧按字段重复序列做更强解析。

### CLI 和后续 PersonaAgent BotClient 有什么关系？

CLI 是当前已经实现的手工调试客户端；PersonaAgent BotClient 是后续项目二要实现的 Python 长连接客户端。两者都走同一套普通账号协议：注册/登录、接收 `PrivateMessagePush`，再按普通私聊发送回复。LiteIM 不知道这个账号背后是真人、脚本还是 LLM。
