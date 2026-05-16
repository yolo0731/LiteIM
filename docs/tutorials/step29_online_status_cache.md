# Step 29：OnlineStatusCache

## 0. 本 Step 结论

- 目标：Step 29 的目标是在 RedisPool 之上实现在线状态缓存。
- 前置依赖：依赖 Step 0-28 已建立的工程、协议或运行时基础。
- 主要交付：`OnlineStatusCache` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

Step 29 的目标是在 RedisPool 之上实现在线状态缓存。

到 Step 28 为止，LiteIM 已经能安全执行 Redis 命令，但业务层还不应该手写 Redis key 和 value 格式。Step 29 解决的问题是：

```text
登录、心跳、断开连接和消息路由如何通过统一组件读写用户在线状态？
```

答案是 `OnlineStatusCache`。

### 概念

在线状态不是永久数据，适合 Redis TTL。

LiteIM 第一版在线状态 key：

```text
online:user:<user_id>
```

value 保存：

```text
user_id
session_id
last_active_time_ms
server_id
```

TTL 表示在线状态有效期：

- 登录成功：写 key + TTL。
- 收到心跳或完整有效业务包：刷新 TTL。
- 连接断开：删除 key。
- 异常断开或删除失败：TTL 到期兜底自动离线。

Step 29 只实现 cache 组件，不把它接入登录流程、心跳 runtime 或 `Session` 关闭流程。Step 32 的 `OnlineService` 开始组合它和当前进程内的 session 绑定。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `OnlineStatusCache` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/cache/OnlineStatusCache.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/cache/OnlineStatusCache.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/cache/online_status_cache_test.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/tutorials/step29_online_status_cache.md` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/cache/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

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

private:
    RedisPool& pool_;
    std::chrono::milliseconds acquire_timeout_;
};
```

### 构造函数

`OnlineStatusCache(RedisPool& pool, acquire_timeout)` 保存 Redis 连接池引用和 acquire 超时时间。

所有权边界：

- `OnlineStatusCache` 不拥有 `RedisPool`。
- 调用方必须保证 `RedisPool` 生命周期长于 cache。
- 每个方法内部临时 acquire 一个 Redis client，方法结束后 guard 自动归还。

线程边界：

- 方法会阻塞等待 Redis 连接和命令结果。
- 只能在 business 线程使用。
- 不允许在 Reactor I/O loop 直接调用。

关键成员：

- `RedisPool& pool_`：Redis 连接池。
- `std::chrono::milliseconds acquire_timeout_`：默认 200ms，避免业务线程永久等连接。

### `setUserOnline(user_id, server_id, session_id, ttl)`

```cpp
Status setUserOnline(std::uint64_t user_id,
                     const std::string& server_id,
                     std::uint64_t session_id,
                     std::chrono::seconds ttl);
```

便捷重载，用基础字段构造 `OnlineSession`。

语义：

- 自动填充 `last_active_time_ms = Timestamp::now().millisecondsSinceEpoch()`。
- 再调用 `setUserOnline(const OnlineSession&, ttl)`。

输入要求：

- `user_id > 0`。
- `session_id > 0`。
- `server_id` 非空。
- `ttl > 0`。

### `setUserOnline(const OnlineSession&, ttl)`

```cpp
Status setUserOnline(const OnlineSession& session, std::chrono::seconds ttl);
```

写入 Redis 在线状态。

流程：

1. 如果 `last_active_time_ms <= 0`，自动补当前时间。
2. 校验 session。
3. 校验 ttl。
4. 从 RedisPool acquire client。
5. 序列化 session。
6. 执行 `SETEX online:user:<user_id> <ttl> <value>`。

Redis key：

```text
online:user:<user_id>
```

Redis value：

```text
v1:<user_id>:<session_id>:<last_active_time_ms>:<server_id_size>:<server_id>
```

value 里保存 `server_id_size`，所以 `server_id` 可以包含冒号，不会被错误切分。

### `refreshUserOnline()`

```cpp
Status refreshUserOnline(std::uint64_t user_id, std::chrono::seconds ttl);
```

刷新用户在线状态 TTL。

流程：

