# Step 26：MessageDao 和 OfflineMessageDao

Step 26 的目标是在 MySQL users 基础上实现消息持久化和离线消息 DAO。

到 Step 25 为止，LiteIM 已经能创建和查询用户，但 IM 最核心的数据是消息。Step 26 解决的问题是：

```text
私聊 / 群聊消息如何落库，离线用户的待投递消息如何保存和标记？
```

答案是实现 `MessageDao` 和 `OfflineMessageDao`。

## 1. 概念

消息持久化分两层：

- `messages` 表保存真实消息内容。
- `offline_messages` 表保存“某个用户还有某条消息未投递”的关系。

这两者不能混在一起：

```text
MessageRecord
    -> 真实消息，属于某个会话

OfflineMessageRecord
    -> 投递队列记录，属于某个接收用户
    -> 内部包含一条 MessageRecord
```

Step 26 只做 DAO，不做 ChatService，不做在线路由，不做 Redis 未读计数，也不修改网络层。

## 2. 本 Step 新增 / 修改文件

新增：

```text
include/liteim/storage/MessageDao.hpp
include/liteim/storage/OfflineMessageDao.hpp
src/storage/MessageDao.cpp
src/storage/OfflineMessageDao.cpp
tests/storage/message_dao_test.cpp
tutorials/step26_message_dao.md
```

同时更新：

```text
include/liteim/storage/MySqlConnection.hpp
src/storage/MySqlConnection.cpp
src/storage/CMakeLists.txt
tests/CMakeLists.txt
README.md
task_plan.md
findings.md
progress.md
```

`MySqlConnection::executeSimple()` 在本 Step 开始被事务控制真实使用。

## 3. MessageDao.hpp 接口说明

```cpp
class MessageDao {
public:
    explicit MessageDao(MySqlPool& pool,
                        std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status savePrivateMessage(const MessageRecord& message, MessageRecord& saved_message);
    Status saveGroupMessage(const MessageRecord& message, MessageRecord& saved_message);
    Status getHistoryByConversation(const HistoryQuery& query, std::vector<MessageRecord>& messages);
};
```

### 构造函数

`MessageDao` 保存非 owning `MySqlPool*` 和 acquire timeout。

线程边界：

- DAO 方法会阻塞在 MySQL。
- 只能在 business 线程使用。
- 不允许在 I/O loop 直接调用。

关键成员：

- `MySqlPool* pool_`：借连接。
- `std::chrono::milliseconds acquire_timeout_`：借连接超时。

### `savePrivateMessage()`

```cpp
Status savePrivateMessage(const MessageRecord& message, MessageRecord& saved_message);
```

保存私聊消息。

输入要求：

- `message.conversation.type == ConversationType::kPrivate`。
- `message.conversation.id != 0`。
- `message.sender_id != 0`。
- `message.receiver_id != 0`。
- id 字段不能超过 MySQL signed bind 范围。

内部使用事务：

```text
START TRANSACTION
    -> INSERT INTO messages
    -> SELECT ... WHERE message_id = LAST_INSERT_ID()
COMMIT
```

如果插入或查回失败，执行 `ROLLBACK` 后返回错误。

输出：

- `saved_message` 是数据库查回的完整记录，包含 `message_id` 和最终 `created_at_ms`。

### `saveGroupMessage()`

```cpp
Status saveGroupMessage(const MessageRecord& message, MessageRecord& saved_message);
```

保存群聊消息。

输入要求：

- `message.conversation.type == ConversationType::kGroup`。
- `message.conversation.id != 0`。
- `message.sender_id != 0`。

群聊 `receiver_id` 边界：

- 如果输入 `receiver_id == 0`，DAO 落库时使用 `conversation.id` 作为 `receiver_id`。
- 这样和 Step 22 seed 数据一致，也方便第一版查询观察。

其他流程和 `savePrivateMessage()` 相同。

### `getHistoryByConversation()`

```cpp
Status getHistoryByConversation(const HistoryQuery& query, std::vector<MessageRecord>& messages);
```

按会话分页查询历史消息。

输入要求：

- `query.conversation.type` 必须是 private 或 group。
- `query.conversation.id != 0`。
- `query.limit != 0`。
- `query.before_message_id` 必须能绑定到 signed int64。

分页语义：

