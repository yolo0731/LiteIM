# Step 36：ChatService 私聊业务

## 0. 本 Step 结论

- 目标：Step 36 把私聊从协议枚举推进到真实业务闭环，处理 `PrivateMessageRequest`，落库后给发送方响应，并在接收方在线时推送 `PrivateMessagePush`。
- 前置依赖：依赖 Step 31 的 `IStorage` / `ICache` 聚合适配层、Step 32 的 `OnlineService`、Step 33 的 `MessageRouter`、Step 34 的登录绑定和 Step 35 的 service 接入模式。
- 主要交付：新增 `ChatService`、私聊 service 测试、server runtime handler 注册和本文档。
- 线程边界：私聊 handler 注册为 `BusinessThread`，MySQL / Redis 阻塞调用不进入 Reactor I/O 线程。
- 范围控制：本 Step 只做单进程私聊发送，不做群聊、离线消息拉取、历史查询、跨节点转发或可靠 ACK 重试。

## 1. 为什么需要这个 Step

前面已经有注册登录、在线 session 绑定和好友关系，但还没有真正的聊天消息链路。IM 系统的核心闭环是：

```text
发送者发消息
    -> 服务端确认发送者是谁
    -> 消息先持久化
    -> 接收者在线则立即推送
    -> 接收者离线则记录离线消息和未读数
    -> 发送者收到发送结果
```

如果没有 `ChatService`，协议里的 `PrivateMessageRequest` / `PrivateMessageResponse` / `PrivateMessagePush` 只是枚举值，客户端无法完成真正的私聊。

本 Step 把已有模块组合起来：

```text
MessageRouter
    -> ChatService
    -> OnlineService / SessionManager
    -> IStorage::saveMessageWithOfflineRecipients()
    -> ICache::incrUnread()
    -> Session::sendPacket()
```

核心原则是“先落库，再投递”。即使接收方在线，也先把消息写入 MySQL，再向接收方 session 推送。这样 server 在 push 前后异常退出时，消息仍然有 MySQL 记录，后续 Step 37 可以通过离线/历史机制补偿。

## 2. 本 Step 边界

### 本 Step 做

- 处理 `PrivateMessageRequest`。
- 从 `OnlineService::getUserBySession(session_id)` 获取发送者身份，不信任客户端传入的 `SenderId`。
- 读取请求 TLV：`ReceiverId` 和 `MessageText`。
- 校验发送者已登录、接收者存在、接收者不是自己、消息文本非空。
- 构造私聊 `MessageRecord` 并调用 `IStorage::saveMessageWithOfflineRecipients()`。
- 接收者在线时，通过当前进程内存绑定拿到 `Session` 并发送 `PrivateMessagePush`。
- 接收者离线时，把接收者写入 `offline_messages`，并调用 `ICache::incrUnread()` 让未读数 +1。
- 给发送者返回 `PrivateMessageResponse`，携带完整消息字段。

### 本 Step 不做

- 不实现群聊消息、离线消息拉取、历史查询、消息撤回、已读回执、可靠投递 ACK 或跨节点在线路由。
- 不修改 MySQL schema，不新增协议枚举或 TLV 字段。
- 不做好友关系校验；当前 Step 只要求接收方用户存在。
- 不把 MySQL / Redis 阻塞调用放到 Reactor I/O 线程。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/service/ChatService.hpp` | 新增 | 声明私聊 service public API、router handler 和内部 helper 边界 |
| `src/service/ChatService.cpp` | 新增 | 实现私聊请求处理、消息落库、在线 push、离线未读递增 |
| `tests/service/chat_service_test.cpp` | 新增 | 覆盖未登录、接收方不存在、离线落库+未读、在线 push、router response |
| `src/service/CMakeLists.txt` | 修改 | 把 `ChatService.cpp` 编入 `liteim_service` |
| `server/main.cpp` | 修改 | 创建 `ChatService` 并注册 `PrivateMessageRequest` handler |
| `tests/CMakeLists.txt` | 修改 | 把 `chat_service_test.cpp` 编入 `liteim_tests` |
| `README.md` | 修改 | 更新当前 service runtime 和验证说明 |
| `docs/tutorials/step36_chat_service.md` | 新增 | 记录本 Step 教学说明 |
| `docs/process/task_plan.md` / `docs/process/findings.md` / `docs/process/progress.md` | 修改 | 记录本次 Step36 过程、发现和验证结果 |
| `/home/yolo/jianli/PROJECT_MEMORY.md` | 修改 | 补充 Step36 私聊请求字段和会话 id 规则 |

## 4. 核心接口与契约

### `ChatService`

```cpp
class ChatService {
public:
    ChatService(IStorage& storage, ICache& cache, OnlineService& online_service);

