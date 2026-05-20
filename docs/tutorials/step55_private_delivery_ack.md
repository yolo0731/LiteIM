# Step 55：私聊 delivery ACK

## 0. 本 Step 结论

- 目标：给在线私聊 `PrivateMessagePush` 增加接收方 delivery ACK，让服务端知道 Bob 的客户端已经确认收到某条私聊消息。
- 前置依赖：依赖 Step 36 私聊保存和 push、Step 53 的 `message_deliveries` 表、Step 54 的消息幂等字段，以及现有 `ChatService` / `MySqlStorage`。
- 主要交付：新增 `DeliveryAckRequest` / `DeliveryAckResponse`，CLI 新增 `delivery-ack <message_id>`，接收方 ACK 后写 `message_deliveries.status = delivered`。
- 语义边界：发送方收到 `PrivateMessageResponse` 只代表服务端保存/处理成功；接收方发回 `DeliveryAckRequest` 才代表接收端客户端确认收到。
- 范围控制：本 Step 只做私聊 delivery ACK，不做 read receipt、不做群聊全员 ACK、不做跨节点投递确认。

## 1. 为什么需要这个 Step

Step 54 解决的是发送方重试幂等，避免 Alice 因网络超时重复发送同一条消息。但它仍不能证明 Bob 已经收到在线 push：

```text
Alice -> Server: PrivateMessageRequest
Server -> MySQL: 保存 message_id=5001
Server -> Alice: PrivateMessageResponse(message_id=5001)
Server -> Bob: PrivateMessagePush(message_id=5001)
Bob 的 TCP 连接在发送途中断开
```

`Session::sendPacket()` 只表示服务端把 packet 交给所属 I/O loop 和输出缓冲，并不等于 Bob 的客户端已经解析、展示或持久化。TCP ACK 也只是字节级确认，业务层看不到“Bob 的客户端已经处理 message_id=5001”。

本 Step 新增接收方 ACK：

```text
Bob Client 收到 PrivateMessagePush(message_id=5001)
    -> DeliveryAckRequest(message_id=5001)
Server
    -> message_deliveries.status = delivered
    -> DeliveryAckResponse(message_id=5001, delivery_status=2)
```

这样服务端至少能区分：

- server-stored：消息已经写入 `messages`。
- delivered：接收方客户端已经对该 `message_id` 回 ACK。
- read：用户已读，当前保留为后续能力。

## 2. 本 Step 边界

本 Step 做：

- 新增 `MessageType::DeliveryAckRequest = 506`。
- 新增 `MessageType::DeliveryAckResponse = 507`。
- `ChatService` 注册并处理 `DeliveryAckRequest`。
- 接收方 ACK 请求只需要携带 `TlvType::MessageId`。
- `IStorage` 增加 `ackPrivateMessageDelivery(user_id, message_id, message)`。
- `MySqlStorage` 校验 ACK 用户必须是该私聊消息 receiver。
- ACK 成功时 upsert `message_deliveries`，把状态推进到 `delivered`。
- 重复 ACK 幂等，返回同一个 `message_id` 和 delivered 状态。
- CLI 支持 `delivery-ack <message_id>`。
- Python E2E 在 Bob 收到 push 后发送 ACK，并验证重复 ACK 仍成功。

本 Step 不做：