- `before_message_id == 0`：从最新消息开始查。
- `before_message_id != 0`：查 `message_id < before_message_id` 的更旧消息。
- SQL 按 `message_id DESC` 返回。
- `limit` 最大截断为 50。

输出：

- `messages` 调用开始先 clear。
- 查询成功但没有消息时返回 ok + 空 vector。

### private helper

`.cpp` 内部 helper 包括：

- `validateConversationKey()`：校验会话类型和 id。
- `normalizeMessageForInsert()`：校验消息输入、补 `created_at_ms`、处理群聊 receiver。
- `bindUint64()`：检查无符号 id 是否能安全绑定到 MySQL signed int64。
- `rowToMessageRecord()`：把查询行转换成 `MessageRecord`。
- `queryLastInsertedMessage()`：在同一连接内查回 `LAST_INSERT_ID()`。
- `rollbackAndReturn()`：失败时回滚并返回原始错误。

这些 helper 是 DAO 内部实现细节，不放进 public API。

## 4. OfflineMessageDao.hpp 接口说明

```cpp
class OfflineMessageDao {
public:
    explicit OfflineMessageDao(MySqlPool& pool,
                               std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));

    Status saveOfflineMessage(std::uint64_t user_id, std::uint64_t message_id);
    Status getOfflineMessages(std::uint64_t user_id, std::vector<OfflineMessageRecord>& messages);
    Status markOfflineDelivered(std::uint64_t user_id, const std::vector<std::uint64_t>& message_ids);
};
```

### `saveOfflineMessage()`

保存“某用户待投递某消息”的记录。

输入要求：

- `user_id != 0`。
- `message_id != 0`。
- id 不能超过 MySQL signed bind 范围。

失败语义：

- `(user_id, message_id)` 唯一键冲突转换成 `AlreadyExists`。
- affected rows 不是 1 返回 `InternalError`。

### `getOfflineMessages()`

查询某用户所有 pending 离线消息。

SQL 语义：

- 只返回 `delivered = 0`。
- join `messages`，一次填充完整 `OfflineMessageRecord`。
- 按 `offline_message_id ASC` 返回，保持投递顺序。

输出：

- 开始先 clear。
- 没有 pending 消息返回 ok + 空 vector。

### `markOfflineDelivered()`

把一批消息标记为已投递。

输入语义：

- `message_ids.empty()` 直接返回 ok。
- `user_id` 和每个 `message_id` 都必须有效。

事务语义：

```text
START TRANSACTION
    -> UPDATE offline_messages for each message_id
COMMIT
```

任意一次 update 失败就关闭当前 statement，执行 `ROLLBACK`，返回错误。

注意：

- `affected_rows == 0` 不算错误，表示这条消息可能已投递、不是该用户的消息或不存在。
- 这个方法的目标是把 pending 消息推进到 delivered，不负责判断业务上是否必须每条都命中。

### private helper

`.cpp` 内部 helper 包括：

- `rowToOfflineMessageRecord()`：解析 join 后的 10 列结果。
- `parseConversationType()`：把 DB 里的 `1/2` 转成 `ConversationType`。
- `bindUint64()`：校验 id。
- `rollbackSilently()`：事务失败时尽力回滚。

## 5. MySqlConnection::executeSimple() 补充说明

Step 26 开始需要事务控制语句：

```sql
START TRANSACTION
COMMIT
ROLLBACK
```

MySQL 不支持把这类语句都当作普通业务 prepared statement 使用，所以 `MySqlConnection::executeSimple()` 走 `mysql_query()`。

边界：

- 只用于固定事务控制语句。
- 不用于拼接用户输入 SQL。
- 普通 insert/select/update 仍然走 `PreparedStatement`。

## MessageDao / OfflineMessageDao 的作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

后续 ChatService 会使用这两个 DAO：

- 收到私聊消息时，先 `savePrivateMessage()`。
- 收到群聊消息时，先 `saveGroupMessage()`。
- 如果目标用户离线，为目标用户写 `saveOfflineMessage()`。
- 用户上线后，拉取 `getOfflineMessages()` 并投递。
- 投递成功后，调用 `markOfflineDelivered()`。
- 用户打开历史记录时，调用 `getHistoryByConversation()`。

### 2. 上下层调用连接

