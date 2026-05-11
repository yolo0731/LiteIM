# Step 29：OnlineStatusCache

## 1. 概念

Step 29 把 Redis 从“能执行命令的基础设施”推进到第一个真实 cache 能力：在线状态。

在线状态不是最终持久化数据，它表示某个用户当前是否在某个 LiteIM server 和 session 上在线。它适合放 Redis，而不是 MySQL，原因是：

- 在线状态会频繁刷新，心跳一次就可能续 TTL。
- 断线清理可能失败，但 TTL 到期后可以自动兜底。
- 后续好友列表、会话列表和多进程扩展都可以读 Redis 判断在线状态。

本 Step 的 Redis key 固定为：

```text
online:user:<user_id>
```

value 保存 `user_id`、`session_id`、`server_id` 和 `last_active_time_ms`。`SETEX` 写入 key 和 TTL，`EXPIRE` 刷新 TTL，`DEL` 下线删除，`GET` 查询当前在线 session。

本 Step 不做未读计数、登录失败限制、AuthService、SessionManager、OnlineService，也不把 Redis 接进 `TcpServer` / `Session` 运行时。Redis 仍然是阻塞 API，后续只能由 business `ThreadPool` 调用，不能在 Reactor I/O 线程里直接调用。

## 2. hpp 接口说明

新增头文件：

```text
include/liteim/cache/OnlineStatusCache.hpp
```

核心接口：

```cpp
class OnlineStatusCache {
public:
    explicit OnlineStatusCache(RedisPool& pool,
                               std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds{200});

    Status setUserOnline(std::uint64_t user_id,
                         const std::string& server_id,
                         std::uint64_t session_id,
                         std::chrono::seconds ttl);
    Status setUserOnline(const OnlineSession& session, std::chrono::seconds ttl);
    Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl);
    Status setUserOffline(std::uint64_t user_id);
    Status isUserOnline(std::uint64_t user_id, bool& online);
    Status getOnlineSession(std::uint64_t user_id, OnlineSession& session);
};
```

`OnlineStatusCache` 持有外部传入的 `RedisPool&`，它不拥有连接池。业务层需要保证 `RedisPool` 已经 `start()`，并且生命周期长于 `OnlineStatusCache`。

接口语义：

- `setUserOnline(user_id, server_id, session_id, ttl)`：登录成功后写入在线状态；`last_active_time_ms` 自动取当前时间。
- `setUserOnline(OnlineSession, ttl)`：允许调用方传入完整 session；如果 `last_active_time_ms <= 0`，实现会自动补当前时间。
- `refreshUserOnline(user_id, ttl)`：心跳成功后刷新已有 key 的 TTL；如果 key 不存在，返回 `NotFound`。
- `setUserOffline(user_id)`：断开连接或登出时删除在线 key；key 不存在也视为成功。
- `isUserOnline(user_id, online)`：查询 key 是否存在且 value 可解析。
- `getOnlineSession(user_id, session)`：读取在线 session；key 不存在返回 `NotFound`。

失败语义：

- `user_id == 0`、`session_id == 0`、空 `server_id` 或非正 TTL 返回 `InvalidArgument`。
- Redis 连接池借不到连接、命令失败或连接错误返回底层 `Status`。
- 在线 key 不存在时，`getOnlineSession()` 和 `refreshUserOnline()` 返回 `NotFound`。
- Redis value 无法解析时返回 `ParseError`，说明缓存内容损坏或版本不匹配。

## 3. 作用场景和运行流程

### 使用场景

后续 Step 31 登录成功后会做两件事：

```text
SessionManager 绑定 user_id -> session_id
OnlineStatusCache 写 online:user:<user_id> 并设置 TTL
```

心跳成功时刷新 TTL：

```text
收到完整合法 heartbeat Packet
  -> business ThreadPool
  -> OnlineStatusCache::refreshUserOnline(user_id, ttl)
```

断开连接或登出时删除 key：

```text
Session close
  -> SessionManager 解除绑定
  -> OnlineStatusCache::setUserOffline(user_id)
```

### 内部流程

上线写入：

```text
setUserOnline()
  -> 校验 user_id / session_id / server_id / ttl
  -> RedisPool::acquire()
  -> 序列化 OnlineSession
  -> RedisClient::setex("online:user:<id>", value, ttl)
```

TTL 刷新：

```text
refreshUserOnline()
  -> 校验 user_id / ttl
  -> RedisPool::acquire()
  -> RedisClient::expire(key, ttl, updated)
  -> updated=false 表示 key 已不存在，返回 NotFound
```

读取 session：

```text
getOnlineSession()
  -> RedisPool::acquire()
  -> RedisClient::get(key)
  -> NIL 返回 NotFound
  -> 解析 value 到 OnlineSession
```

### 一个小例子

用户 `1001` 在 `liteim-dev-1` 的 session `42` 登录：

```text
SETEX online:user:1001 30 "v1:1001:42:...:12:liteim-dev-1"
```

30 秒内持续收到心跳：

```text
EXPIRE online:user:1001 30
```

如果进程崩溃导致没有执行 `DEL`，这个 key 最多保留 30 秒，然后 Redis 自动让用户变为离线。

## 4. 测试

新增测试文件：

```text
tests/cache/online_status_cache_test.cpp
```

覆盖：

- `OnlineStatusCacheTest.HeaderIsSelfContained`：头文件可独立使用，非法 user id 在未连接 Redis 前就返回 `InvalidArgument`。
- `OnlineStatusCacheIntegrationTest.SetUserOnlineMakesUserQueryable`：上线后 `isUserOnline()` 为 true，`getOnlineSession()` 能读回 user、session、server 和活跃时间。
- `OnlineStatusCacheIntegrationTest.ServerIdMayContainColon`：`server_id` 中包含冒号时仍能按长度字段正确解析。
- `OnlineStatusCacheIntegrationTest.RefreshUserOnlineExtendsTtl`：刷新 TTL 后，超过原 TTL 仍然在线。
- `OnlineStatusCacheIntegrationTest.SetUserOfflineRemovesOnlineSession`：下线删除 key，之后查询为离线，读取 session 返回 `NotFound`。
- `OnlineStatusCacheIntegrationTest.TtlExpiryMakesUserOffline`：TTL 到期后自动离线。
- `OnlineStatusCacheIntegrationTest.RefreshMissingUserReturnsNotFound`：刷新不存在用户返回 `NotFound`。

Redis 集成测试继续使用 `Config::defaults().redis`，也就是 Step 22 Docker Redis：`127.0.0.1:63790`，密码 `6`。Redis 不可用时测试 skip，避免无 Docker 环境下误报失败。

## 5. 验证命令

```bash
docker compose -f docker/docker-compose.yml up -d --wait
cmake --build build
ctest --test-dir build -R "OnlineStatusCache" --output-on-failure
ctest --test-dir build --output-on-failure
```

本 Step 只完成在线状态 cache，不实现 Step 30 的未读计数和登录限流，也不进入 Step 31 的 `SessionManager` / `OnlineService`。
