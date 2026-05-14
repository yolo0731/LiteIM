# Step 31：MySqlStorage 和 RedisCache 聚合适配层

Step 31 的目标是把 Step 21 定义的 `IStorage` / `ICache` 抽象接口，连接到 Step 23-30 已经实现好的 MySQL DAO 和 Redis cache 组件。

这一层不是新的数据库表，也不是新的业务 service。它的作用是做“统一入口”：Step 32 之后的 OnlineService、AuthService、ChatService、HistoryService 只依赖 `IStorage` / `ICache`，不需要知道底层有多少 DAO、Redis key 或连接池对象。

## 1. 概念

Step 21 只定义接口，Step 23-30 逐个实现具体能力：

- MySQL 侧：`MySqlConnection`、`MySqlPool`、`UserDao`、`FriendDao`、`GroupDao`、`MessageDao`、`OfflineMessageDao`。
- Redis 侧：`RedisClient`、`RedisPool`、`OnlineStatusCache`、`UnreadCounter`、`LoginRateLimiter`。

如果业务层直接依赖这些具体类，ChatService 以后可能要同时拿到 `MessageDao`、`OfflineMessageDao`、`UnreadCounter`、`OnlineStatusCache`，依赖会很散。

所以 Step 31 做两件事：

- `MySqlStorage : IStorage`：把多个 MySQL DAO 聚合成一个存储接口实现。
- `RedisCache : ICache`：把多个 Redis cache 组件聚合成一个缓存接口实现。

边界很重要：

- MySQL 仍然是消息、用户、好友、群组的事实来源。
- Redis 仍然只保存在线状态、未读计数、登录失败限制这类短期状态。
- Redis 未读数不参与 MySQL 事务；业务层要在 MySQL commit 成功后再递增 Redis。
- 这一 Step 不实现 `SessionManager`、`OnlineService`、AuthService、ChatService，也不把 MySQL / Redis 接入 Reactor I/O 线程。

## 2. 本 Step 新增 / 修改文件

新增：

```text
include/liteim/storage/MySqlStorage.hpp
src/storage/MySqlStorage.cpp
include/liteim/cache/RedisCache.hpp
src/cache/RedisCache.cpp
tests/storage/mysql_storage_integration_test.cpp
tests/cache/redis_cache_integration_test.cpp
tutorials/step31_storage_cache_adapters.md
```

同时更新：

```text
include/liteim/storage/IStorage.hpp
include/liteim/storage/StorageTypes.hpp
include/liteim/storage/FriendDao.hpp
src/storage/FriendDao.cpp
include/liteim/storage/MySqlConnection.hpp
src/storage/MySqlConnection.cpp
src/storage/CMakeLists.txt
src/cache/CMakeLists.txt
tests/CMakeLists.txt
README.md
task_plan.md
findings.md
progress.md
```

## 3. hpp 接口说明

### MySqlStorage.hpp

```cpp
class MySqlStorage final : public IStorage {
public:
    explicit MySqlStorage(MySqlPool& pool,
                          std::chrono::milliseconds acquire_timeout =
                              std::chrono::milliseconds{500});

    Status createUser(const CreateUserRequest& request, UserRecord& created_user) override;
    Status findUserByUsername(const std::string& username, UserRecord& user) override;
    Status findUserById(std::uint64_t user_id, UserRecord& user) override;

    Status addFriendship(std::uint64_t user_id, std::uint64_t friend_id) override;
    Status getFriends(std::uint64_t user_id, std::vector<UserProfileRecord>& friends) override;

    Status createGroup(const CreateGroupRequest& request, GroupRecord& created_group) override;
    Status addGroupMember(std::uint64_t group_id, std::uint64_t user_id) override;
    Status removeGroupMember(std::uint64_t group_id, std::uint64_t user_id) override;
    Status getGroupMembers(std::uint64_t group_id,
                           std::vector<GroupMemberRecord>& members) override;

    Status saveMessage(const MessageRecord& message, std::uint64_t& message_id) override;
    Status saveMessageWithOfflineRecipients(const MessageRecord& message,
                                            const std::vector<std::uint64_t>& offline_user_ids,
                                            MessageRecord& saved_message) override;
    Status saveOfflineMessage(std::uint64_t user_id, std::uint64_t message_id) override;
    Status getOfflineMessages(std::uint64_t user_id,
                              std::vector<OfflineMessageRecord>& messages) override;
    Status markOfflineDelivered(std::uint64_t user_id,
                                const std::vector<std::uint64_t>& message_ids) override;
    Status getHistory(const HistoryQuery& query, std::vector<MessageRecord>& messages) override;

private:
    MySqlPool& pool_;
    std::chrono::milliseconds acquire_timeout_;
    UserDao user_dao_;
    FriendDao friend_dao_;
    GroupDao group_dao_;
    MessageDao message_dao_;
    OfflineMessageDao offline_message_dao_;
};
```

