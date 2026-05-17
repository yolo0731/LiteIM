# Step 39：HistoryService 历史消息分页

## 0. 本 Step 结论

- 目标：Step 39 把已经存在的 `MessageDao::getHistoryByConversation()` 暴露成客户端可用的 `HistoryRequest` / `HistoryResponse`。
- 前置依赖：依赖 Step 26 历史查询 DAO、Step 31 `IStorage::getHistory()`、Step 32 `OnlineService`、Step 33 `MessageRouter`、Step 36 私聊 conversation id 规则和 Step 38 群成员查询能力。
- 主要交付：新增 `HistoryService`、service 单元测试、server runtime handler 注册、README 和本文档。
- 线程边界：`HistoryRequest` 通过 business `ThreadPool` 执行，MySQL 查询不进入 Reactor I/O 线程。
- 范围控制：第一版只做最近消息和 `before_message_id` 游标分页，不做全文搜索、按关键字查询、消息撤回过滤、已读回执、JSON body 或客户端 UI。

## 1. 为什么需要这个 Step

Step 26 已经有 `MessageDao::getHistoryByConversation()`，Step 31 也已经把它挂到 `IStorage::getHistory()`。但在运行时，客户端打开一个会话时还不能发包查询历史消息，因为 `MessageRouter` 还没有 `HistoryRequest` handler。

Step 39 解决的问题是：

- 客户端打开私聊或群聊会话时，先加载最近 N 条历史消息。
- 客户端上拉时，带上上一页最后一条 `message_id`，查询更早的消息。
- service 层统一校验当前 session 已登录，以及当前用户是否有权读取该会话。
- SQL 细节继续封装在 storage 层，业务 service 不拼 SQL。

## 2. 本 Step 边界

### 本 Step 做

- 新增 `HistoryService`。
- 注册 `MessageType::HistoryRequest`。
- 请求读取 `ConversationType`、`ConversationId`、可选 `MessageId` 和可选 `Limit`。
- `MessageId` 在请求里表示 `before_message_id` 游标。
- 默认 limit 为 20，最大 limit 为 50，`limit=0` 拒绝。
- 私聊历史校验当前用户是否参与该私聊 conversation。
- 群聊历史校验 group 存在，并校验当前用户是群成员。
- 响应用重复 TLV 字段返回消息列表。

### 本 Step 不做

- 不新增 SQL schema。
- 不改 `MessageDao` 历史分页 SQL。
- 不新增 `BeforeMessageId` TLV 类型。
- 不做 JSON body 模式。
- 不做历史消息全文搜索或关键字搜索。
- 不做撤回消息、删除消息、已读回执或 ACK。
- 不做 CLI / Qt 历史加载 UI。
- 不做 HeartbeatService。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/service/HistoryService.hpp` | 新增 | 声明历史消息 service、分页选项、handler 和内部 helper |
| `src/service/HistoryService.cpp` | 新增 | 实现请求解析、登录校验、会话权限校验、storage 查询和响应编码 |
| `src/service/CMakeLists.txt` | 修改 | 把 `HistoryService.cpp` 编入 `liteim_service` |
| `server/main.cpp` | 修改 | 创建 `HistoryService` 并注册 `HistoryRequest` handler |
| `tests/service/history_service_test.cpp` | 新增 | 覆盖 Step 39 service 行为和 router 注册 |
| `tests/CMakeLists.txt` | 修改 | 接入 Step 39 service 测试 |
| `README.md` | 更新 | 记录 Step 39 runtime 和验证命令 |
| `docs/tutorials/step39_history_service.md` | 新增 | 讲解历史分页业务闭环 |
| `docs/process/task_plan.md / docs/process/findings.md / docs/process/progress.md` | 更新 | 记录 Step 39 过程、边界和验证结果 |

## 4. 核心接口与契约

### `HistoryServiceOptions`

```cpp
struct HistoryServiceOptions {
    std::uint32_t default_limit{20};
    std::uint32_t max_limit{50};
};
```

契约：

- `default_limit` 是请求不带 `Limit` 时的默认条数。
- `max_limit` 是 service 层允许的最大条数。
- 构造时要求 `1 <= default_limit <= max_limit <= 50`。

### `HistoryService`

```cpp
class HistoryService {
public:
    HistoryService(IStorage& storage, OnlineService& online_service,
                   HistoryServiceOptions options = HistoryServiceOptions{});