    Status registerHandlers(MessageRouter& router);
    Status handlePrivateMessage(const MessageRouter::RouterRequest& request, Packet& response);
};
```

`ChatService` 只依赖抽象接口和上层在线服务：

- `IStorage& storage_`：查接收方用户、保存消息和离线接收者。
- `ICache& cache_`：离线时递增 Redis 未读数。
- `OnlineService& online_service_`：查当前 session 对应用户，查接收方是否在当前进程有在线 session。

### `registerHandlers()`

```cpp
Status registerHandlers(MessageRouter& router);
```

契约：

- 注册 `MessageType::PrivateMessageRequest`。
- dispatch mode 固定为 `MessageRouter::DispatchMode::BusinessThread`。
- handler 调用 `handlePrivateMessage()`。
- 不在注册阶段访问 MySQL、Redis 或 `Session`。

### `handlePrivateMessage()`

```cpp
Status handlePrivateMessage(const MessageRouter::RouterRequest& request, Packet& response);
```

输入契约：

- `request.session` 必须是已登录 session。
- `request.fields` 必须包含 `TlvType::ReceiverId`。
- `request.fields` 必须包含非空 `TlvType::MessageText`。
- `SenderId` 不从客户端读取，而是由 `OnlineService::getUserBySession()` 得到。

输出契约：

- 成功时 `response.header.msg_type = PrivateMessageResponse`。
- response body 写入：
  - `MessageId`
  - `ConversationType`
  - `ConversationId`
  - `SenderId`
  - `ReceiverId`
  - `MessageText`
  - `TimestampMs`
- 接收方在线时，向接收方 session 发送同字段的 `PrivateMessagePush`。
- 接收方离线时，不发送 push，改为写 offline recipient 并递增未读数。

### 失败语义

| 场景 | 返回 |
| --- | --- |
| session 未登录或已关闭 | `InvalidArgument` |
| `ReceiverId` 缺失或解析失败 | TLV getter 原始错误 |
| `ReceiverId=0` 或发给自己 | `InvalidArgument` |
| `MessageText` 缺失或为空 | `NotFound` / `InvalidArgument` |
| 接收方用户不存在 | `NotFound` |
| MySQL 保存失败 | 原始 `IStorage` 错误 |
| Redis 未读递增失败 | 记录 warning；消息和 offline row 已保存时仍返回成功 response |
| 在线 push 编码或发送失败 | 原始 `Session::sendPacket()` 错误 |

### 生命周期和线程边界

- `ChatService` 不拥有 `storage_`、`cache_`、`online_service_`，它们由 server 运行时装配并保证生命周期覆盖 router handler 使用期。
- business 线程可以调用 `Session::sendPacket()`，因为 `sendPacket()` 会把实际发送任务投递回接收方 `Session` 的 owner `EventLoop`。
- `ChatService` 不直接读写 fd、`Buffer`、`Channel`，也不访问 `Session::pendingOutputBytes()`。
- 接收方在线判断第一版只看当前进程内存绑定；Redis 在线状态用于在线展示和后续扩展，不在本 Step 做跨进程转发。

## 5. 运行流程

### 主流程

```text
TcpServer 收到 Packet
    -> MessageRouter 解析 TLV
    -> business ThreadPool 执行 ChatService::handlePrivateMessage()
    -> OnlineService 查发送者 user_id
    -> IStorage::findUserById(receiver_id)
    -> OnlineService::getSessionByUser(receiver_id)
    -> IStorage::saveMessageWithOfflineRecipients()
    -> 在线：receiver_session->sendPacket(PrivateMessagePush)
    -> 离线：ICache::incrUnread()
    -> 填充 PrivateMessageResponse
    -> MessageRouter 给发送者回包
```

### 在线接收者

```text
Alice session_id=42 已登录为 user_id=1001
Bob   session_id=43 已登录为 user_id=1002