- 不做 read receipt；`read` 状态只保留枚举含义。
- 不做群聊每个成员逐条 ACK。
- 不做 ACK 超时重试队列。
- 不做跨节点路由和跨节点投递确认。
- 不把服务端 `sendPacket()` 成功当成 delivered。
- 不主动通知 Alice “Bob 已 delivered”；本 Step 只记录服务端状态和返回 Bob 的 ACK 响应。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/protocol/MessageType.hpp` / `src/protocol/MessageType.cpp` | 修改 | 新增 delivery ACK 请求/响应类型和分类 |
| `include/liteim/storage/IStorage.hpp` | 修改 | 增加私聊 delivery ACK 存储接口 |
| `include/liteim/storage/MySqlStorage.hpp` / `src/storage/MySqlStorage.cpp` | 修改 | 校验 receiver 身份并 upsert delivered 状态 |
| `include/liteim/service/ChatService.hpp` / `src/service/ChatService.cpp` | 修改 | 注册并实现 `handleDeliveryAck()` |
| `client_cli/ClientCli.cpp` | 修改 | 新增 `delivery-ack` 命令 |
| `tests/protocol/message_type_test.cpp` | 修改 | 覆盖 ACK 类型名称和请求/响应分类 |
| `tests/service/chat_service_test.cpp` | 修改 | 覆盖 receiver ACK、非 receiver 拒绝、重复 ACK |
| `tests/storage/mysql_storage_test.cpp` | 修改 | 覆盖 MySQL delivered 写入和权限拒绝 |
| `tests/client_cli/cli_protocol_test.cpp` | 修改 | 覆盖 CLI ACK 构包 |
| `tests/e2e/liteim_e2e.py` / `tests/e2e/test_private_chat.py` | 修改 | 覆盖在线 push 后 delivery ACK 黑盒流程 |
| `README.md` / `docs/tutorials/step03_protocol_types.md` / `docs/tutorials/step41_cli_client.md` / process 文件 | 修改 | 同步协议、CLI 和过程说明 |

## 4. 核心接口与契约

### 协议类型

```cpp
DeliveryAckRequest = 506,
DeliveryAckResponse = 507,
```

请求示例：

```text
MessageType = DeliveryAckRequest
seq_id = 12
MessageId = 5001
```

响应示例：

```text
MessageType = DeliveryAckResponse
seq_id = 12
MessageId = 5001
DeliveryStatus = 2
```

`DeliveryStatus = 2` 对应 `storage::DeliveryStatus::kDelivered`。

### `ChatService::handleDeliveryAck()`

```cpp
Status handleDeliveryAck(const MessageRouter::RouterRequest& request,
                         Packet& response);
```

契约：

- 必须从当前 `Session` 绑定关系里取得登录用户，不信任客户端传 sender/receiver。
- 请求体必须包含非 0 的 `MessageId`。
- 只允许该私聊消息的 receiver ACK。
- 成功响应保留请求 `seq_id`，返回 `DeliveryAckResponse`。
- 存储层返回 `NotFound` 时，通常表示消息不存在、不是私聊、或当前用户不是 receiver。

### `IStorage::ackPrivateMessageDelivery()`

```cpp
Status ackPrivateMessageDelivery(std::uint64_t user_id,
                                 std::uint64_t message_id,
                                 MessageRecord& message);
```

契约：

- `user_id` 是当前登录用户 id。
- `message_id` 是要 ACK 的服务端消息 id。
- 只处理 `ConversationType::kPrivate`。
- `message.receiver_id` 必须等于 `user_id`。
- ACK 成功后返回完整 `MessageRecord`，便于 service 未来扩展响应字段。

### MySQL 状态推进

本 Step 复用 Step 53 的 `message_deliveries` 表：

```sql
INSERT INTO message_deliveries
    (message_id, user_id, status, pushed_at_ms, delivered_at_ms, read_at_ms)
VALUES
    (?, ?, 2, NULL, ?, NULL)
ON DUPLICATE KEY UPDATE
    status = IF(status > VALUES(status), status, VALUES(status)),
    delivered_at_ms = COALESCE(delivered_at_ms, VALUES(delivered_at_ms));
```

这让重复 ACK 保持幂等：已经 delivered 的 row 不会被降级，第一次 delivered 时间也不会被覆盖。

## 5. 运行流程

在线私聊主流程：

```text
Alice(user_id=1001)
    -> PrivateMessageRequest(receiver_id=1002, text="hello")
    -> ChatService::handlePrivateMessage()
    -> MySQL messages 保存 message_id=5001
    -> Alice 收到 PrivateMessageResponse(message_id=5001)
    -> Bob 在线，Bob Session 收到 PrivateMessagePush(message_id=5001)

Bob(user_id=1002)
    -> 客户端解析 push 并展示/持久化
    -> DeliveryAckRequest(message_id=5001)
    -> ChatService::handleDeliveryAck()
    -> MySqlStorage::ackPrivateMessageDelivery(1002, 5001)
    -> message_deliveries(5001, 1002).status = delivered
    -> Bob 收到 DeliveryAckResponse(message_id=5001, delivery_status=2)
```

非接收方 ACK：

```text
Alice 或 Eve -> DeliveryAckRequest(message_id=5001)
    -> MySqlStorage 查出 message.receiver_id = 1002
    -> 当前 user_id != 1002
    -> 返回 NotFound / ErrorResponse
