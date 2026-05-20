# Step 37：OfflineMessageService 离线消息拉取

## 0. 本 Step 结论

- 目标：Step 37 把 Step 36 写入 `offline_messages` 的待投递消息真正拉回客户端。
- 前置依赖：依赖 Step 26 `OfflineMessageDao`、Step 31 `IStorage` / `ICache` 适配层、Step 32 `OnlineService`、Step 33 `MessageRouter` 和 Step 36 私聊离线写入。
- 主要交付：新增 `OfflineMessageService`、离线消息 service 测试、server runtime handler 注册和本文档。
- 线程边界：`OfflineMessagesRequest` 通过 business `ThreadPool` 执行，MySQL / Redis 阻塞调用不进入 Reactor I/O 线程。
- 范围控制：采用方案一，登录成功后客户端主动发送 `OfflineMessagesRequest`；服务端登录 handler 仍只返回 `LoginResponse`。
- 当前实现提示：Step 53 已把“拉取后标记 delivered”升级为“客户端发送 `OfflineMessagesAckRequest` 后才 delivered”；本文件保留 Step 37 原始教学背景，并在关键位置标出当前语义。

## 1. 为什么需要这个 Step

Step 36 已经能在接收者离线时做两件事：

```text
PrivateMessageRequest
    -> messages 表保存真实消息
    -> offline_messages 表保存待投递记录
    -> Redis unread 计数 +1
```

但如果没有 Step 37，用户重新上线后只能看到未读数，拿不到真正的离线消息内容。`OfflineMessageService` 负责把“待投递记录”变成客户端能消费的 `OfflineMessagesResponse`。

本 Step 解决的问题是：

- 确认当前 session 已登录。
- 查询当前用户未 delivered 的离线消息。
- 按最多 100 条的上限构造响应。
- Step 37 原始版本在拉取后清理 unread 并标记 delivered；当前 Step 53 之后，拉取只返回 pending，ACK 后才清 unread 并标记 delivered。

这里没有把离线消息直接塞进 `LoginResponse`，也没有让登录 handler 额外发送一个 follow-up response。原因是当前 `MessageRouter` 的模型是一请求一响应，主动拉取能保持 service 边界简单，后续 Qt / Python 客户端只需要在登录成功后再发一个 `OfflineMessagesRequest`。

## 2. 本 Step 边界

### 本 Step 做

- 新增 `OfflineMessageService`。
- 注册 `MessageType::OfflineMessagesRequest`。
- 从 `OnlineService` 查询当前 session 绑定的 user id。
- 调用 `IStorage::getOfflineMessages()` 查询 pending offline rows。
- 支持可选 `TlvType::Limit`，但最多返回 100 条。
- 返回 `OfflineMessagesResponse`，消息字段使用重复 TLV 表达。
- 返回 pending 离线消息。Step 53 之后，清理 unread 和标记 delivered 已移动到 `OfflineMessagesAckRequest`。

### 本 Step 不做

- 不修改 `AuthService` 登录响应。
- 不修改 `MessageRouter` 支持多 response。
- Step 37 原始版本不实现可靠 ACK 和重试；当前 Step 53 已补离线消息 ACK，私聊在线 push ACK 仍不在本 Step。
- 不删除 `offline_messages` 历史记录，只标记 delivered。
- 不实现群聊、历史分页或跨节点路由。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/service/OfflineMessageService.hpp` | 新增 | 声明离线消息 service、选项、handler 和内部 helper 边界 |
| `src/service/OfflineMessageService.cpp` | 新增 | 实现离线消息拉取和响应构造；Step 53 后同一 service 也负责离线 ACK |
| `src/service/CMakeLists.txt` | 修改 | 把 `OfflineMessageService.cpp` 编入 `liteim_service` |
| `server/main.cpp` | 修改 | 创建 `OfflineMessageService` 并注册 `OfflineMessagesRequest` handler |
| `tests/service/offline_message_service_test.cpp` | 新增 | 覆盖登录态、拉取响应、limit 截断、ACK 后 delivered、未读清理和真实依赖集成 |
| `tests/CMakeLists.txt` | 修改 | 接入 Step 37 service 测试 |
| `README.md` | 更新 | 记录 Step 37 runtime 和验证命令 |
| `docs/tutorials/step37_offline_message_service.md` | 新增 | 讲解离线消息拉取流程 |
| `docs/process/task_plan.md / docs/process/findings.md / docs/process/progress.md` | 更新 | 记录 Step 37 过程、方案一和验证结果 |
| `/home/yolo/jianli/PROJECT_MEMORY.md` | 更新 | 把 Step 37 固定为客户端主动拉取方案 |

## 4. 核心接口与契约

### `OfflineMessageServiceOptions`

```cpp
struct OfflineMessageServiceOptions {
    std::uint32_t max_messages_per_pull{100};
};
```

契约：

- 默认每次最多返回 100 条。
- 配置值必须在 `[1, 100]`。
- 客户端请求里的 `Limit` 不能超过这个 service 上限。

### `OfflineMessageService`

```cpp
class OfflineMessageService {
public:
    OfflineMessageService(IStorage& storage, ICache& cache, OnlineService& online_service,
                          OfflineMessageServiceOptions options = OfflineMessageServiceOptions{});