    Status registerHandlers(MessageRouter& router);
    Status handleHistory(const MessageRouter::RouterRequest& request, Packet& response);
    const HistoryServiceOptions& options() const noexcept;
};
```

它只依赖两个对象：

- `IStorage`：查询历史消息和群成员。
- `OnlineService`：根据当前 `session_id` 查询登录 user id。

请求字段：

| TLV | 含义 | 要求 |
| --- | --- | --- |
| `ConversationType` | 会话类型，1 私聊，2 群聊 | 必填 |
| `ConversationId` | 私聊 conversation id 或 group id | 必填，必须大于 0 |
| `MessageId` | `before_message_id` 游标 | 可选，0 表示第一页 |
| `Limit` | 本次最多返回多少条 | 可选，默认 20，最大 50 |

响应字段对每条消息重复写入：

- `MessageId`
- `ConversationType`
- `ConversationId`
- `SenderId`
- `ReceiverId`
- `MessageText`
- `TimestampMs`

客户端按相同下标把重复字段重新组装成消息列表。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

Alice 打开和 Bob 的私聊窗口：

```text
Alice user_id = 1001
Bob user_id   = 1002
conversation_type = 1
conversation_id = 10011002
limit = 20
```

客户端发送 `HistoryRequest` 后，服务端返回最近 20 条消息。Alice 上拉加载更早消息时，假设第一页最后一条是 `message_id=5001`，下一次请求带：

```text
MessageId = 5001
```

storage 层会查询 `message_id < 5001` 的更早消息。

### 2. 上下层调用连接

```text
客户端
    -> HistoryRequest
    -> TcpServer / Session
    -> MessageRouter
    -> business ThreadPool 执行 HistoryService::handleHistory()
    -> OnlineService::getUserBySession()
    -> HistoryService 校验会话权限
    -> IStorage::getHistory()
    -> MessageDao::getHistoryByConversation()
    -> MessageRouter 发回 HistoryResponse
```

网络层只负责收包和发包，MySQL 查询在 business 线程执行。

### 3. 整体运行链路

1. 客户端已登录，`OnlineService` 里存在 `session_id -> user_id` 绑定。
2. 客户端发送 `HistoryRequest`。
3. `MessageRouter` 解析 TLV，找到 `HistoryService` handler。
4. handler 读取当前登录 user id。
5. handler 从 TLV 构造 `HistoryQuery`。
6. handler 根据会话类型做权限校验。
7. handler 调用 `IStorage::getHistory(query, messages)`。
8. storage 层按 `(conversation_type, conversation_id, message_id)` 索引查询。
9. handler 把消息编码成重复 TLV 字段。
10. `MessageRouter` 把 `HistoryResponse` 发回当前 session。

### 4. 自身内部运行流程

`handleHistory()` 的核心顺序是：

```text
currentUserId()
    -> buildQuery()
    -> authorizeQuery()
    -> storage_.getHistory()
    -> appendMessages()
```

`authorizeQuery()` 分两条线：

```text
私聊:
    conversation_id 解出两个 user_id
    当前 user_id 必须是其中之一

群聊:
    storage_.findGroupById(group_id)
    storage_.getGroupMembers(group_id)
    当前 user_id 必须在成员列表中
```

### 5. 该项目代码在实际应用中的具体数据例子

私聊第一页请求：

```text
HistoryRequest:
  ConversationType = 1
  ConversationId   = 10011002
  Limit            = 20
```

响应里两条消息：

```text
MessageId        = 5002
ConversationType = 1
ConversationId   = 10011002
SenderId         = 1002
ReceiverId       = 1001
MessageText      = "收到，今晚看"
TimestampMs      = 1800000005002

MessageId        = 5001
ConversationType = 1
ConversationId   = 10011002
SenderId         = 1001
ReceiverId       = 1002
MessageText      = "今晚同步一下进度"
TimestampMs      = 1800000005001
```

下一页请求：

```text
HistoryRequest:
  ConversationType = 1
  ConversationId   = 10011002
  MessageId        = 5001
  Limit            = 20
