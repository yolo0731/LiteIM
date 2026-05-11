# Step 21：定义 IStorage / ICache 抽象

Step 21 的目标不是连接 MySQL 或 Redis，而是先把业务层需要的“存储能力”和“缓存能力”抽象出来。

到 Step 20 为止，LiteIM 已经有高性能网络底座：

```text
TcpServer
  -> Session 读写 Packet
  -> ThreadPool 预留业务执行位置
  -> TimerManager 心跳超时
  -> SignalWatcher 优雅退出
```

但后续注册、登录、好友、群聊、私聊、离线消息都不能直接把 SQL 或 Redis 命令散落在业务代码里。Step 21 解决的问题是：

```text
业务服务以后应该依赖什么接口，才能既能落 MySQL，又不把网络层和数据库细节绑死？
```

答案是先定义 `IStorage` 和 `ICache` 两条接口边界。

## 1. 概念

`IStorage` 表示持久化存储能力，最终主线实现会落到 MySQL：

- 用户信息。
- 好友关系。
- 群组和群成员。
- 私聊 / 群聊消息。
- 离线消息。
- 历史消息分页查询。

`ICache` 表示短期状态缓存能力，最终主线实现会落到 Redis：

- 在线状态。
- 未读计数。
- 登录失败限制。

这一步只定义“业务层希望看到的接口”，不定义 MySQL 表结构，不定义 Redis key，不写 DAO，也不接入 `TcpServer`。这样后续 Step 可以按顺序逐层实现：

```text
Step 21: IStorage / ICache 接口
Step 22: Docker MySQL / Redis 环境和 SQL schema
Step 23-27: MySQL wrapper / pool / DAO
Step 28-30: Redis wrapper / pool / cache
Step 31+: AuthService / ChatService 通过接口或 DAO 接入业务线程池
```

接口风格沿用 LiteIM 现有习惯：

- 函数返回 `Status`。
- 查询结果通过输出参数返回。
- 不引入 `Result<T>`、future、协程或异步接口。
- 不在接口里暴露 MySQL / Redis 具体类型。

## 2. 本 Step 新增 / 修改文件

新增：

```text
include/liteim/storage/StorageTypes.hpp
include/liteim/storage/IStorage.hpp
include/liteim/cache/CacheTypes.hpp
include/liteim/cache/ICache.hpp
tests/storage/storage_interface_test.cpp
tests/cache/cache_interface_test.cpp
tutorials/step21_storage_cache_interfaces.md
```

同时更新：

```text
src/storage/CMakeLists.txt
src/cache/CMakeLists.txt
src/CMakeLists.txt
tests/CMakeLists.txt
README.md
task_plan.md
findings.md
progress.md
```

`liteim_storage` 和 `liteim_cache` 在本 Step 先是接口目标，主要作用是让业务、测试和后续真实实现都能 include 同一组头文件。

## 3. StorageTypes.hpp 接口说明

`StorageTypes.hpp` 放 MySQL 业务实体对应的 DTO。它不 include MySQL C API，也不保存数据库连接，只表达业务数据。

### `ConversationType`

```cpp
enum class ConversationType : std::uint8_t {
    kPrivate = 1,
    kGroup = 2,
};
```

会话类型固定为私聊和群聊。数值会和 MySQL `messages.conversation_type` 对齐：`1` 表示私聊，`2` 表示群聊。

### `ConversationKey`

```cpp
struct ConversationKey {
    ConversationType type{ConversationType::kPrivate};
    std::uint64_t id{0};
};
```

它是历史消息和未读计数共同使用的会话键。

- 私聊时，`id` 表示业务层生成的私聊 conversation id。
- 群聊时，`id` 表示 `group_id`。
- `id == 0` 通常表示无效输入，后续 DAO 会做参数校验。

### 用户类型

`CreateUserRequest` 是创建用户的输入：

- `username`：唯一用户名。
- `password_hash`：密码 hash，不在 DAO 层计算。
- `password_salt`：密码盐。
- `nickname`：展示昵称。

`UserRecord` 是查询或创建后的完整用户记录：

- `user_id`：MySQL 自增主键。
- `username`、`password_hash`、`password_salt`、`nickname`：users 表字段。
- `created_at_ms`：毫秒时间戳。

`UserProfileRecord` 是可以给好友列表、搜索结果、群成员展示使用的公开资料：

- `user_id`：用户 id。
- `username`、`nickname`：公开展示字段。
- `created_at_ms`：账号创建时间。

它不包含 `password_hash` 和 `password_salt`。认证相关查询仍然使用完整 `UserRecord`，展示型接口不应把认证字段带到业务响应边界。

接口层不做密码校验，也不做注册规则判断；这些留给后续 AuthService。

