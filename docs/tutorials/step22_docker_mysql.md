# Step 22：Docker Compose 和 MySQL 初始化 SQL

## 0. 本 Step 结论

- 目标：Step 22 的目标是把本地 MySQL / Redis 开发环境固定下来，并创建 LiteIM 后续 DAO 会使用的 MySQL schema。
- 前置依赖：依赖 Step 0-21 已建立的工程、协议或运行时基础。
- 主要交付：`Docker Compose 和 MySQL 初始化 SQL` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

Step 22 的目标是把本地 MySQL / Redis 开发环境固定下来，并创建 LiteIM 后续 DAO 会使用的 MySQL schema。

到 Step 21 为止，LiteIM 已经有 `IStorage` 和 `ICache` 接口，但还没有真实数据库。Step 22 解决的问题是：

```text
后续 MySQL / Redis 代码应该连到哪里？表结构、索引和测试数据从哪里来？
```

答案是用 Docker Compose 启动本地 MySQL 8.0 和 Redis 7.2，并用 SQL 脚本初始化数据库。

### 概念

这一 Step 不是 C++ 功能实现，而是本地依赖契约。

它固定了三件事：

- 服务契约：MySQL 和 Redis 的镜像、端口、密码、健康检查。
- 数据契约：users、friendships、chat_groups、group_members、messages、offline_messages 表结构。
- 测试契约：固定 seed 用户、群、消息和离线消息，后续 DAO 集成测试可以重复使用。

当前开发环境：

```text
MySQL: 127.0.0.1:33060
Redis: 127.0.0.1:63790
user/password: liteim / 6
database: liteim
```

Redis 第一版只启动空实例并开启密码认证，不提前写业务 key。在线状态、未读计数和登录失败限制留给 Step 29 / Step 30。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `Docker Compose 和 MySQL 初始化 SQL` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `docker/docker-compose.yml` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `scripts/init_mysql.sql` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `scripts/seed_test_data.sql` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/tutorials/step22_docker_mysql.md` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `include/liteim/base/Config.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/base/Config.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

Step 22 没有新增 C++ `.hpp`。这里的“接口说明”对应 Compose 和 SQL 脚本对后续 C++ 代码提供的运行契约。

### `docker/docker-compose.yml`

`docker-compose.yml` 定义两个服务：`mysql` 和 `redis`。

MySQL 契约：

- 镜像固定为 `mysql:8.0`，避免 MySQL Workbench 8.0 对 8.4 服务端提示兼容性警告。
- 容器内端口是 MySQL 经典协议 `3306`。
- 宿主机默认映射到 `127.0.0.1:33060`。
- 默认数据库是 `liteim`。
- 默认用户是 `liteim`。
- root 和 liteim 用户默认密码都是 `6`。
- 字符集固定为 `utf8mb4`，排序规则为 `utf8mb4_0900_ai_ci`。
- 默认时区为 `+08:00`。
- 首次创建数据卷时按顺序执行 `01-init_mysql.sql` 和 `02-seed_test_data.sql`。
- healthcheck 使用 `mysqladmin ping` 确认 MySQL 已可连接。

Redis 契约：

- 镜像固定为 `redis:7.2-alpine`。
- 容器内端口是 `6379`。
- 宿主机默认映射到 `127.0.0.1:63790`。
- 默认密码是 `6`。
- 开启 appendonly。
- healthcheck 使用带认证的 `redis-cli ping`。
- 不初始化业务数据。

环境变量可以覆盖默认值：

```text
LITEIM_MYSQL_ROOT_PASSWORD
LITEIM_MYSQL_DATABASE
LITEIM_MYSQL_USER
LITEIM_MYSQL_PASSWORD
LITEIM_MYSQL_PORT
LITEIM_REDIS_PASSWORD
LITEIM_REDIS_PORT
```

### `scripts/init_mysql.sql`

`init_mysql.sql` 是 schema 来源。它负责创建数据库和 6 张主表。

`users`：

