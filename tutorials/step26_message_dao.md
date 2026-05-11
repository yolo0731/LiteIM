# Step 26：MessageDao 和 OfflineMessageDao

## 1. 概念

Step 26 把 Step 22 建好的 `messages` / `offline_messages` 两张表封装成 DAO。MySQL 是消息实体的真实来源，Redis 后续只做在线状态、未读数和限流状态，不保存最终消息正文。

本 Step 只做数据访问：

- `MessageDao` 保存私聊消息、保存群聊消息、按会话分页拉历史消息。
- `OfflineMessageDao` 保存待投递离线记录、拉取某个用户的 pending 离线消息、把离线记录标记为 delivered。
- DAO 方法会阻塞等待连接池和 MySQL 响应，后续只能放到 business `ThreadPool` 中调用。
- DAO 不持有 `Session`，不操作 `EventLoop`，不做消息路由，也不更新 Redis 未读数。

## 2. hpp 接口说明

### `MessageDao`

`MessageDao` 依赖 `MySqlPool`：

```cpp
explicit MessageDao(MySqlPool& pool,
                    std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));
```

public 方法：

- `savePrivateMessage(const MessageRecord&, MessageRecord&)`：插入私聊消息，成功后返回带 `message_id` 的完整记录。
- `saveGroupMessage(const MessageRecord&, MessageRecord&)`：插入群聊消息，成功后返回带 `message_id` 的完整记录。
- `getHistoryByConversation(const HistoryQuery&, std::vector<MessageRecord>&)`：按 `conversation_type + conversation_id` 查询历史消息。

`savePrivateMessage()` / `saveGroupMessage()` 接收 `MessageRecord`。调用方提供 `ConversationKey`，DAO 只验证类型和必要 id，不在这一层定义私聊会话 id 生成策略。群聊消息如果输入 `receiver_id == 0`，DAO 会用 `conversation.id` 作为 `receiver_id` 落库，和 Step 22 seed 数据保持一致。

历史查询规则：

- `before_message_id == 0` 表示第一页。
- `before_message_id != 0` 表示只查比这个 message id 更旧的记录。
- 返回顺序是 `message_id DESC`，也就是最新消息在前。
- `limit` 最大 50，超过 50 会截断；`limit == 0` 返回 `InvalidArgument`。

### `OfflineMessageDao`

`OfflineMessageDao` 同样依赖 `MySqlPool`：

```cpp
explicit OfflineMessageDao(MySqlPool& pool,
                           std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds(500));
```

public 方法：

- `saveOfflineMessage(user_id, message_id)`：插入一条待投递记录。
- `getOfflineMessages(user_id, messages)`：查询 `delivered = 0` 的 pending 离线消息，并 join `messages` 填完整 `MessageRecord`。
- `markOfflineDelivered(user_id, message_ids)`：把指定消息标记为 delivered。

`offline_messages` 有 `(user_id, message_id)` 唯一索引。重复保存同一条离线消息会返回 `AlreadyExists`，方便后续 service 层区分“重复投递记录”和“数据库不可用”。

### `MySqlConnection::executeSimple()`

Step 26 新增：

```cpp
Status executeSimple(const std::string& sql);
```

它用于 `START TRANSACTION`、`COMMIT`、`ROLLBACK` 这类无用户输入的控制语句。MySQL 不支持把 `START TRANSACTION` 放进 prepared statement 协议，所以事务边界走 `mysql_query()`。普通业务 SQL 仍然通过 `PreparedStatement` 绑定参数，不拼接用户输入。

## 3. 作用场景和运行流程

后续发送私聊消息的大致流程会是：

```text
business ThreadPool
  -> MessageDao::savePrivateMessage(message, saved_message)
  -> receiver 不在线时 OfflineMessageDao::saveOfflineMessage(receiver_id, saved_message.message_id)
  -> Redis 更新未读数
  -> EventLoop::queueInLoop() 推送或返回结果
```

后续打开会话加载历史消息会是：

```text
business ThreadPool
  -> MessageDao::getHistoryByConversation(query, messages)
  -> EventLoop::queueInLoop() 返回 HistoryResponse
```

DAO 内部保存一条消息：

```text
MySqlPool::acquire(timeout, guard)
  -> START TRANSACTION
  -> validate ConversationKey / sender / receiver
  -> INSERT INTO messages ...
  -> SELECT ... WHERE message_id = LAST_INSERT_ID()
  -> COMMIT
  -> 失败时 ROLLBACK
  -> MessageRecord
  -> ConnectionGuard 析构归还连接
```

DAO 内部拉离线消息：

```text
MySqlPool::acquire(timeout, guard)
  -> SELECT offline_messages JOIN messages
  -> OfflineMessageRecord + MessageRecord
  -> 只返回 delivered = 0 的记录
```

DAO 内部标记 delivered：

```text
MySqlPool::acquire(timeout, guard)
  -> START TRANSACTION
  -> UPDATE offline_messages SET delivered = 1 ...
  -> COMMIT
  -> 失败时 ROLLBACK
```

## 4. 测试

新增 `tests/storage/message_dao_test.cpp`：

- `MessageDaoTest.HeadersAreSelfContained`：`MessageDao` / `OfflineMessageDao` 头文件可独立使用。
- `MessageDaoIntegrationTest.SavePrivateMessagePersistsRecord`：私聊消息保存成功并返回完整字段。
- `MessageDaoIntegrationTest.SaveGroupMessagePersistsRecord`：群聊消息保存成功，默认把 group id 写入 `receiver_id`。
- `MessageDaoIntegrationTest.OfflineMessageSaveFetchAndDeliveredFlow`：保存离线消息、拉取 pending、标记 delivered 后不再重复拉取。
- `MessageDaoIntegrationTest.HistoryReturnsNewestMessagesBeforeCursor`：历史消息按 `message_id DESC` 返回，并支持 `before_message_id` 游标。
- `MessageDaoIntegrationTest.HistoryLimitIsCappedAtFifty`：`limit > 50` 时最多返回 50 条。

测试使用 `step26_` 用户名和消息文本前缀，SetUp/TearDown 只清理自己的测试用户、消息和离线记录，不修改 seed 用户 `alice`、`bob`、`mira_bot`。

## 5. 验证命令

```bash
docker compose -f docker/docker-compose.yml up -d --wait
cmake --build build
ctest --test-dir build -R "MessageDaoTest|MessageDaoIntegrationTest" --output-on-failure
ctest --test-dir build --output-on-failure
```

本 Step 不实现 Redis client、AuthService、ChatService、HistoryService、user-session 绑定或网络层运行时消息路由。它只把消息持久化和离线消息投递状态封装成 MySQL DAO。
