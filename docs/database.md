# LiteIM 数据库说明

本文档用于记录 LiteIM 的 SQLite 表结构和存储层设计。

Step 14 已经先定义 `IStorage` / `ICache` 抽象，Step 15 已经实现 `SQLiteStorage` 并补充 `sql/init.sql`。

计划表：

- `users`：用户表，保存用户名、昵称、密码 salt、密码 hash、用户类型。
- `friendships`：好友关系表。
- `groups`：群组表。
- `group_members`：群成员表。
- `messages`：消息表，保存私聊和群聊消息。

设计原则：

- 业务层依赖 `IStorage` 接口，不直接依赖 SQLite。
- 单元测试可以使用 `InMemoryStorage` 替代 SQLite。
- SQLite 是第一版单机持久化方案，不引入 Redis、MySQL 或分布式存储。

## Step 14：存储和缓存接口

Step 14 新增：

- `include/liteim/storage/StorageTypes.hpp`
- `include/liteim/storage/IStorage.hpp`
- `include/liteim/storage/ICache.hpp`
- `include/liteim/storage/NullCache.hpp`
- `src/storage/NullCache.cpp`

`StorageTypes.hpp` 定义业务层和存储层共享的数据结构：

- `User` / `CreateUserRequest`
- `Group` / `CreateGroupRequest`
- `PrivateMessage` / `SavePrivateMessageRequest`
- `GroupMessage` / `SaveGroupMessageRequest`
- `UserId`、`GroupId`、`MessageId`、`UnixTimestamp`

`IStorage` 是业务层依赖的持久化接口。它覆盖：

- 用户创建和查询。
- 好友关系。
- 群组创建、成员增删、成员查询。
- 私聊消息保存和历史查询。
- 群聊消息保存和历史查询。
- 离线消息查询。

`ICache` 是在线状态缓存边界。当前只定义：

- `setOnline()`
- `setOffline()`
- `findOnlineSession()`
- `clear()`

`NullCache` 是第一版 no-op 实现：所有写入方法什么都不做，`findOnlineSession()` 永远返回空。它的作用是让单机版可以先不引入真实缓存，同时让业务层代码从一开始就依赖 `ICache` 抽象。

Step 14 不实现 SQLite，也不执行 `sql/init.sql`。真实数据库读写、建表语句和错误处理放到 Step 15。

## Step 15：SQLiteStorage

Step 15 新增：

- `include/liteim/storage/SQLiteStorage.hpp`
- `src/storage/SQLiteStorage.cpp`
- `tests/test_sqlite_storage.cpp`
- 完整 `sql/init.sql`

`SQLiteStorage` 继承 `IStorage`。构造时会：

1. 打开指定 SQLite 数据库文件，默认是 `liteim.db`。
2. 执行 `PRAGMA foreign_keys = ON`。
3. 读取并执行 `sql/init.sql`。

测试中可以传入 `:memory:` 作为数据库路径，也可以传入 `/tmp/...db` 这样的临时文件路径。

### users

`users` 保存用户基本信息：

- `id`：自增主键。
- `username`：唯一用户名。
- `nickname`：昵称。
- `password_salt`：密码 salt。
- `password_hash`：密码 hash。
- `user_type`：用户类型，当前对应 `UserType::Human` / `UserType::Bot`。
- `created_at`：创建时间戳。

`SQLiteStorage::createUser()` 写入这张表，`findUserByUsername()` 和 `findUserById()` 从这张表查询。

### friendships

`friendships` 保存好友关系：

- `user_id`
- `friend_id`
- `created_at`

主键是 `(user_id, friend_id)`。`SQLiteStorage::addFriendship()` 当前会写入双向关系：

```text
alice -> bob
bob   -> alice
```

这样 `getFriends(alice)` 和 `getFriends(bob)` 都能查到对方。

### groups

`groups` 保存群组：

- `id`
- `name`
- `owner_id`
- `created_at`

`SQLiteStorage::createGroup()` 创建群组后，会同时把群主写入 `group_members`。

### group_members

`group_members` 保存群成员关系：

- `group_id`
- `user_id`
- `joined_at`

主键是 `(group_id, user_id)`。`addGroupMember()` 使用 `INSERT OR IGNORE`，重复添加不会产生重复成员。

### messages

`messages` 同时保存私聊和群聊消息：

- `message_type = 0`：私聊消息。
- `message_type = 1`：群聊消息。
- 私聊消息使用 `receiver_id`，`group_id` 为空。
- 群聊消息使用 `group_id`，`receiver_id` 为空。

`CHECK` 约束保证私聊/群聊字段组合不会混乱。

`delivered` 当前用于私聊离线消息查询。`getOfflineMessages()` 查询 `receiver_id = user_id` 且 `delivered = 0` 的私聊消息。

### 设计边界

Step 15 仍然不做注册、登录、私聊转发或群聊转发。它只负责把 Step 14 的 `IStorage` 抽象落到 SQLite 上。

后续 Step 16 / Step 17 / Step 18 的业务层会依赖 `IStorage`，而不是直接依赖 SQLite C API。