```text
Chat Packet
    -> Session
    -> business ThreadPool
    -> ChatService
    -> MessageDao / OfflineMessageDao
    -> MySqlPool
    -> messages / offline_messages
    -> EventLoop::queueInLoop()
    -> Session::sendPacket()
```

DAO 不判断用户是否在线；在线状态由后续 Redis cache 负责。

### 3. 整体运行链路

私聊发送链路：

1. ChatService 校验发送者和接收者。
2. 调用 `MessageDao::savePrivateMessage()` 落库。
3. 如果接收者在线，后续直接投递。
4. 如果接收者离线，调用 `OfflineMessageDao::saveOfflineMessage()`。
5. 后续还会更新 Redis 未读计数。

用户上线拉离线消息链路：

1. AuthService 登录成功。
2. ChatService 或 OfflineService 调用 `getOfflineMessages(user_id)`。
3. 把每条消息投递给用户当前 Session。
4. 投递成功后调用 `markOfflineDelivered(user_id, ids)`。

历史消息链路：

1. 客户端请求某个 conversation 的历史。
2. service 构造 `HistoryQuery`。
3. DAO 查询 `messages`，按 `message_id DESC` 返回最多 50 条。

### 4. 自身内部运行流程

消息保存流程：

```text
validate / normalize MessageRecord
    -> pool.acquire()
    -> START TRANSACTION
    -> INSERT messages
    -> SELECT LAST_INSERT_ID()
    -> rowToMessageRecord()
    -> COMMIT
```

历史查询流程：

```text
validate HistoryQuery
    -> cap limit to 50
    -> pool.acquire()
    -> prepare SELECT by conversation
    -> optional before_message_id condition
    -> executeQuery()
    -> rowToMessageRecord() for every row
```

离线消息拉取流程：

```text
pool.acquire()
    -> SELECT offline_messages JOIN messages
    -> WHERE user_id = ? AND delivered = 0
    -> ORDER BY offline_message_id ASC
    -> rowToOfflineMessageRecord()
```

标记 delivered 流程：

```text
validate ids
    -> pool.acquire()
    -> START TRANSACTION
    -> repeated UPDATE delivered = 1
    -> COMMIT
```

### 5. 小例子和边界

小例子：

```cpp
MessageRecord input;
input.conversation = {ConversationType::kPrivate, 10011002};
input.sender_id = 1001;
input.receiver_id = 1002;
input.text = "hello bob";

MessageRecord saved;
const auto status = message_dao.savePrivateMessage(input, saved);
```

边界：

- 私聊 receiver_id 不能为 0。
- 群聊 receiver_id 可由 DAO 补成 group id。
- `limit == 0` 是无效参数。
- `limit > 50` 会截断到 50。
- 离线消息唯一键冲突返回 `AlreadyExists`。
- DAO 不做在线判断、不投递消息、不更新 Redis 未读数。

## 后续实现 / 关键设计说明

Step 26 只打通 messages 和 offline_messages。

它不实现：

- ChatService。
- OnlineStatusCache。
- UnreadCounter。
- 群成员扩散策略。
- 网络 runtime 消息路由。

消息落库和离线队列先稳定后，后续 service 才能安全组合它们。

## 测试设计

测试覆盖：

- 头文件自包含。
- 私聊消息保存并查回完整记录。
- 群聊消息保存并查回完整记录。
- 离线消息 save / fetch / delivered 完整流程。
- 历史消息按 cursor 查询更旧消息。
- 历史查询 limit 最大 50。
- Docker MySQL 不可用时集成测试按当前测试策略 skip 或明确失败。

## 验证命令

```bash
cmake --build build
docker compose -f docker/docker-compose.yml ps
ctest --test-dir build -R "MessageDao|OfflineMessageDao" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 面试时怎么讲

可以这样说：

> Step 26 实现消息 DAO，把真实消息和离线投递关系分开。`MessageDao` 负责私聊/群聊消息落库和历史分页，保存消息时用事务完成 insert 和 `LAST_INSERT_ID()` 查回；`OfflineMessageDao` 负责保存 pending 离线消息、拉取未投递消息和批量标记 delivered。事务控制用 `MySqlConnection::executeSimple()`，普通业务 SQL 仍然走 prepared statement。DAO 只做数据访问，不判断在线状态，也不做 Redis 未读计数。

## 提交信息

```text
feat(storage): implement message dao and offline messages
```