```

这样不会因为知道 `message_id` 就能替别人 ACK。

## 6. 关键实现点

### 1. ACK handler 放在 `ChatService`

Delivery ACK 是私聊消息语义的一部分，和 `PrivateMessagePush` 同属 chat 业务，不放进 `OfflineMessageService`。离线 ACK 仍由 `OfflineMessageService` 处理，因为它还需要标记 `offline_messages` rows 和清 unread。

### 2. 存储层负责权限兜底

service 层只知道当前登录用户和 message id。真正判断“这个 message 是否私聊、receiver 是否当前用户”必须靠 MySQL 查出的消息事实。这样即使未来多个 service 调用 ACK 接口，也不会绕过权限校验。

### 3. ACK 不改变历史消息

历史消息仍从 `messages` 表读取。delivery 状态写在 `message_deliveries`，所以 ACK 不会改 message text、sender、receiver、conversation id 或 created timestamp。

### 4. 先做私聊，不做群聊全员 ACK

群聊 ACK 会带来 N 个成员的状态和大量 ACK 流量，还要处理成员退群、离线、多设备等问题。当前项目更需要先修掉私聊在线 push 的最小可靠性缺口。

## 7. 测试设计

| 风险 | 测试 |
| --- | --- |
| 协议枚举漏注册 | `MessageTypeTest` 覆盖 ACK 类型名称和请求/响应分类 |
| handler 未暴露 | `ChatServiceTest` 用 `static_assert` 检查 `handleDeliveryAck` 签名 |
| receiver ACK 不写 delivered | `DeliveryAckMarksPrivateMessageDeliveredForReceiver` |
| 非 receiver 可伪造 ACK | `DeliveryAckRejectsNonReceiver` 和 MySQL integration 测试 |
| 重复 ACK 失败或覆盖状态 | `DeliveryAckIsIdempotentForReceiver` 和 MySQL duplicate ACK 验证 |
| MySQL 状态表写错 | `PrivateDeliveryAckMarksDeliveredAndRejectsNonReceiver` 查询 `message_deliveries.status` |
| CLI 构包漏字段 | `DeliveryAckCommandBuildsRequest` |
| 黑盒 push/ACK 漂移 | `LiteIME2E.test_private_chat` 在 Bob 收到 push 后 ACK 并重复 ACK |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests liteim_server -j2
ctest --test-dir build -R "MessageType|ClientCliCommandTest|ChatService|MySqlStorageIntegrationTest|LiteIME2E.test_private_chat" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

可以这样讲：

> 我把在线私聊 push 的接收确认从“服务端 sendPacket 成功”改成了应用层 delivery ACK。发送方的 `PrivateMessageResponse` 只表示服务端已保存消息；接收方客户端真正收到 `PrivateMessagePush` 后，会回 `DeliveryAckRequest(message_id)`。服务端再校验当前用户必须是这条私聊的 receiver，并把 `message_deliveries` 状态推进到 delivered。这样可以把 server-stored 和 delivered 两个状态分清楚。

更短版本：

> Step55 解决接收方收到确认：`PrivateMessageResponse` 是发送方存储 ACK，`DeliveryAckRequest` 是接收方 delivery ACK。

## 10. 面试常见追问

### TCP 不是已经有 ACK 了吗？

TCP ACK 只确认字节到达对端 TCP 栈，不代表客户端业务代码已经解析、展示或持久化某个 `message_id`。IM 需要应用层 ACK 才能表达业务投递状态。

### 为什么 ACK 只允许 receiver 发？

因为 delivered 的含义是“目标接收者客户端确认收到”。如果 sender 或第三方能 ACK，就会污染投递状态，所以存储层必须查消息并校验 `receiver_id == 当前登录 user_id`。

### 为什么重复 ACK 还返回成功？

ACK 是幂等操作。客户端可能因为响应丢失而重发 ACK，服务端应该保持 delivered 状态并返回成功，而不是把第二次 ACK 当成错误。

### 为什么不通知发送方 Bob 已经 delivered？

这是后续实时回执能力。本 Step 只建立服务端可信的 delivery 状态，不增加发送方订阅、推送回执或 UI 状态机。

### 为什么不做群聊全员 ACK？

群聊 ACK 会让每条消息产生最多 N 个成员状态和 N 个 ACK，还涉及退群、多设备、离线成员和回执聚合。当前项目先做私聊 ACK，群聊保持服务端存储 ACK 和离线 ACK。

### delivered 和 read 有什么区别？

`delivered` 表示接收方客户端确认收到；`read` 表示用户实际阅读。LiteIM 当前只实现 delivered，`read` 状态保留给后续 read receipt。
