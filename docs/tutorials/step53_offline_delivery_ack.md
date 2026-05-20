# Step 53：离线消息 ACK 与投递状态

## 0. 本 Step 结论

- 目标：把 Step 37 的“拉取即 delivered”改成“拉取只返回 pending，客户端 ACK 后才 delivered”。
- 前置依赖：依赖 `OfflineMessageService`、`IStorage` / `MySqlStorage`、`OfflineMessageDao`、CLI 和 Python E2E 的现有协议路径。
- 主要交付：新增 `OfflineMessagesAckRequest` / `OfflineMessagesAckResponse`，新增 `message_deliveries` 表，离线拉取不再提前标记 delivered，客户端 ACK 后再清 unread 并写 delivered。
- 可靠性边界：本 Step 只修离线消息 ACK，不做私聊在线 push ACK、不做群聊全员 ACK、不做已读回执、不做多设备。
- 工程边界：所有 MySQL / Redis 操作仍在 business `ThreadPool` 中执行，I/O 线程只负责收发 Packet。

## 1. 为什么需要这个 Step

旧的离线消息流程是：

```text
OfflineMessagesRequest
    -> 服务端查 pending offline rows
    -> 构造 OfflineMessagesResponse
    -> 服务端立即 mark delivered
    -> MessageRouter 之后才 sendPacket()
```

这里有一个丢消息窗口：服务端把数据库标记成 delivered 时，只能说明业务 handler 已经准备好 response，不能证明客户端已经收到、解析或展示了消息。如果 `sendPacket()` 入队后连接断开，客户端可能没看到消息，但下次再拉也拉不到。

本 Step 改成：

```text
OfflineMessagesRequest
    -> 只返回 pending messages，不改 delivered

客户端收到并处理后
    -> OfflineMessagesAckRequest(message_id...)
    -> 服务端 mark delivered + 写 message_deliveries + 清 unread
```

这样未 ACK 的离线消息会继续保持 pending，下一次拉取还能返回。

## 2. 本 Step 边界

本 Step 做：

- 新增 `OfflineMessagesAckRequest = 504`。
- 新增 `OfflineMessagesAckResponse = 505`。
- 新增 `TlvType::DeliveryStatus`，ACK 成功时返回 `2`，表示 delivered。
- 新增 `storage::DeliveryStatus` 枚举，当前使用 pending / pushed / delivered / read-reserved。
- 新增 MySQL `message_deliveries` 表，记录按用户维度的消息投递状态。
- `OfflineMessagesRequest` 只拉 pending，不再标记 delivered，不清 unread。
- `OfflineMessagesAckRequest` 批量 ACK `MessageId`，服务端才标记 delivered，并按会话清 unread。
- CLI 新增 `offline-ack <message_id> [message_id...]`。
- Python E2E 覆盖“拉取后未 ACK 仍可重复拉取，ACK 后不再返回”。

本 Step 不做：

- 不改登录响应，不把离线消息塞进 `LoginResponse`。
- 不做在线私聊 push 的接收方 ACK。
- 不做群聊每成员逐条 ACK。
- 不做 read receipt。
- 不做 `client_msg_id` 幂等，留给 Step 54。
- 不做设备维度投递，`message_deliveries` 第一版先按 `(message_id, user_id)` 记录。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/protocol/MessageType.hpp` / `src/protocol/MessageType.cpp` | 修改 | 增加离线 ACK 请求/响应类型和分类 |
| `include/liteim/protocol/Tlv.hpp` / `src/protocol/Tlv.cpp` | 修改 | 增加 `DeliveryStatus` 字段 |
| `include/liteim/storage/StorageTypes.hpp` | 修改 | 增加服务端投递状态枚举 |
| `include/liteim/storage/IStorage.hpp` | 修改 | 增加 `ackOfflineMessages()` 抽象接口 |
| `include/liteim/storage/OfflineMessageDao.hpp` / `src/storage/OfflineMessageDao.cpp` | 修改 | 实现离线 ACK 事务、delivered 标记和 delivery 状态写入 |
| `include/liteim/storage/MySqlStorage.hpp` / `src/storage/MySqlStorage.cpp` | 修改 | 接入 ACK 接口，并在保存离线消息时写 pending delivery |
| `include/liteim/service/OfflineMessageService.hpp` / `src/service/OfflineMessageService.cpp` | 修改 | 注册 ACK handler，拉取和 ACK 语义分离 |
| `client_cli/ClientCli.cpp` | 修改 | 增加 `offline-ack` 命令和 `delivery_status` 输出 |
| `scripts/init_mysql.sql` | 修改 | 新增 `message_deliveries` 表 |
| `scripts/migrations/054_delivery_ack.sql` | 新增 | 给已有本地数据库补投递状态表 |
| `tests/...` | 修改 | 覆盖协议常量、service、storage、CLI 和 E2E 离线 ACK 语义 |

## 4. 核心接口与契约

### 协议类型

```cpp
OfflineMessagesRequest = 500
OfflineMessagesResponse = 501
HistoryRequest = 502
HistoryResponse = 503
OfflineMessagesAckRequest = 504
OfflineMessagesAckResponse = 505
```

ACK request 的 body 使用重复 `MessageId`：

```text
MessageId = 5001
MessageId = 5002
```

ACK response 返回已确认的 message id 和投递状态：

```text
MessageId = 5001
DeliveryStatus = 2
MessageId = 5002
DeliveryStatus = 2
```

### 存储接口

```cpp
Status ackOfflineMessages(std::uint64_t user_id,
                          const std::vector<std::uint64_t>& message_ids,
                          std::vector<OfflineMessageRecord>& acked_messages);
