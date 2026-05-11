# Step 22：Docker Compose 和 MySQL 初始化 SQL

## 1. 概念

Step 21 已经定义了 `IStorage` / `ICache` 接口，但还没有真实 MySQL / Redis 环境。Step 22 先把本地开发依赖固定下来，让后续 Step 23-30 可以直接围绕真实 MySQL 连接、连接池、DAO 和 Redis 缓存实现。

本 Step 只做基础设施：

- Docker Compose 启动 MySQL 和 Redis。
- MySQL 初始化 LiteIM 需要的主线表。
- seed 脚本写入本地测试数据。
- Redis 只启动服务并开启本地开发密码，不写初始化数据。

本 Step 不实现 C++ MySQL client、Redis client、DAO、业务服务，也不让网络 I/O 线程接触阻塞存储调用。

## 2. 文件说明

### `docker/docker-compose.yml`

该文件定义两个本地开发服务：

- `mysql`：MySQL 8.0 系列，默认库名 `liteim`，本机端口 `33060`。
- `redis`：Redis 7.2，本机端口 `63790`。

MySQL / Redis 的默认本地开发密码统一为 `6`。MySQL root 密码也是 `6`。`33060` 是宿主机端口，映射到容器内经典 MySQL 协议端口 `3306`，不是 MySQL X Protocol。

MySQL 容器第一次创建数据卷时，会按文件名顺序执行：

```text
/docker-entrypoint-initdb.d/01-init_mysql.sql
/docker-entrypoint-initdb.d/02-seed_test_data.sql
```

端口和密码只用于本地开发，可以通过环境变量覆盖：

```bash
LITEIM_MYSQL_PORT=33306 LITEIM_REDIS_PORT=36379 \
LITEIM_MYSQL_PASSWORD=6 LITEIM_MYSQL_ROOT_PASSWORD=6 LITEIM_REDIS_PASSWORD=6 \
docker compose -f docker/docker-compose.yml up -d
```

### `scripts/init_mysql.sql`

初始化脚本创建 `liteim` 数据库和六类主线表：

- `users`
- `friendships`
- `chat_groups`
- `group_members`
- `messages`
- `offline_messages`

字段以 Step 21 的 DTO 为准：

- 用户、群组、消息、离线消息使用 `BIGINT UNSIGNED` 主键。
- `created_at_ms` / `updated_at_ms` 用毫秒时间戳，便于映射到 C++ 的 `std::int64_t`。
- 私聊和群聊消息通过 `conversation_type` + `conversation_id` 建索引，对应 `ConversationKey`。
- 离线消息通过 `(user_id, delivered, offline_message_id)` 支撑“查询某用户未投递消息”。

### `scripts/seed_test_data.sql`

seed 脚本创建三类本地数据：

- 测试用户：`alice`、`bob`。
- Bot 用户：`mira_bot`。
- 测试群：`dev_group`。

脚本使用固定 id，便于后续集成测试写稳定断言。它也使用 `ON DUPLICATE KEY UPDATE`，重复执行不会产生重复数据。

## 3. 运行流程

启动本地依赖：

```bash
docker compose -f docker/docker-compose.yml up -d
```

MySQL 第一次启动时自动完成：

```text
创建 liteim 数据库
  -> 创建 users/friendships/chat_groups/group_members/messages/offline_messages
  -> 写入 alice/bob/mira_bot/dev_group 测试数据
```

Redis 启动后只提供带密码的空实例。后续 `RedisPool` 和 `ICache` 实现会负责在线状态、未读计数和登录失败限制 key。

## 4. 验证命令

查看服务状态：

```bash
docker compose -f docker/docker-compose.yml ps
```

查询 MySQL 表：

```bash
docker compose -f docker/docker-compose.yml exec mysql \
  mysql -uliteim -p6 liteim \
  -e "SHOW TABLES;"
```

验证关键索引和 seed 数据：

```bash
docker compose -f docker/docker-compose.yml exec mysql \
  mysql -uliteim -p6 liteim \
  -e "SHOW INDEX FROM messages; SELECT user_id, username FROM users ORDER BY user_id;"
```

验证 Redis 认证：

```bash
docker compose -f docker/docker-compose.yml exec redis \
  sh -c 'REDISCLI_AUTH=6 redis-cli ping'
```

如果本机之前已经用 `mysql:8.4` 创建过 LiteIM 开发数据卷，切换到 MySQL 8.0 时需要重建这个开发数据卷，避免把 8.4 数据目录直接降级给 8.0 使用：

```bash
docker compose -f docker/docker-compose.yml down -v
docker compose -f docker/docker-compose.yml up -d --wait
```

清理本地开发容器：

```bash
docker compose -f docker/docker-compose.yml down
```

如果需要重新执行初始化脚本，需要删除本地数据卷：

```bash
docker compose -f docker/docker-compose.yml down -v
```

## 5. 边界

- Docker 环境只用于本地开发和后续集成测试。
- MySQL 是消息、用户、好友、群组和离线消息的最终数据来源。
- Redis 是在线状态、未读数和登录失败限制缓存，不保存最终消息实体。
- SQL schema 不改变当前 C++ 网络层，不接入 `TcpServer` / `ThreadPool`。
- 后续 Step 23 才开始封装 MySQL C API。