```

SQL 语义是查询同一个会话里 `message_id < 5001` 的消息。

## 6. 关键实现点

### 1. 请求里的 `MessageId` 表示游标

协议里已经有 `TlvType::MessageId`，本 Step 没有新增 `BeforeMessageId`，而是在 `HistoryRequest` 中复用 `MessageId` 表示 `before_message_id`。响应里仍然重复写 `MessageId` 表示每条消息的真实 id。

### 2. limit 在 service 层截断

`HistoryServiceOptions` 默认 `default_limit=20`、`max_limit=50`。客户端不传 `Limit` 时查 20 条，传大于 50 的值会被截成 50，传 0 直接返回 `InvalidArgument`。

### 3. 私聊权限依赖 conversation id 规则

Step 36 的私聊 conversation id 规则是：

```text
小 user_id: min_id * 10000 + max_id
大 user_id: (min_id << 32) | max_id
```

因此 `HistoryService` 能从 `conversation_id=10011002` 反推出 `1001` 和 `1002`，再判断当前登录用户是不是其中之一。

### 4. 群聊权限依赖成员表

群聊不能只凭 `group_id` 查询，否则知道群 id 的非成员就能读取历史。Step 39 查询群历史前先确认 group 存在，再用 `getGroupMembers()` 判断当前用户是否在成员列表中。

### 5. service 不拼 SQL

`HistoryService` 只构造 `HistoryQuery`：

```cpp
query.conversation = {ConversationType::kGroup, 8801};
query.before_message_id = 5001;
query.limit = 20;
```

真正 SQL 仍然由 `MessageDao::getHistoryByConversation()` 负责，这样 service 层不关心 MySQL 字段、索引和 prepared statement 绑定细节。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| 未登录 session 可以查历史 | `HistoryRequiresLoggedInSession` 校验未绑定 session 被拒绝 |
| 默认 limit 错误 | `PrivateHistoryUsesDefaultLimitAndReturnsRecentMessages` 校验默认 20 |
| limit 过大导致一次查太多 | `LimitAboveMaxIsCappedAndBeforeCursorIsForwarded` 校验截成 50 |
| 游标没有传到 storage | 同一个测试校验 `before_message_id=5003` |
| `limit=0` 被当成无限制 | `LimitZeroIsRejected` 校验返回 `InvalidArgument` |
| 私聊历史泄露给非参与者 | `PrivateHistoryRejectsNonParticipant` 校验不调用 storage 查询 |
| 群聊历史泄露给非成员 | `GroupHistoryRejectsNonMember` 校验非成员不查历史 |
| 群成员不能查历史 | `GroupHistoryReturnsMessagesForMember` 校验群成员查询成功 |
| router 没注册 History handler | `RegisteredHandlerSendsHistoryResponseThroughRouter` 走真实 `MessageRouter` 和 business pool |

## 8. 验证命令

```bash
cmake --build build
ctest --test-dir build -R HistoryService --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
timeout 1s ./build/server/liteim_server || test $? -eq 124
```

本 Step 的 `HistoryService` 单元测试不依赖真实 MySQL / Redis；全量服务端 smoke 仍然需要本地 Docker MySQL / Redis 正常启动。

## 9. 面试表达

一句话：

> 我在业务层实现了历史消息分页服务，客户端通过 `HistoryRequest` 指定会话、游标和 limit，服务端校验权限后通过 storage 层按 message_id 倒序分页返回消息。

展开说：

> 这个功能没有让 service 直接拼 SQL，而是复用 `IStorage::getHistory()`，底层 DAO 走 `(conversation_type, conversation_id, message_id)` 索引。私聊会校验当前用户是否参与该 conversation，群聊会查 group members 防止非成员读取历史。分页上默认 20 条、最大 50 条，客户端上拉时把上一页最后一条 `message_id` 作为游标。

容易被追问：

> 为什么不用 offset 分页？因为聊天记录是持续增长的，用 offset 容易在新消息插入后出现跳页或重复；用 `message_id < before_message_id` 的游标分页更稳定，也更适合走索引。

## 10. 面试常见追问

### Q1：为什么 HistoryService 不能直接写 SQL？

因为 LiteIM 已经有 `IStorage` 边界。service 层负责业务规则和权限，storage 层负责 MySQL 细节。这样以后替换 DAO、加缓存或做测试 fake storage 时，不需要改 service 逻辑。

### Q2：为什么请求里用 `MessageId` 表示 `before_message_id`？

第一版协议已经有 `TlvType::MessageId`，没有必要为了一个语义再新增 TLV 类型。只要在文档里明确：请求中的 `MessageId` 是游标，响应中的重复 `MessageId` 是消息 id，就不会混淆。

### Q3：为什么要限制最大 50 条？

历史查询是用户打开会话时很高频的路径。限制最大条数能避免单个请求占用太多 MySQL 时间、内存和网络输出缓冲，也能降低慢客户端对服务端的影响。

### Q4：为什么群聊必须查成员？

`group_id` 不是秘密。只要知道一个群 id 就能查历史会造成数据泄露，所以群历史必须先确认当前登录用户在 `group_members` 里。

### Q5：私聊权限为什么可以从 conversation id 判断？

当前私聊 conversation id 是由两个 user id 生成的稳定值，比如 `1001` 和 `1002` 生成 `10011002`。服务端能反推出这两个参与者，判断当前 user id 是否属于这个会话。

### Q6：为什么返回结果按 `message_id DESC`？

打开会话最常见需求是先看最近消息，所以第一版返回最新消息在前。客户端如果需要正序展示，可以在渲染层反转；分页游标仍然用最后一条的 `message_id` 查询更早消息。
