# Step 54：client_msg_id 幂等发送

## 0. 本 Step 结论

- 目标：让客户端网络重试同一条私聊消息时，不在 MySQL 里插入重复消息，也不重复 push / offline / unread。
- 前置依赖：依赖 Step 36 私聊、Step 37 离线消息、Step 53 离线 ACK、`IStorage` / `MySqlStorage` 和 CLI / Python E2E。
- 主要交付：新增 `TlvType::ClientMessageId`，`messages.client_msg_id` 和 `(sender_id, client_msg_id)` 唯一约束，私聊重复请求返回已存在消息。
- 可靠性边界：本 Step 解决“发送方重试导致重复消息”，不是接收方是否收到 push 的 delivery ACK；私聊在线 ACK 留给 Step 55。
- 兼容边界：旧客户端不传 `ClientMessageId` 时保持原行为；新客户端传入后才获得幂等语义。

## 1. 为什么需要这个 Step

客户端发送私聊后可能遇到这种情况：

```text
Alice -> Server: PrivateMessageRequest
Server -> MySQL: INSERT messages 成功
Server -> Alice: PrivateMessageResponse
网络断开或客户端超时，Alice 没看到 response
Alice 重试同一条消息
```

如果服务端只按每次请求插入消息，Bob 会看到两条相同文本，离线未读也会重复 +1。`seq_id` 只能匹配一次 TCP 长连接上的请求/响应，不能跨重连证明两次发送是同一条业务消息。

本 Step 引入客户端生成的幂等键：

```text
PrivateMessageRequest
    ReceiverId = 1002
    ClientMessageId = "alice-phone-uuid-001"
    MessageText = "hello bob"
```

服务端把它写入 `messages.client_msg_id`，并用 `(sender_id, client_msg_id)` 唯一约束兜住并发和重试。第二次同 sender 同 client id 到来时，服务端不再插入重复消息，只查出原来的消息并返回同一个 `message_id`。第一版收口后，重复请求还会重新检查接收方投递侧：如果已经 delivered/read 就只返回发送方响应；如果还没有 delivered，则按当前在线状态重试 push 或补 offline fallback；如果 fallback row 已经存在，则幂等接受且不重复加 unread。

## 2. 本 Step 边界

本 Step 做：

- 新增 `TlvType::ClientMessageId = 51`。
- `PrivateMessageRequest` 可选携带 `ClientMessageId`。
- `MessageRecord` 增加 `client_msg_id` 字段。
- `messages` 表增加 `client_msg_id VARCHAR(64) NULL`。
- `messages` 表增加唯一索引 `uk_messages_sender_client_msg(sender_id, client_msg_id)`。
- `IStorage` 增加 `findMessageByClientMessageId()`。
- `ChatService` 遇到重复 `(sender_id, client_msg_id)` 时返回已存在消息；已经 delivered/read 的消息不重投，未 delivered 的消息会修复缺失的 receiver-side fallback，已有 row 不重复加 unread。
- `PrivateMessageResponse`、push、history、offline response 在消息存在 `client_msg_id` 时带回该字段。
- CLI 新增 `private-id <receiver_id> <client_msg_id> <text...>`。
- Python E2E 覆盖重复发送只产生一条离线消息。

本 Step 不做：