Alice -> Server:
  PrivateMessageRequest seq_id=7
  ReceiverId=1002
  MessageText="hello bob"

ChatService:
  sender_id = 1001
  receiver_id = 1002
  conversation_id = 10011002
  save messages -> message_id=5001
  Bob 当前进程在线 -> push

Server -> Bob:
  PrivateMessagePush
  MessageId=5001
  SenderId=1001
  ReceiverId=1002
  MessageText="hello bob"

Server -> Alice:
  PrivateMessageResponse seq_id=7
  MessageId=5001
```

### 离线接收者

```text
Alice 在线，Bob 不在当前进程在线表里。

ChatService:
  saveMessageWithOfflineRecipients(message, {1002}, saved_message)
  Redis unread:user:1002:conversation:1:10011002 +1
  不发送 PrivateMessagePush
  返回 PrivateMessageResponse 给 Alice
```

### 该项目代码在实际应用中的具体数据例子

真实 LiteIM seed 数据里，`alice` 是 `user_id=1001`，`bob` 是 `user_id=1002`。Alice 发私聊：

```text
PrivateMessageRequest
  seq_id = 7
  ReceiverId = 1002
  MessageText = "hello bob"
```

`ChatService` 会生成：

```text
sender_id = 1001
receiver_id = 1002
conversation_type = 1
conversation_id = 10011002
message_id = 5001
offline key = unread:user:1002:conversation:1:10011002
online key = online:user:1002
```

如果 Bob 在线，Bob 收到 `PrivateMessagePush`；如果 Bob 离线，MySQL `offline_messages` 记录 `user_id=1002, message_id=5001`，Redis 未读数加一。

## 6. 关键实现点

### 发送者身份来自 session

客户端请求里不需要也不应该携带可信 `SenderId`。发送者身份来自：

```text
request.session->id()
    -> OnlineService::getUserBySession()
    -> sender_id
```

这样客户端无法伪造“我是 user_id=1001”。

### 私聊会话 id

当前实现由服务端根据发送方和接收方生成私聊 `conversation_id`。对 seed 中的小用户 id，`1001` 和 `1002` 会得到 `10011002`，便于和现有教程、seed 数据、Redis 未读 key 对齐。更大用户 id 使用 32-bit pair 方式生成稳定 id；超过当前 v1 约束会返回错误。

### 先落库再推送

无论接收者在线还是离线，都先调用：

```cpp
storage.saveMessageWithOfflineRecipients(message, offline_user_ids, saved_message);
```

push 只使用 `saved_message`，这样 response/push 中的 `MessageId` 和 `TimestampMs` 都来自 MySQL 保存后的结果。

### 在线只看当前进程绑定

`OnlineService::getSessionByUser(receiver_id, receiver_session)` 查询的是 `SessionManager` 内存表。第一版 LiteIM 是单进程服务，所以能直接拿到接收方 `Session` 就 push；拿不到就走离线路径。跨节点路由留给后续扩展。

### Redis 未读不是消息真相

消息正文和离线投递记录在 MySQL。Redis 未读数只是 UI 状态和查询加速。离线分支里 MySQL commit 成功后再递增 Redis；如果 Redis 失败，当前 hardening 只记录 warning，不把发送方响应改成失败。原因是消息和 offline row 已经保存成功，继续返回失败会诱导客户端重试并产生重复消息。未读数可以后续根据 MySQL offline/history 数据修复或重算。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| 未登录连接伪造私聊 | `PrivateMessageRequiresLoggedInSession` 校验未绑定 session 返回错误且不落库、不递增未读 |
| 接收方不存在导致脏消息 | `PrivateMessageRequiresExistingReceiver` 校验 `findUserById()` 失败时不保存消息 |
| 离线消息没有进入 offline/unread | `OfflineReceiverSavesMessageAndIncrementsUnread` 校验 offline recipient 为 `{1002}`，Redis unread +1 |
| Redis unread 失败诱导发送方重试 | `OfflineUnreadFailureStillReturnsSenderSuccess` 校验消息和 offline row 已保存时仍返回 `PrivateMessageResponse` |
| 在线接收者没有收到 push | `OnlineReceiverGetsPushWithoutOfflineUnread` 用真实 running `Session` 读取 `PrivateMessagePush` |
| sender response 没经过 router | `RegisteredHandlerSendsSenderResponseThroughRouter` 注册 handler 后通过 `MessageRouter::route()` 读取发送方 response |
| response/push 字段不完整 | 所有成功路径都校验 `MessageId`、会话字段、发送方、接收方、文本和时间戳 |

本 Step 的 service 单元测试使用 fake `IStorage` / `ICache`，避免私聊业务测试依赖本机 MySQL / Redis 是否启动。真实 MySQL 事务和 Redis 未读行为已经由 Step 31 / Step 30 的集成测试覆盖。

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "ChatService" --output-on-failure
```