- `user_id BIGINT UNSIGNED AUTO_INCREMENT` 是主键。
- `username` 有唯一索引 `uk_users_username`。
- 保存 `password_hash`、`password_salt`、`nickname`、`created_at_ms`、`updated_at_ms`。
- `idx_users_created_at_ms` 支撑按创建时间观察或后续管理查询。

`friendships`：

- 主键是 `(user_id, friend_id)`。
- `idx_friendships_friend_id` 支撑反向查询。
- 两个外键都指向 `users`。
- CHECK 约束保证 `user_id <> friend_id`。
- 第一版好友关系由 DAO 写两行表达双向关系。

`chat_groups`：

- `group_id BIGINT UNSIGNED AUTO_INCREMENT` 是主键。
- `owner_id` 外键指向 `users`，删除用户时限制删除群主。
- `idx_chat_groups_owner_id` 支撑按 owner 查询。

`group_members`：

- 主键是 `(group_id, user_id)`。
- `idx_group_members_user_id` 支撑按用户查群。
- 外键分别指向 `chat_groups` 和 `users`。

`messages`：

- `message_id BIGINT UNSIGNED AUTO_INCREMENT` 是主键。
- `conversation_type` 只允许 `1` 或 `2`。
- `conversation_id` 保存私聊 conversation id 或群 id。
- `sender_id` 外键指向 `users`。
- `receiver_id` 私聊是接收者，群聊第一版保存群 id。
- `idx_messages_history(conversation_type, conversation_id, message_id)` 支撑历史消息游标分页。
- `idx_messages_sender` / `idx_messages_receiver` 支撑后续按用户侧查询。

`offline_messages`：

- `offline_message_id BIGINT UNSIGNED AUTO_INCREMENT` 是主键。
- `user_id` 表示待投递给谁。
- `message_id` 指向 `messages`。
- `delivered` 表示是否已投递。
- `uk_offline_messages_user_message` 防止同一用户同一消息重复入离线队列。
- `idx_offline_messages_user_pending(user_id, delivered, offline_message_id)` 支撑“拉取某用户未投递离线消息”。

所有时间字段用毫秒时间戳，和 LiteIM `Timestamp` / DTO 字段一致。

### `scripts/seed_test_data.sql`

`seed_test_data.sql` 是开发和集成测试的基础数据。

固定用户：

```text
1001 alice
1002 bob
```

固定群：

```text
2001 dev_group
```

固定消息：

```text
5001 alice -> bob
5002 alice -> dev_group
```

固定离线消息：

```text
user 1002 pending message 5001
```

脚本使用 `ON DUPLICATE KEY UPDATE`，可以重复执行。最后把 users、chat_groups、messages、offline_messages 的自增起点调到 `10000`，避免后续测试自动创建的数据和 seed id 冲突。

### Redis 空实例边界

Redis 在 Step 22 只保证服务可连接和认证可用。它不创建：

- `online:user:<id>`。
- 未读计数 key。
- 登录失败限制 key。

原因是 Redis key 的格式要等 `RedisClient`、`OnlineStatusCache`、`UnreadCounter`、`LoginRateLimiter` 实现时再固定。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

Step 22 是后续存储/cache 层的本地运行底座。

它的使用者不是网络层，而是后续这些模块：

- `MySqlConnection` 连接 `127.0.0.1:33060`。
- `MySqlPool` 维护 MySQL 连接池。
- `UserDao` / `MessageDao` / `FriendDao` / `GroupDao` 依赖表结构和索引。
- `RedisClient` 连接 `127.0.0.1:63790`。
- `OnlineStatusCache` / Step 30 cache 组件依赖 Redis 服务。

### 2. 上下层调用连接

```text
docker compose
    -> mysql:8.0 + init SQL + seed SQL
    -> redis:7.2 empty authenticated instance
    -> Config::defaults()
    -> MySqlConnection / RedisClient
    -> Pool / DAO / Cache
    -> 后续 AuthService / ChatService
```

Compose 是开发环境的下层依赖；C++ 代码只通过 host、port、password 和 schema 使用它，不依赖容器名。

### 3. 整体运行链路

第一次启动时：

