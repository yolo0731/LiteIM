# LiteIM 数据库说明

本文档用于记录 LiteIM 的 SQLite 表结构和存储层设计。

后续 Step 15 实现 `SQLiteStorage` 时会补充完整建表语句和字段说明。

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