    Status registerHandlers(MessageRouter& router);
    Status handleOfflineMessages(const MessageRouter::RouterRequest& request, Packet& response);
};
```

它只依赖三个抽象/上层服务：

- `IStorage`：查询 pending offline rows；Step 53 之后通过 `ackOfflineMessages()` 标记 delivered。
- `ICache`：清理本批消息涉及会话的 unread key。
- `OnlineService`：根据当前 `session_id` 查询登录 user id。

`OfflineMessagesRequest` 的 body 可以为空，也可以带：

- `TlvType::Limit`：本次最多返回多少条，必须大于 0，超过 service 上限会被截断。

`OfflineMessagesResponse` 对每条消息重复写入：

- `MessageId`
- `ConversationType`
- `ConversationId`
- `SenderId`
- `ReceiverId`
- `MessageText`
- `TimestampMs`

这些字段的重复顺序一致。客户端按相同下标把字段重新组装成一条消息。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

Bob 离线时，Alice 给 Bob 发消息。Step 36 会把消息写入 MySQL `messages`，同时给 Bob 写一条 `offline_messages` pending 记录，并把 Bob 的 Redis 未读数加一。Bob 后续登录成功后，客户端再发送 `OfflineMessagesRequest`，Step 37 把这些 pending 消息返回给 Bob。

### 2. 上下层调用连接

```text
Bob 客户端
    -> OfflineMessagesRequest
    -> MessageRouter
    -> business ThreadPool 执行 OfflineMessageService::handleOfflineMessages()
    -> OnlineService::getUserBySession()
    -> IStorage::getOfflineMessages()
    -> OfflineMessagesResponse

Client 收到并处理后
    -> OfflineMessagesAckRequest
    -> IStorage::ackOfflineMessages()
    -> ICache::clearUnread()
    -> MessageRouter 发回 OfflineMessagesResponse
```

网络层只负责收包和发包。阻塞的 MySQL / Redis 调用都在 business 线程执行。

### 3. 整体运行链路

1. 客户端先完成登录，拿到 `LoginResponse`。
2. 客户端发送 `OfflineMessagesRequest`，可以不带 body，也可以带 `Limit`。
3. `MessageRouter` 解析 TLV，按消息类型找到离线消息 handler。
4. handler 从 `OnlineService` 查当前 session 绑定的 user id。
5. handler 调用 `IStorage::getOfflineMessages(user_id)` 查询未 delivered 的记录。
6. handler 根据 `Limit` 和 100 条上限截断本批结果。
7. handler 把消息字段写入 `OfflineMessagesResponse`。
8. `MessageRouter` 把 response 发回当前 session。
9. Step 53 之后，客户端收到消息后再发送 `OfflineMessagesAckRequest`。
10. ACK handler 调用 `IStorage::ackOfflineMessages()` 并清理本批涉及会话的 Redis unread key。

### 4. 自身内部运行流程

核心函数是 `handleOfflineMessages()`：

```text
currentUserId()
    -> requestLimit()
    -> storage_.getOfflineMessages()
    -> resize 到本次 limit
    -> appendMessages()
    -> appendMessages()
```

Step 53 之后，`handleOfflineMessagesAck()` 才会调用 `clearUnreadForMessages()`。它会先按 `(ConversationType, ConversationId)` 去重。同一个会话里有多条 ACK 消息时，只清一次 unread key。

### 5. 该项目代码在实际应用中的具体数据例子

假设：

```text
Alice user_id = 1001
Bob   user_id = 1002
private conversation_id = 10011002
message_id = 5001
message_text = "hello bob"
created_at_ms = 1700000000000
```

Bob 离线时，MySQL 里会有：

```text
messages:
  message_id = 5001
  conversation_type = 1
  conversation_id = 10011002
  sender_id = 1001
  receiver_id = 1002
  message_text = "hello bob"

offline_messages:
  user_id = 1002
  message_id = 5001
  delivered = 0

Redis:
  unread:user:1002:conversation:1:10011002 = 1
```

Bob 登录后发送 `OfflineMessagesRequest`，响应 body 会包含：

```text
MessageId        = 5001
ConversationType = 1
ConversationId   = 10011002
SenderId         = 1001
ReceiverId       = 1002
MessageText      = "hello bob"
TimestampMs      = 1700000000000
```

如果 Bob 此时还没 ACK，MySQL `offline_messages.delivered` 仍是 `0`，下次拉取还能返回同一条消息。Bob 发送 `OfflineMessagesAckRequest(MessageId=5001)` 后，MySQL `offline_messages.delivered` 变成 `1`，Redis 未读 key 被清理。

## 6. 关键实现点

### 登录态只信任 Session

`OfflineMessagesRequest` 不需要也不信任客户端传 `UserId`。服务端只用当前 `Session::id()` 去 `OnlineService` 查询绑定 user id。

```text
session_id = 7001
    -> OnlineService
    -> user_id = 1002