1. 校验 `user_id` 和 ttl。
2. acquire Redis client。
3. 执行 `EXPIRE online:user:<user_id> <ttl>`。
4. 如果 Redis 返回 0，说明 key 不存在，返回 `NotFound`。

这个函数只刷新 TTL，不重写 value。`last_active_time_ms` 是写在线时的记录，后续如果业务需要每次心跳同步更新时间，可以再扩展。

### `setUserOffline()`

```cpp
Status setUserOffline(std::uint64_t user_id);
```

删除在线状态 key。

语义：

- key 存在：删除，返回 ok。
- key 不存在：`DEL` 返回 0，但仍返回 ok。

原因：

断开连接清理应该幂等。即使删除失败或 key 已过期，Redis TTL 也会兜底最终离线。

### `isUserOnline()`

```cpp
Status isUserOnline(std::uint64_t user_id, bool& online);
```

查询用户是否在线。

语义：

- 调用开始先 `online = false`。
- key 不存在：返回 ok，保持 `online=false`。
- key 存在：解析 value。
- value 解析成功：`online=true`。
- value 损坏：返回 `ParseError`，不静默当成在线或离线。

为什么损坏 value 返回错误：

在线状态 value 是服务端内部格式。如果 Redis 里已有损坏数据，继续当成离线会掩盖数据污染，继续当成在线会错误路由消息。

### `getOnlineSession()`

```cpp
Status getOnlineSession(std::uint64_t user_id, OnlineSession& session);
```

读取完整在线 session。

语义：

- 调用开始先清空 `session`。
- key 不存在返回 `NotFound`。
- key 存在但格式损坏返回 `ParseError`。
- 成功时填充完整 `OnlineSession`。

后续消息路由会用它找到目标用户当前 `server_id` 和 `session_id`。

### private helper

`OnlineStatusCache.cpp` 内部 helper 包括：

- `onlineKey(user_id)`：生成 `online:user:<id>`。
- `validateUserId()` / `validateTtl()` / `validateSession()`：参数校验。
- `acquireClient()`：从 RedisPool 获取 client，并检查 guard 非空。
- `serializeSession()`：把 `OnlineSession` 编成 `v1:` value。
- `parseSessionValue()`：解析 Redis value。
- `readToken()`：按冒号读取字段。
- `parseUint64()` / `parseInt64()`：解析数值字段。
- `offlineStatus()`：构造用户离线 `NotFound`。
- `parseStatus()`：构造 value 损坏 `ParseError`。

这些 helper 都不暴露在头文件里，因为 key/value 格式是 `OnlineStatusCache` 的内部契约。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

`OnlineStatusCache` 位于业务 service 和 RedisPool 之间。

后续会在这些场景使用：

- AuthService 登录成功后写在线状态。
- HeartbeatService 或业务包处理成功后刷新 TTL。
- Session 断开后写下线。
- ChatService 发送消息前查询接收者是否在线。
- 私聊路由需要 `getOnlineSession()` 找到目标 session。

当前 Step 只提供组件，不接入这些 runtime 流程。

### 2. 上下层调用连接

```text
AuthService / ChatService / HeartbeatService
    -> business ThreadPool
    -> OnlineStatusCache
    -> RedisPool::acquire()
    -> RedisClient
    -> Redis SETEX / GET / EXPIRE / DEL
    -> online:user:<user_id>
```

上层只看到业务方法，不需要知道 Redis key 和 value 格式。

### 3. 整体运行链路

登录成功链路：

1. AuthService 校验用户名和密码。
2. AuthService 拿到 user_id 和当前 Session 的 session_id。
3. 调用 `setUserOnline(user_id, server_id, session_id, ttl)`。
4. `OnlineStatusCache` 写 `online:user:<user_id>`。
5. Redis 自动维护 TTL。

心跳续期链路：

1. 服务端收到完整有效入站 Packet。
2. 后续 HeartbeatService 判断用户已登录。
3. 调用 `refreshUserOnline(user_id, ttl)`。
4. Redis `EXPIRE` 成功则继续在线。
5. 如果 key 不存在，返回 `NotFound`，说明 Redis 状态已经过期或未写入。

发送消息链路：

1. ChatService 要发送给 target user。
2. 调用 `isUserOnline(target_user, online)`。
3. 如果 online=false，落离线消息并更新未读。
4. 如果 online=true，调用 `getOnlineSession()` 拿 session 信息。
5. 后续通过 SessionManager 找到连接并投递。