- 不强制所有客户端必须发送 `ClientMessageId`。
- 不实现客户端本地发送队列、自动重试或消息状态机。
- 不做接收方 delivery ACK。
- 不做群聊全员 ACK。
- 不做 read receipt。
- 不改变 `seq_id` 的请求/响应匹配语义。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/protocol/Tlv.hpp` / `src/protocol/Tlv.cpp` | 修改 | 新增 `ClientMessageId` 字段和可读名 |
| `include/liteim/service/Validation.hpp` | 修改 | 新增 64 字节 client message id 长度上限 |
| `include/liteim/storage/StorageTypes.hpp` | 修改 | `MessageRecord` 增加 `client_msg_id` |
| `include/liteim/storage/IStorage.hpp` | 修改 | 增加按 `(sender_id, client_msg_id)` 查询消息的接口；第一版收口后补 delivery status 查询用于重复请求投递修复 |
| `include/liteim/storage/MessageDao.hpp` / `src/storage/MessageDao.cpp` | 修改 | messages insert / history / idempotent find 支持 `client_msg_id` |
| `include/liteim/storage/MySqlStorage.hpp` / `src/storage/MySqlStorage.cpp` | 修改 | storage adapter 暴露幂等查询，事务 insert 识别唯一冲突 |
| `src/storage/OfflineMessageDao.cpp` | 修改 | 离线消息查询和 ACK 查询带回 `client_msg_id` |
| `src/service/MessagePacketBuilder.cpp` | 修改 | response / push / history / offline 消息字段可带回 `ClientMessageId` |
| `src/service/ChatService.cpp` | 修改 | 解析可选 id，重复保存时查回已存在消息，并按 delivery 状态决定是否修复接收方投递入口 |
| `client_cli/ClientCli.cpp` | 修改 | 新增 `private-id` 命令和 `client_msg_id` 输出 |
| `scripts/init_mysql.sql` | 修改 | `messages` 表新增列和唯一索引 |
| `scripts/migrations/055_client_msg_id.sql` | 新增 | 给已有本地数据库补 schema |
| `tests/...` | 修改 | 覆盖 TLV、service、storage、CLI 和 E2E 幂等语义 |

## 4. 核心接口与契约

### 协议字段

```cpp
ClientMessageId = 51
```

请求示例：

```text
MessageType = PrivateMessageRequest
ReceiverId = 1002
ClientMessageId = "cli-msg-1"
MessageText = "hello bob"
```

响应示例：

```text
MessageType = PrivateMessageResponse
MessageId = 5001
SenderId = 1001
ReceiverId = 1002
ClientMessageId = "cli-msg-1"
MessageText = "hello bob"
TimestampMs = 1800000000000
```

### 存储接口

```cpp
Status findMessageByClientMessageId(std::uint64_t sender_id,
                                    const std::string& client_msg_id,
                                    MessageRecord& message);
```

契约：

- `sender_id` 必须来自当前登录 session，不信任客户端传 sender id。
- `client_msg_id` 不能为空，最长 64 字节。
- 未找到时返回 `NotFound`。
- 找到时返回完整 `MessageRecord`，包括 `message_id`、会话、文本、时间戳和 `client_msg_id`。

### MySQL 约束

```sql
ALTER TABLE messages
ADD COLUMN client_msg_id VARCHAR(64) NULL,
ADD UNIQUE KEY uk_messages_sender_client_msg (sender_id, client_msg_id);
```

`client_msg_id` 允许 `NULL`，所以旧客户端不传 id 时不会互相冲突。只有同一个 sender 重复使用同一个非空 id 时，唯一约束才触发。

## 5. 运行流程

首次发送：

```text
Client
    -> PrivateMessageRequest(ClientMessageId="cli-msg-1")
    -> ChatService
    -> IStorage::saveMessageWithOfflineRecipients()
    -> INSERT messages(..., client_msg_id)
    -> push 或 offline/unread
    -> PrivateMessageResponse(message_id=5001, client_msg_id="cli-msg-1")
```

重复发送：

```text
Client timeout 后重试同一 client_msg_id
    -> ChatService
    -> IStorage::saveMessageWithOfflineRecipients()
    -> MySQL unique key 返回 duplicate
    -> IStorage::findMessageByClientMessageId(sender_id, client_msg_id)
    -> IStorage::findDeliveryStatus(receiver_id, message_id)
    -> 若已 delivered/read：不重投
    -> 若未 delivered：按在线状态重试 push 或补 offline fallback
    -> PrivateMessageResponse(message_id=5001, client_msg_id="cli-msg-1")