### 群组类型

`CreateGroupRequest` 包含：

- `owner_id`：群主用户 id。
- `group_name`：群名。

`GroupRecord` 包含：

- `group_id`：MySQL 自增群 id。
- `owner_id`：群主。
- `group_name`：群名。
- `created_at_ms`：创建时间。

`GroupMemberRecord` 用于群成员列表：

- `user_id`：成员用户 id。
- `username`、`nickname`：来自 users 表。
- `joined_at_ms`：入群时间。

### 消息类型

`MessageRecord` 表示一条已落库或待落库消息：

- `message_id`：MySQL 自增消息 id；新消息输入时可以为 0。
- `conversation`：私聊或群聊会话键。
- `sender_id`：发送者。
- `receiver_id`：私聊接收者；群聊第一版可使用群 id。
- `text`：消息文本。
- `created_at_ms`：创建时间；DAO 可在缺省时补当前时间。

`OfflineMessageRecord` 表示一条待投递离线消息记录：

- `offline_message_id`：离线消息表主键。
- `user_id`：需要投递给哪个用户。
- `message`：对应的完整消息。
- `created_at_ms`：离线记录创建时间。

`HistoryQuery` 表示历史消息分页：

- `conversation`：查哪个会话。
- `before_message_id`：游标，0 表示从最新开始。
- `limit`：最多返回多少条，默认 50，后续 DAO 会限制最大值。

## 4. IStorage.hpp 接口说明

`IStorage` 是业务层眼里的持久化接口。它只有虚析构和纯虚函数，不持有连接池，也不规定 MySQL 实现方式。

### 生命周期和所有权

```cpp
class IStorage {
public:
    virtual ~IStorage() = default;
    ...
};
```

业务服务可以通过引用、指针或智能指针持有 `IStorage`。接口本身不拥有任何数据库资源，真实资源由后续 MySQL DAO / Pool 实现持有。

### 用户接口

```cpp
Status createUser(const CreateUserRequest& request, UserRecord& created_user);
Status findUserByUsername(const std::string& username, UserRecord& user);
Status findUserById(std::uint64_t user_id, UserRecord& user);
```

- `createUser()` 创建用户，并把完整记录写入输出参数。
- `findUserByUsername()` / `findUserById()` 查询不存在时应返回 `NotFound`。
- 用户名唯一冲突后续会映射成 `AlreadyExists`。

### 好友接口

```cpp
Status addFriendship(std::uint64_t user_id, std::uint64_t friend_id);
Status getFriends(std::uint64_t user_id, std::vector<UserProfileRecord>& friends);
```

第一版好友关系是直接双向添加，不做好友申请审批。`getFriends()` 返回公开 `UserProfileRecord`，不返回密码 hash 或 salt。

### 群组接口

```cpp
Status createGroup(const CreateGroupRequest& request, GroupRecord& created_group);
Status addGroupMember(std::uint64_t group_id, std::uint64_t user_id);
Status removeGroupMember(std::uint64_t group_id, std::uint64_t user_id);
Status getGroupMembers(std::uint64_t group_id, std::vector<GroupMemberRecord>& members);
```

第一版只区分 owner 和普通成员。创建群时应保证 owner 成为成员；移除 owner 的行为由后续具体 DAO 拒绝。

### 消息和离线消息接口

```cpp
Status saveMessage(const MessageRecord& message, std::uint64_t& message_id);
Status saveMessageWithOfflineRecipients(const MessageRecord& message,
                                        const std::vector<std::uint64_t>& offline_user_ids,
                                        MessageRecord& saved_message);
Status saveOfflineMessage(std::uint64_t user_id, std::uint64_t message_id);
Status getOfflineMessages(std::uint64_t user_id, std::vector<OfflineMessageRecord>& messages);
Status markOfflineDelivered(std::uint64_t user_id, const std::vector<std::uint64_t>& message_ids);
Status getHistory(const HistoryQuery& query, std::vector<MessageRecord>& messages);
```

- `saveMessage()` 负责持久化消息并返回消息 id。
- `saveMessageWithOfflineRecipients()` 在同一个 MySQL 事务中保存 `messages` 行和多个 `offline_messages` 行；Redis 未读计数仍由业务层在事务成功后处理。
- `saveMessageWithOfflineRecipients()` 的 MySQL 实现会先对 `offline_user_ids` 去重；如果 `queryLastInsertedMessage()` 已经查回消息但后续离线记录插入或 `COMMIT` 前失败，会 `ROLLBACK` 并清空 `saved_message`，避免上层误用半成功输出。
- `saveOfflineMessage()` 只保存“某用户待投递某消息”的关系。
- `getOfflineMessages()` 只拉取 pending 离线消息。
- `markOfflineDelivered()` 标记已投递，避免重复推送。
- `getHistory()` 按会话和游标分页。