```

如果 session 未登录，handler 返回 `InvalidArgument`。

### Limit 处理

请求不带 `Limit` 时使用默认 100 条。请求带 `Limit=10` 时最多返回 10 条。请求带 `Limit=999` 时仍最多返回 100 条。请求带 `Limit=0` 直接返回 `InvalidArgument`。

### Response 字段重复

当前 TLV codec 支持同一个 `TlvType` 重复出现。Step 37 利用这个能力表达消息列表：

```text
MessageId: 5001
MessageId: 5002
MessageText: "one"
MessageText: "two"
```

客户端按相同顺序取 repeated fields，即可把第 0 个 `MessageId`、第 0 个 `MessageText` 等字段组合成第一条消息。

### delivered 标记时机

Step 37 原始版本是在拉取 handler 内部标记 delivered。Step 53 之后，delivered 标记移动到客户端 ACK handler：

```text
pull 只读 pending
ACK 才 mark delivered
```

所以当前 delivered 表示“客户端已经显式确认收到这条离线消息”，不再只是“服务端准备把 response 交给当前 session 发送”。

## 7. 测试设计

| 风险 | 测试 |
| --- | --- |
| 未登录用户能拉取离线消息 | `OfflineMessagesRequireLoggedInSession` 校验未绑定 session 直接失败 |
| pending rows 没有进入响应 | `OfflineMessagesReturnPendingRowsWithoutMarkingDelivered` 校验 repeated TLV 字段，并确认拉取不 ACK |
| delivered 没有更新 | `OfflineAckMarksDeliveredAndClearsUnread` 校验 ACK 后 delivered |
| 未读数没有清理 | 同一 ACK 测试校验每个涉及会话调用 `clearUnread()` |
| 拉取数量超过上限 | `OfflineMessagesLimitIsCappedByServiceOption` 校验只返回和标记前 N 条 |
| 非法 limit | `OfflineMessagesRejectZeroLimit` 校验 `Limit=0` 不访问 storage/cache |
| 真实 MySQL / Redis 适配漂移 | `PullKeepsPendingUntilClientAck` 使用 `MySqlStorage` / `RedisCache` 做集成验证；依赖不可用时跳过 |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "OfflineMessageService" --output-on-failure
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
```

如果要跑真实 MySQL / Redis 集成路径：

```bash
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build -R "OfflineMessageService" --output-on-failure
```

## 9. 面试表达

> Step 37 实现了离线消息主动拉取：客户端登录成功后发送 `OfflineMessagesRequest`，服务端在 business 线程根据当前 session 查 user id，从 MySQL 查询 pending offline rows，按最多 100 条构造 `OfflineMessagesResponse`。后续 Step 53 把 delivered 标记移动到 `OfflineMessagesAckRequest`，因此当前拉取不会提前确认，客户端 ACK 后才清 unread 并标记 delivered。

可以重点强调三点：

- MySQL 是消息和 offline delivery 的事实来源。
- Redis unread 只是快速状态，当前实现是在 ACK 成功后按会话清理。
- I/O 线程不做 MySQL / Redis 阻塞调用，所有业务 handler 走 business thread pool。

## 10. 面试常见追问

### 为什么不直接在 LoginResponse 里带离线消息？

当前 `MessageRouter` 是一请求一响应模型。把离线消息塞进登录响应会让 `AuthService` 依赖聊天投递逻辑，或者迫使 `MessageRouter` 支持 follow-up response。第一版采用客户端登录后主动拉取，边界更清楚。

### delivered 是不是表示客户端一定收到了？

当前 Step 53 之后，离线消息的 delivered 表示客户端已经发送 ACK；它仍不等于用户已读。已读回执属于后续设计。

### 为什么清理 unread 要按 conversation 去重？

同一个会话可能有多条离线消息，但 Redis unread key 是按 `(user_id, conversation)` 维度存的。拉取一批消息时，同一个会话只需要清一次。

### 如果 Redis 清理失败怎么办？

当前实现会先完成 MySQL ACK，再把 Redis unread 清理作为 best-effort 副作用。Redis 是缓存状态，清理失败会记录 warning，但不会撤销已经确认的 MySQL delivered。

### 为什么最多 100 条？

离线消息响应可能很大，第一版先用固定上限保护单次响应大小和业务线程处理时间。更完整的分页、游标、私聊在线 delivery ACK 和 read receipt 可以在后续可靠投递设计中继续扩展。