断开连接链路：

1. Session close 后，后续 service 得知 user_id。
2. 调用 `setUserOffline(user_id)`。
3. 删除 Redis key。
4. 如果没删到，也依赖 TTL 兜底。

### 4. 自身内部运行流程

上线写入流程：

```text
input fields / OnlineSession
    -> fill last_active_time_ms if needed
    -> validate user/session/server/ttl
    -> acquire RedisConnectionGuard
    -> serialize "v1:..."
    -> SETEX online:user:<id> ttl value
```

查询在线流程：

```text
online = false
    -> validate user_id
    -> acquire RedisConnectionGuard
    -> GET online:user:<id>
    -> nil: ok + false
    -> string: parseSessionValue()
    -> parse ok: online = true
    -> parse failed: ParseError
```

读取 session 流程：

```text
session = {}
    -> validate user_id
    -> GET key
    -> nil: NotFound
    -> parse value into OnlineSession
```

value 解析流程：

```text
check "v1:" prefix
    -> read user_id
    -> read session_id
    -> read last_active_time_ms
    -> read server_id_size
    -> read remaining bytes as server_id
    -> compare server_id.size() with declared size
    -> validate session
```

### 5. 该项目代码在实际应用中的具体数据例子

Bob 登录成功后，业务层可以写 `OnlineSession{user_id=1002, session_id=43, server_id=liteim-dev-1, last_active_time_ms=1700000000000}`，Redis key 是 `online:user:1002`，TTL 例如 90 秒。后续完整有效入站 Packet 已由 `Session` 刷新 `last_active_time`；已登录用户的业务心跳只需要让 HeartbeatService 调用 `refreshUserOnline(1002, ttl)` 刷新 Redis TTL，不直接修改 `Session::last_active_time`。

## 6. 关键实现点

- 保持模块职责单一。
- 失败时返回清晰错误，不吞掉异常状态。
- 不跨越本 Step 边界提前实现后续业务。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `OnlineStatusCache` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

测试覆盖：

- `OnlineStatusCache.hpp` 头文件自包含。
- `setUserOnline()` 后可以查询在线状态。
- `server_id` 包含冒号时仍能正确解析。
- `refreshUserOnline()` 能延长 TTL。
- `setUserOffline()` 删除在线状态。
- TTL 到期后用户自动离线。
- 刷新不存在用户返回 `NotFound`。
- 无效上线参数返回 `InvalidArgument`。

损坏 value 返回 `ParseError` 是当前实现的接口边界，后续如果扩展 Redis value 兼容性测试，应补专门用例覆盖这条路径。当前集成测试使用 Docker Redis，并用测试前缀 key 清理隔离。

## 8. 验证命令

```bash
cmake --build build
docker compose -f docker/docker-compose.yml ps
ctest --test-dir build -R "OnlineStatusCache" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

### 一句话

Step 29 在 RedisPool 上实现在线状态缓存。

### 展开说

可以这样说：

> Step 29 在 RedisPool 上实现在线状态缓存。key 固定为 `online:user:<user_id>`，value 使用带版本和 server_id 长度的格式保存 user_id、session_id、last_active_time_ms 和 server_id，TTL 表示在线有效期。登录成功用 `SETEX` 写入，心跳用 `EXPIRE` 刷新，断开连接用 `DEL` 删除，查询用 `GET` 并解析 value。key 不存在时 `isUserOnline()` 返回 false，`getOnlineSession()` 返回 `NotFound`，损坏 value 返回 `ParseError`，避免错误路由。

### 容易被追问

- Redis 在线状态为什么还需要 TTL？
- HeartbeatService 和 Session 活跃时间怎么分工？

## 10. 面试常见追问

### Q1：Redis 在线状态为什么还需要 TTL？

连接异常断开、进程崩溃或下线清理失败时，TTL 能让在线状态最终过期，避免用户永久显示在线。

### Q2：HeartbeatService 和 Session 活跃时间怎么分工？

Session 读路径在完整合法入站 Packet 后刷新连接活跃时间；HeartbeatService 只处理业务心跳响应，并为已登录用户刷新 Redis 在线 TTL。