`MySqlStorage` 不拥有 `MySqlPool`。它保存 pool 引用，并在内部组合多个 DAO。

大部分接口只是转发：

- 用户接口转发给 `UserDao`。
- 好友接口转发给 `FriendDao`。
- 群组接口转发给 `GroupDao`。
- 普通消息和历史接口转发或复用 `MessageDao`。
- 离线消息接口转发给 `OfflineMessageDao`。

特殊接口是 `saveMessageWithOfflineRecipients()`。它自己拿一条 MySQL 连接，开启事务，在同一个事务里写：

```text
messages
offline_messages
```

这样可以避免“消息已经保存，但离线投递记录没保存”的半成功状态。

### RedisCache.hpp

```cpp
class RedisCache final : public ICache {
public:
    explicit RedisCache(RedisPool& pool,
                        std::chrono::milliseconds acquire_timeout =
                            std::chrono::milliseconds{200});

    Status setUserOnline(const OnlineSession& session, std::chrono::seconds ttl) override;
    Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl) override;
    Status setUserOffline(std::uint64_t user_id) override;
    Status isUserOnline(std::uint64_t user_id, bool& online) override;
    Status getOnlineSession(std::uint64_t user_id, OnlineSession& session) override;

    Status incrUnread(const UnreadKey& key, std::uint64_t delta,
                      std::uint64_t& unread_count) override;
    Status getUnread(const UnreadKey& key, std::uint64_t& unread_count) override;
    Status clearUnread(const UnreadKey& key) override;

    Status allowLoginAttempt(const LoginAttemptKey& key, std::uint32_t max_failures,
                             bool& allowed) override;
    Status recordLoginFailure(const LoginAttemptKey& key, std::chrono::seconds ttl) override;
    Status clearLoginFailure(const LoginAttemptKey& key) override;

private:
    OnlineStatusCache online_;
    UnreadCounter unread_;
    LoginRateLimiter login_limiter_;
};
```

`RedisCache` 也不拥有 `RedisPool`，但它把 pool 传给内部三个组件：

- `OnlineStatusCache`：负责 `online:user:<user_id>`。
- `UnreadCounter`：负责 `unread:user:<user_id>:conversation:<type>:<id>`。
- `LoginRateLimiter`：负责 `login:failure:<username_size>:<username>:<ip_size>:<ip>`。

`RedisCache` 本身不拼 Redis 命令，只负责把 `ICache` 接口转发到对应组件。

## 4. 作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

后续业务层会依赖接口：

```cpp
class ChatService {
public:
    ChatService(IStorage& storage, ICache& cache);
};
```

单元测试可以传入 fake storage / fake cache。真实服务端启动时，则可以创建：

```cpp
MySqlPool mysql_pool(mysql_config);
RedisPool redis_pool(redis_config);

MySqlStorage storage(mysql_pool);
RedisCache cache(redis_pool);
```

这样业务层看到的是统一接口，运行时用的是 MySQL 和 Redis 的真实实现。

### 2. 上下层调用连接

```text
AuthService / ChatService / HistoryService
    -> IStorage / ICache
    -> MySqlStorage / RedisCache
    -> DAO / cache component
    -> MySqlPool / RedisPool
    -> MySQL / Redis service
```

网络 I/O 线程不直接走这条链路。后续 `MessageRouter` 会把阻塞 MySQL / Redis 任务投递到 business `ThreadPool`，业务完成后再通过 `EventLoop::queueInLoop()` 把响应送回连接所在的 I/O loop。

### 3. MySqlStorage 内部运行流程

普通转发流程：

```text
IStorage::findUserById(1001)
    -> MySqlStorage::findUserById()
    -> UserDao::findUserById()
    -> MySqlPool::acquire()
    -> prepared SELECT
```

消息 + 离线记录事务流程：

```text
saveMessageWithOfflineRecipients(message, offline_user_ids, saved_message)
    -> validate conversation / sender / receiver
    -> offline_user_ids 去重并拒绝 0
    -> acquire MySqlConnection
    -> START TRANSACTION
    -> INSERT INTO messages
    -> SELECT ... WHERE message_id = LAST_INSERT_ID()
    -> for each offline user: INSERT INTO offline_messages
    -> COMMIT
```

任一步失败都会 `ROLLBACK`。如果消息已经查回到 `saved_message`，但离线记录或 `COMMIT` 前失败，函数会清空 `saved_message`，避免上层误以为消息已经完整成功。

### 4. RedisCache 内部运行流程

`RedisCache` 的流程更简单，它主要做接口分派：

```text
ICache::setUserOnline()
    -> RedisCache::setUserOnline()
    -> OnlineStatusCache::setUserOnline()
    -> Redis SETEX online:user:<id> ttl value

ICache::incrUnread()
    -> RedisCache::incrUnread()
    -> UnreadCounter::incrUnread()
    -> Redis EVAL + INCRBY

ICache::recordLoginFailure()
    -> RedisCache::recordLoginFailure()
    -> LoginRateLimiter::recordFailure()
    -> Redis EVAL + INCR + EXPIRE
```