```

关键点是重复路径不再重复插入消息，并且会先看接收方 delivery 状态。已经 delivered/read 的消息不会重新 push 或重新生成离线消息；尚未 delivered 的消息才会修复缺失的接收方投递入口。

## 6. 关键实现点

### 1. 幂等边界放在数据库唯一约束

只在业务层先查再插有竞态：两个重试请求并发到来时可能都查不到，然后都插入。唯一约束是最终兜底，业务层只负责把 duplicate entry 翻译成 `AlreadyExists`，再查回已有消息。

### 2. `PrivateMessageResponse` 是发送方存储 ACK

Step54 里的重复响应说明“这条消息已经以该 id 存在于服务端”。它仍不表示接收方已经收到 push。这个区别很重要：

- `PrivateMessageResponse`：服务端已处理/已存储。
- `ClientMessageId`：发送方重试幂等。
- `DeliveryAck`：接收方已收到，Step55 再做。
- `ReadReceipt`：用户已读，当前不做。

### 3. 响应和历史带回 `ClientMessageId`

`appendMessageFields()` 统一追加消息 TLV 字段。只要 `MessageRecord::client_msg_id` 非空，响应、push、history、offline response 都会带回 `ClientMessageId`，方便客户端把服务端消息和本地发送中气泡绑定。

### 4. 旧客户端保持兼容

没有 `ClientMessageId` 的请求走旧路径。MySQL 用 `NULLIF(?, '')` 存储空 id，唯一索引允许多个 `NULL`，因此不会让旧客户端互相冲突。

## 7. 测试设计

| 风险 | 测试 |
| --- | --- |
| TLV 字段未注册 | `TlvTypeTest` 校验 `ClientMessageId` 名称 |
| 重复请求重复 side effect | `ChatServiceFixture.DuplicateClientMessageIdReturnsExistingMessageWithoutSecondUnread` |
| MySQL 唯一约束缺失 | `MySqlStorageIntegrationTest.ClientMessageIdCanFindExistingSenderMessage` |
| CLI 构包漏字段 | `ClientCliCommandTest.PrivateIdCommandAddsClientMessageId` |
| CLI 输出看不到幂等键 | `ClientCliCommandTest.DescribePacketIncludesMessageFields` |
| 黑盒行为漂移 | `LiteIME2E.test_private_chat` 覆盖重复发送只返回一条离线消息 |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests liteim_server -j2
docker compose -f docker/docker-compose.yml up -d --wait
mysql -h127.0.0.1 -P33060 -uliteim -p6 liteim < scripts/migrations/055_client_msg_id.sql
ctest --test-dir build -R "TlvType|ClientCliCommandTest|ChatService|MySqlStorageIntegrationTest|LiteIME2E.test_private_chat" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

可以这样讲：

> 我给私聊发送加了 `client_msg_id` 幂等键。客户端每条待发送消息生成一个稳定 id，服务端把它和 sender id 一起写进 MySQL 唯一约束。第一次请求正常插入；如果客户端因为超时或断线重试同一个 id，MySQL 会拒绝重复插入，业务层查回已有消息并返回同一个 `message_id`。第一版收口后，重复请求还会查询接收方 delivery 状态：已经 delivered/read 就不重投；尚未 delivered 才修复在线 push 或离线 fallback，已有 fallback 不会重复加 unread。

更短版本：

> Step54 解决发送方重试重复消息：`client_msg_id` 负责幂等，`message_id` 负责服务端事实记录。

## 10. 面试常见追问

### 为什么不用 `seq_id` 做幂等？

`seq_id` 是一条连接里的请求/响应匹配号，重连后通常会重新生成，也不一定持久化。`client_msg_id` 是业务消息 id，可以跨重连、跨超时重试保持不变。

### 为什么唯一约束是 `(sender_id, client_msg_id)`，不是只用 `client_msg_id`？

客户端生成 id 不能假设全局绝对唯一。加上 sender id 后，只要求同一个发送者不重复使用同一个 id；不同用户碰巧生成同一个字符串也不会冲突。

### 如果同一个 sender 用同一个 `client_msg_id` 发了不同文本怎么办？

服务端按幂等语义返回第一次成功保存的消息。客户端应该保证同一个 id 只对应同一条本地消息；如果复用 id，服务端不会把它当成新消息。

### 这个 ACK 和 Step53 的离线 ACK 有什么区别？

`client_msg_id` 是发送方重试幂等，解决“同一条消息被插入两次”。Step53 的离线 ACK 是接收方确认拉到离线消息，解决“服务端提前 delivered 导致丢消息”。两者解决的是不同可靠性问题。

### 现在是否已经保证 Bob 一定收到在线 push？

还没有。Step54 只保证服务端不重复存储；第一版收口后，重复请求会用 delivery 状态避免已 delivered 的消息被重投，并修复尚未 delivered 的接收方入口。Bob 是否实际收到在线 `PrivateMessagePush`，仍需要 Step55 的接收方 delivery ACK。
