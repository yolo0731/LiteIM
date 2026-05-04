# LiteIM 数据库说明

本文档用于记录 LiteIM 的 SQLite 表结构和存储层设计。

Step 14 已经先定义 `IStorage` / `ICache` 抽象，后续 Step 15 实现 `SQLiteStorage` 时会补充完整建表语句和字段说明。

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