```

契约：

- `user_id` 来自当前登录 session，不信任客户端传用户 id。
- `message_ids` 不能为空，且每个 id 必须大于 0。
- 只 ACK 属于当前用户的离线消息。
- 重复 ACK 是幂等语义：已经 delivered 的同一用户同一消息仍可返回成功。
- ACK 成功后 `offline_messages.delivered = 1`，`message_deliveries.status = delivered`。

## 5. 运行流程

离线拉取：

```text
Client
    -> OfflineMessagesRequest
    -> MessageRouter
    -> OfflineMessageService::handleOfflineMessages()
    -> OnlineService 查 session 对应 user_id
    -> IStorage::getOfflineMessages(user_id, limit)
    -> OfflineMessagesResponse
```

这一段只读 pending，不标记 delivered。

离线 ACK：

```text
Client 收到并处理离线消息
    -> OfflineMessagesAckRequest(message_id...)
    -> MessageRouter
    -> OfflineMessageService::handleOfflineMessagesAck()
    -> OnlineService 查 user_id
    -> IStorage::ackOfflineMessages(user_id, message_ids)
    -> ICache::clearUnread(user_id, conversation...)
    -> OfflineMessagesAckResponse
```

如果 ACK 前连接断开，pending row 仍是 `delivered = 0`，下次 `OfflineMessagesRequest` 还能返回同一条消息。

## 6. 关键实现点

### 1. 拉取和确认分离

`handleOfflineMessages()` 不再调用 `markOfflineDelivered()`，也不再清 Redis unread。它只负责读 pending rows 并构造 response。

### 2. ACK 事务同时更新两类表

`OfflineMessageDao::ackOfflineMessages()` 在事务中完成：

```text
fetch 属于当前 user 的 offline rows
    -> UPDATE offline_messages SET delivered = 1
    -> INSERT ... ON DUPLICATE KEY UPDATE message_deliveries.status = delivered
    -> COMMIT
```

这样 `offline_messages` 继续服务第一版 pending 拉取，`message_deliveries` 为后续私聊 delivery ACK / read receipt 预留状态表。

### 3. unread 清理变成 ACK 后副作用

Redis unread 不是消息事实来源。ACK 成功后清理 unread；清理失败只记录 warning，不撤回 MySQL delivered 状态。

### 4. CLI 和 E2E 都走真实协议

CLI 的 `offline-ack` 命令构造普通 `OfflineMessagesAckRequest`。Python E2E 先拉取、再重复拉取确认未 ACK 仍 pending、最后 ACK 并确认消息不再返回。

## 7. 测试设计

| 风险 | 测试 |
| --- | --- |
| 协议类型漏注册 | `MessageType` 测试覆盖 ACK request/response 的字符串和分类 |
| TLV 字段漏注册 | `TlvType` 测试覆盖 `DeliveryStatus` |
| 拉取仍提前 delivered | service 测试校验 `OfflineMessagesRequest` 不调用 ACK/mark |
| ACK 不清 unread | service 测试校验 ACK 后按会话清理 unread |
| 真实 MySQL 表缺失 | integration 测试和 migration 验证 `message_deliveries` |
| CLI 不支持 ACK | CLI 单测覆盖 `offline-ack` 构包和 `delivery_status` 打印 |
| 客户端语义漂移 | Python E2E 覆盖 ACK 前重复返回、ACK 后不返回 |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests liteim_server -j2
ctest --test-dir build -R "MessageType|TlvType|ClientCliCommandTest|OfflineMessageService|MessageDaoIntegrationTest|MySqlStorageIntegrationTest|LiteIME2E.test_offline" --output-on-failure
git diff --check
```

已有本地数据库如果缺少 `message_deliveries`，先执行：

```bash
mysql -h127.0.0.1 -P33060 -uliteim -p6 liteim < scripts/migrations/054_delivery_ack.sql
```

## 9. 面试表达

可以这样讲：

> 我把离线消息从“服务端拉取时就标记 delivered”改成了“客户端收到后显式 ACK”。服务端拉取只返回 pending rows；客户端处理完后发送 `OfflineMessagesAckRequest`，服务端才在 MySQL 事务里把 `offline_messages` 标记 delivered，并同步写 `message_deliveries` 投递状态。这样如果 response 还没真正到客户端连接就断了，未 ACK 的消息下次还能继续拉取。

更短版本：

> Step53 修掉了离线消息提前 delivered 的丢消息窗口：拉取不确认，ACK 才确认。

## 10. 面试常见追问

### TCP 自带 ACK，为什么还要业务 ACK？

TCP ACK 只能说明字节在传输层被对端内核接收，不说明 IM 客户端已经解析、持久化或展示了某条业务消息。离线 ACK 是应用层语义。

### delivered 是否等于已读？

不等于。当前 `delivered` 表示客户端确认收到这条消息；已读需要用户打开会话或阅读动作触发 read receipt，后续才做。

### 为什么 ACK 后才清 unread？

未读数是用户体验状态。如果消息还没被客户端确认收到，就清 unread，用户可能既看不到消息，也看不到未读提醒。ACK 后清理更符合“客户端已拿到这批消息”的语义。

### 为什么 `message_deliveries` 现在还不按 device_id？

第一版 LiteIM 仍是单设备/单 session 展示项目。表结构先按 `(message_id, user_id)` 记录，后续多设备可以扩展 device 维度。

### 群聊要不要每个成员都 ACK？

大群逐成员逐消息 ACK 会带来明显的写放大和流量放大。本计划先做离线 ACK 和私聊 delivery ACK，群聊暂时保持服务端存储确认和离线 pending 语义。