### 5. 该项目代码在实际应用中的具体数据例子

Alice 给离线 Bob 发私聊：

```text
sender_id = 1001
receiver_id = 1002
conversation_type = 1
conversation_id = 10011002
text = "hello bob"
offline_user_ids = {1002, 1002}
```

ChatService 后续会调用：

```cpp
MessageRecord saved_message;
storage.saveMessageWithOfflineRecipients(message, {1002, 1002}, saved_message);
```

`MySqlStorage` 会先把离线用户去重成 `{1002}`，然后在一个事务中插入一条 `messages` 和一条 `offline_messages`。成功后得到真实 `message_id`，例如 `5001`。

MySQL commit 成功后，业务层再调用：

```cpp
std::uint64_t unread = 0;
cache.incrUnread({1002, {ConversationType::kPrivate, 10011002}}, 1, unread);
```

Redis 里会更新：

```text
unread:user:1002:conversation:1:10011002 -> 1
```

如果 Bob 在线，业务层还可以通过：

```cpp
OnlineSession session;
cache.getOnlineSession(1002, session);
```

读取 `online:user:1002`，再通过 Step 32 的 `SessionManager` 找到当前进程里的连接。

## 测试设计

Step 31 的测试重点是证明真实适配层可以替代 fake 接口：

- `StorageInterfaceTest` / `CacheInterfaceTest` 继续证明接口可被测试替身实现。
- `MySqlStorageIntegrationTest` 覆盖真实 `IStorage` 创建用户、好友公开资料、消息 + 离线记录事务提交、离线记录失败回滚。
- `RedisCacheIntegrationTest` 覆盖真实 `ICache` 在线状态、未读计数和登录失败限制。
- `FriendGroupDaoIntegrationTest` 验证好友列表返回 `UserProfileRecord`，不暴露 `password_hash` / `password_salt`。
- `MySqlIntegrationTest` 验证 `PreparedStatement::bindUInt64()` 可以绑定 MySQL `BIGINT UNSIGNED`。

这些测试依赖本地 Docker MySQL / Redis。如果服务不可用，当前集成测试按项目策略跳过或给出明确依赖提示，不影响纯单元测试。

## 验证命令

```bash
cmake --build build
ctest --test-dir build -R "StorageInterfaceTest|MySqlConnection|FriendGroupDao|MySqlStorage|UnreadCounter|LoginRateLimiter|RedisCache|CacheInterfaceTest" --output-on-failure
ctest --test-dir build -R "MySqlIntegrationTest|MessageDaoIntegrationTest" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 面试时怎么讲

可以这样说：

> Step 31 是存储和缓存层的收束。Step 21 先定义 `IStorage` / `ICache`，Step 23-30 分别实现 MySQL DAO 和 Redis cache 组件，Step 31 再用 `MySqlStorage` / `RedisCache` 把具体组件聚合成统一接口。这样后续业务层只依赖接口，测试时可以注入 fake，真实运行时注入 MySQL / Redis 实现。`MySqlStorage::saveMessageWithOfflineRecipients()` 额外提供一个事务边界，保证消息行和离线投递行一起成功或一起回滚。

## 面试常见追问

### Q1：为什么不让 ChatService 直接用 MessageDao / OfflineMessageDao？

因为业务层直接依赖多个 DAO 会让依赖分散，测试也更麻烦。`IStorage` 给业务层一个稳定边界，DAO 细节留在 storage 层内部。

### Q2：MySqlStorage 是不是 ORM？

不是。它不做对象关系映射，也不隐藏 SQL 模型。它只是把已有 DAO 聚合成接口实现，SQL 仍然写在 DAO / storage 实现里。

### Q3：为什么 Redis 未读数不放进 MySQL 事务？

MySQL 和 Redis 是两个独立系统，没有共享事务。第一版选择简单可靠的顺序：先 MySQL commit，成功后再递增 Redis 未读数。Redis 未读数是快捷状态，不是消息正文的事实来源。

### Q4：如果 MySQL commit 成功，但 Redis 未读递增失败怎么办？

消息不会丢，因为消息正文和离线记录已经在 MySQL。Redis 未读数可能短暂不准，后续可以在拉取离线消息或打开会话时用 MySQL 状态修正。第一版不引入跨资源事务。

### Q5：为什么好友列表改成 UserProfileRecord？

好友列表只需要公开资料，例如 `user_id`、`username`、`nickname`、`created_at_ms`。`password_hash` / `password_salt` 属于认证字段，不应该穿过好友列表接口。

### Q6：RedisCache 里面为什么只是转发？

这是有意的。`OnlineStatusCache`、`UnreadCounter`、`LoginRateLimiter` 各自维护自己的 key 格式和命令语义；`RedisCache` 只负责实现 `ICache` 统一入口，避免重复业务逻辑。