失败语义统一通过 `Status` 表达；输出参数只有在 `Status::ok()` 时才应被上层当作有效结果。

### 线程边界

`IStorage` 后续真实实现会阻塞在 MySQL 上，所以业务线程可以调用，Reactor I/O 线程不能直接调用。网络线程收到 Packet 后应把业务任务投递到 `ThreadPool`，业务完成后再通过 `EventLoop::queueInLoop()` 回到 `Session` 所属 I/O loop 发送响应。

## 5. CacheTypes.hpp 接口说明

`CacheTypes.hpp` 放 Redis 业务状态相关 DTO。

### `OnlineSession`

```cpp
struct OnlineSession {
    std::uint64_t user_id{0};
    std::uint64_t session_id{0};
    std::string server_id;
    std::int64_t last_active_time_ms{0};
};
```

它表示一个在线用户当前绑定到哪个服务端和哪个逻辑 session：

- `user_id`：在线用户。
- `session_id`：LiteIM 内部逻辑连接 id。
- `server_id`：当前服务器实例标识；单机第一版也保留这个字段，便于后续扩展。
- `last_active_time_ms`：最后活跃时间，用于观察和调试，真实过期由 Redis TTL 控制。

### `UnreadKey`

```cpp
struct UnreadKey {
    std::uint64_t user_id{0};
    ConversationKey conversation;
};
```

未读计数必须同时知道“哪个用户”和“哪个会话”。它复用 `StorageTypes.hpp` 的 `ConversationKey`，避免消息历史和未读数使用两套会话定义。

### `LoginAttemptKey`

```cpp
struct LoginAttemptKey {
    std::string username;
    std::string remote_ip;
};
```

登录失败限制按用户名和远端 IP 组合统计。Step 21 只定义键，不实现 Redis token bucket 或计数规则。

## 6. ICache.hpp 接口说明

`ICache` 是业务层眼里的缓存接口。它和 `IStorage` 一样，只暴露业务动作，不暴露 Redis 命令。

### 在线状态接口

```cpp
Status setUserOnline(const OnlineSession& session, std::chrono::seconds ttl);
Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl);
Status setUserOffline(std::uint64_t user_id);
Status isUserOnline(std::uint64_t user_id, bool& online);
Status getOnlineSession(std::uint64_t user_id, OnlineSession& session);
```

- 登录成功后写在线状态。
- 心跳或有效业务包到达后刷新 TTL。
- 断开连接时删除在线状态。
- 查询在线状态可只要 bool，也可读取完整 session。
- key 不存在时，`isUserOnline()` 应返回 ok 且 `online=false`；`getOnlineSession()` 后续具体实现会返回 `NotFound`。

### 未读计数接口

```cpp
Status incrUnread(const UnreadKey& key, std::uint64_t delta, std::uint64_t& unread_count);
Status getUnread(const UnreadKey& key, std::uint64_t& unread_count);
Status clearUnread(const UnreadKey& key);
```

后续 ChatService 在接收者离线或未读需要统计时递增，用户读完会话后清零。本 Step 不定义 Redis key 字符串。

### 登录失败限制接口

```cpp
Status allowLoginAttempt(const LoginAttemptKey& key, std::uint32_t max_failures, bool& allowed);
Status recordLoginFailure(const LoginAttemptKey& key, std::chrono::seconds ttl);
Status clearLoginFailure(const LoginAttemptKey& key);
```

后续 AuthService 使用它做登录失败限制：

- 登录前先判断是否允许尝试。
- 密码错误后记录失败。
- 登录成功后清除失败状态。

### 线程边界和失败语义

`ICache` 后续真实实现会阻塞在 Redis 上，所以同样只能在 business `ThreadPool` 使用。所有失败都走 `Status`，包括参数错误、连接池超时、Redis 命令失败和解析失败。

## IStorage / ICache 的作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

Step 21 的接口位于业务服务和真实存储/cache 实现之间。

以后 AuthService、FriendService、ChatService、HistoryService 会依赖这些接口或依赖由它们拆出的具体 DAO/cache 组件。网络层只负责收包、解包、投递业务任务和回写响应，不直接拼 SQL 或 Redis 命令。

### 2. 上下层调用连接

```text
Session 收到 Packet
    -> TcpServer / 后续业务分发器
    -> ThreadPool 业务任务
    -> AuthService / ChatService / HistoryService
    -> IStorage / ICache
    -> MySQL DAO / Redis cache 实现
    -> EventLoop::queueInLoop()
    -> Session::sendPacket()
```