1. `docker compose -f docker/docker-compose.yml up -d` 创建 MySQL 和 Redis 容器。
2. MySQL 初始化空数据卷。
3. MySQL entrypoint 执行 `01-init_mysql.sql` 创建库表和索引。
4. MySQL entrypoint 执行 `02-seed_test_data.sql` 写 seed 数据。
5. Redis 启动空实例并开启密码。
6. healthcheck 等待两个服务进入 healthy。
7. C++ 集成测试按 `Config::defaults()` 连接本地端口。

后续重复启动时：

1. 已存在的数据卷不会重新执行 entrypoint SQL。
2. 需要重建干净数据时执行 `docker compose down -v` 删除数据卷。
3. 再次 `up -d` 会重新执行初始化脚本。

### 4. 自身内部运行流程

Compose 自身做服务编排：

- 读取环境变量或使用默认值。
- 创建 volume 保存 MySQL / Redis 数据。
- 启动 MySQL 并挂载 SQL 脚本到 `/docker-entrypoint-initdb.d/`。
- 启动 Redis 并把 `requirepass` 写入启动命令。
- 周期执行 healthcheck。

SQL 脚本自身按依赖顺序执行：

1. 先创建 `users`，因为好友、群、消息都依赖用户。
2. 再创建 `friendships`。
3. 再创建 `chat_groups` 和 `group_members`。
4. 再创建 `messages`。
5. 最后创建依赖 `messages` 的 `offline_messages`。
6. seed 先插用户，再插好友、群、成员、消息、离线消息。

### 5. 该项目代码在实际应用中的具体数据例子

初始化脚本会写入真实开发数据：`users` 里有 `user_id=1001` 的 `alice`、`user_id=1002` 的 `bob`；`messages` 里有 `message_id=5001`、`conversation_type=1`、`conversation_id=10011002`、文本 `hello bob`。历史分页依赖 `idx_messages_history(conversation_type, conversation_id, message_id)`，离线拉取依赖 `offline_messages(user_id=1002, message_id=5001, delivered=0)`。

## 6. 关键实现点

- 保持模块职责单一。
- 失败时返回清晰错误，不吞掉异常状态。
- 不跨越本 Step 边界提前实现后续业务。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `Docker Compose 和 MySQL 初始化 SQL` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

本 Step 的验证重点是外部依赖能真实启动，而不是 mock：

- Compose 能启动 MySQL / Redis。
- 两个服务 healthcheck 变为 healthy。
- MySQL schema 创建成功。
- seed 用户、群、消息、离线消息可查询。
- Redis 认证后 `PING` 成功。
- `Config::defaults()` 的端口、用户、密码和 Compose 默认一致。

## 8. 验证命令

```bash
docker compose -f docker/docker-compose.yml up -d
docker compose -f docker/docker-compose.yml ps
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
docker compose -f docker/docker-compose.yml down -v
```

## 9. 面试表达

### 一句话

Step 22 先把本地 MySQL / Redis 环境和 schema 固定下来。

### 展开说

可以这样说：

> Step 22 先把本地 MySQL / Redis 环境和 schema 固定下来。MySQL 用 Docker Compose 启动 8.0 系列，宿主机端口是 33060，Redis 端口是 63790，两者默认密码都是 6。MySQL 初始化脚本创建用户、好友、群、消息和离线消息表，并准备索引和 seed 数据；Redis 第一版只启动空实例和认证。这样后续 C++ MySQL wrapper、DAO 和 Redis cache 都有稳定的真实依赖可以集成测试。

### 容易被追问

- 为什么 MySQL 是主线依赖？
- 为什么 seed 只放普通开发用户？

## 10. 面试常见追问

### Q1：为什么 MySQL 是主线依赖？

LiteIM 的消息、用户、好友、群组和离线消息都需要可恢复的持久化来源。Redis 只保存在线状态、未读数和登录失败窗口，不能替代 MySQL。

### Q2：为什么 seed 只放普通开发用户？

LiteIM 不在 C++ 服务端区分 AI/Bot 身份，也不依赖固定 bot seed 用户。后续 PersonaAgent 需要账号时，由 Python BotClient 使用普通注册/登录流程或配置好的普通账号接入。