完整验证建议：

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
git diff --check
timeout 1s ./build/server/liteim_server || test $? -eq 124
```

如果要验证真实 MySQL / Redis 依赖先启动本地 Docker：

```bash
docker compose -f docker/docker-compose.yml up -d --wait
```

## 9. 面试表达

### 一句话

> Step 36 实现了私聊发送闭环：业务线程校验登录态和接收方，先把消息持久化到 MySQL，再根据接收方是否在当前进程在线来 push 或写离线/未读，最后给发送方返回发送结果。

### 展开说

`ChatService` 不信任客户端传 `SenderId`，而是从 `SessionManager` / `OnlineService` 查当前 session 绑定的 user id。私聊请求只需要 `ReceiverId` 和 `MessageText`。服务端生成私聊会话 id，构造 `MessageRecord`，调用 `IStorage::saveMessageWithOfflineRecipients()` 保存消息。接收方在线时，通过 `Session::sendPacket()` 把 `PrivateMessagePush` 投递回接收方 owner loop；离线时把接收方写进 offline recipient，并调用 `ICache::incrUnread()`。如果 unread 递增失败，消息事实仍以 MySQL 为准，服务端记录 warning 并给发送方成功响应，避免客户端重试制造重复消息。这些 handler 都通过 `MessageRouter` 跑在 business thread pool，避免 Reactor I/O 线程被 MySQL / Redis 阻塞。

### 容易被追问

- 为什么 sender id 不能从请求 body 读？
- 为什么要先落库再 push？
- 接收方在线判断为什么先用 `SessionManager` 而不是 Redis？
- Redis 未读递增失败时 MySQL 已经写入了怎么办？
- business 线程调用 `Session::sendPacket()` 是否安全？

## 10. 面试常见追问

### Q1：为什么不直接让客户端带 `SenderId`？

因为客户端传的字段不能作为身份来源。服务端必须从登录态里拿发送者，也就是 `session_id -> user_id` 的绑定关系。否则恶意客户端可以把 body 里的 `SenderId` 改成别人。

### Q2：为什么先落库再 push？

IM 消息的 source of truth 是 MySQL。先落库可以保证消息有持久化记录，push 失败或进程异常时，后续离线/历史流程还能补偿。先 push 再落库会出现客户端看到了消息但服务端没有记录的风险。

### Q3：为什么在线投递只看当前进程内存表？

第一版 LiteIM 是单进程服务。当前进程能拿到 `Session` 才能直接 `sendPacket()`。Redis 在线状态可以告诉我们用户可能在线，但不能提供本进程可写的 `Session` 指针。跨节点转发需要额外路由层，本 Step 不做。

### Q4：离线分支为什么既写 MySQL 又写 Redis？

MySQL `offline_messages` 保存可靠的待拉取记录；Redis 未读数用于会话列表显示和快速查询。Redis 不是消息真相，丢了可以从 MySQL 重新计算或修复，但正常路径仍要递增，保证用户体验。若 MySQL 已保存成功而 Redis unread 递增失败，服务端只记录 warning 并返回发送成功，避免客户端重试产生重复消息。

### Q5：business 线程调用 `receiver_session->sendPacket()` 会不会跨线程直接写 fd？

不会。`Session::sendPacket()` 先编码 Packet，然后检查当前线程。如果不是该 `Session` 的 owner loop，就通过 `queueInLoop()` 把发送任务投递回 owner loop，真正写 fd 的动作仍在 I/O 线程执行。

### Q6：现在会检查 Alice 和 Bob 是好友吗？

不会。本 Step 的边界只要求接收方用户存在。好友关系校验可以在后续隐私策略里加，但不能混进当前私聊落库和投递主线。