`IStorage` / `ICache` 的上游是业务 service，下游才是 MySQL / Redis。它们不反向调用 `Session`，也不持有网络层对象。

### 3. 整体运行链路

以用户登录为例：

1. `Session` 解出登录请求 Packet。
2. 网络层把登录任务投递到 business `ThreadPool`。
3. AuthService 调用 `IStorage::findUserByUsername()` 查询用户。
4. AuthService 做密码校验。
5. 登录成功后调用 `ICache::setUserOnline()` 写在线状态。
6. 业务线程不能直接写 socket，而是投递回 `Session` owner loop。
7. `Session::sendPacket()` 在 I/O loop 内发送登录响应。

### 4. 自身内部运行流程

接口自身没有内部状态，但它定义了两个关键流程约束：

- 数据输入用 DTO 表达，例如 `CreateUserRequest`、`MessageRecord`、`OnlineSession`。
- 结果输出用引用参数表达，例如 `UserRecord&`、`std::vector<MessageRecord>&`、`bool&`。

实现类必须保证：

1. 调用开始时先校验明显非法参数。
2. 获取 MySQL / Redis 连接失败时直接返回失败 `Status`。
3. 数据不存在时返回 `NotFound` 或空列表，不能返回未初始化输出参数。
4. 业务唯一约束冲突转换成明确错误，例如用户名重复是 `AlreadyExists`。
5. 阻塞 I/O 不在 Reactor I/O 线程执行。

### 5. 该项目代码在实际应用中的具体数据例子

ChatService 未来处理 Alice 发给离线 Bob 的私聊时，会构造 `MessageRecord{conversation_type=1, conversation_id=10011002, sender_id=1001, receiver_id=1002, text=hello bob}`，调用 `saveMessageWithOfflineRecipients(message, {1002}, saved_message)`。MySQL 实现成功后返回 `saved_message.message_id=10000` 这类真实自增 id；随后业务层再用 cache 更新 `unread:user:1002:conversation:1:10011002`，在线状态则读写 `online:user:1002`。如果离线用户列表传入 `{1002, 1002}`，MySQL 实现会先去重，避免重复 pending 离线记录。

## 后续实现 / 关键设计说明

本 Step 故意不把所有接口一次性实现成一个巨大类。后续实际代码会先拆成更小的 MySQL DAO 和 Redis cache 组件：

- Step 23-24 先实现 MySQL connection 和 pool。
- Step 25-27 实现用户、消息、好友、群组 DAO。
- Step 28-30 实现 Redis client、在线状态、未读计数和登录限制。
- Pre-Step31 小重构补 `MySqlStorage : IStorage` 和 `RedisCache : ICache` 两个聚合适配层，让业务层可以注入统一接口，同时保留 DAO/cache 组件的可测试边界。

这让每个 Step 都有明确边界，也便于测试单个 DAO/cache。

## 测试设计

本 Step 的测试不依赖 MySQL 或 Redis。

测试重点：

- `StorageTypes.hpp` / `IStorage.hpp` 头文件可以独立 include。
- `CacheTypes.hpp` / `ICache.hpp` 头文件可以独立 include。
- `FakeStorage` 可以实现 `IStorage`，证明接口可替换。
- `NullCache` 可以实现 `ICache`，证明后续业务测试可以用轻量测试替身。
- Pre-Step31 后新增 `MySqlStorage` / `RedisCache` 集成测试，证明真实 MySQL / Redis 适配层可以通过统一接口使用。
- CMake target 能被测试目标链接。

## 验证命令

```bash
cmake --build build
ctest --test-dir build -R "StorageInterfaceTest|CacheInterfaceTest" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 面试时怎么讲

可以这样说：

> Step 21 先把 IM 业务需要的持久化和缓存能力抽象出来。持久化接口覆盖用户、好友公开资料、群组、消息、离线消息和历史查询，缓存接口覆盖在线状态、未读计数和登录失败限制。接口统一返回 `Status`，结果用输出参数，不暴露 MySQL / Redis 具体类型。Pre-Step31 小重构补上真实 `MySqlStorage` / `RedisCache` 聚合适配层，让后续业务 service 可以依赖稳定边界，真实阻塞 I/O 放到 business 线程池执行，网络 I/O 线程只负责 Reactor 和连接读写。

## 面试常见追问

### Q1：为什么先抽 IStorage / ICache？

业务 service 需要依赖稳定抽象，而不是直接依赖 MySQL C API 或 hiredis。这样后续可以用真实 MySQL/Redis 实现，也可以在 service 单测里注入 fake。

### Q2：为什么接口不负责线程切换？

存储和缓存接口表达的是阻塞资源访问契约。是否投递到 business ThreadPool 是调用方的运行时责任，接口层不隐藏线程模型。
