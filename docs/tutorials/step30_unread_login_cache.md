# Step 30：UnreadCounter 和 LoginRateLimiter

## 0. 本 Step 结论

- 目标：Step 30 的目标是在 RedisPool 之上实现两个短期状态组件。
- 前置依赖：依赖 Step 0-29 已建立的工程、协议或运行时基础。
- 主要交付：`UnreadCounter 和 LoginRateLimiter` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

Step 30 的目标是在 `RedisPool` 之上实现两个短期状态组件：

- `UnreadCounter`：维护某个用户在某个会话上的未读消息数。
- `LoginRateLimiter`：维护某个用户名和来源 IP 的短期登录失败次数。

到 Step 29 为止，LiteIM 已经有 Redis client、Redis pool 和在线状态缓存。Step 30 继续把业务层常用 Redis key 收口到专门组件里，避免后续 AuthService / ChatService 直接拼 Redis 命令。

### 概念

未读数和登录失败次数都属于 Redis 适合处理的短期状态。

未读数的特点：

- 它不是消息正文的最终事实来源。
- 消息正文仍然落 MySQL。
- 未读数只是客户端会话列表上的快捷状态。
- 用户打开会话或拉取离线消息后可以清零。

登录失败限制的特点：

- 它只在一个短时间窗口内有效。
- 失败次数达到阈值后临时拒绝登录。
- TTL 到期后自动恢复允许。
- 登录成功后清除失败计数。

Step 30 只实现 cache 组件本身，不把它接入登录流程或消息投递流程。真正的调用点会在后续 service Step 中完成。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `UnreadCounter 和 LoginRateLimiter` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/cache/UnreadCounter.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `include/liteim/cache/LoginRateLimiter.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/cache/UnreadCounter.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/cache/LoginRateLimiter.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/cache/unread_login_cache_test.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/tutorials/step30_unread_login_cache.md` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/cache/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

```cpp
class UnreadCounter {
public:
    explicit UnreadCounter(RedisPool& pool,
                           std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds{200});

    Status incrUnread(const UnreadKey& key, std::uint64_t delta, std::uint64_t& unread_count);
    Status getUnread(const UnreadKey& key, std::uint64_t& unread_count);
    Status clearUnread(const UnreadKey& key);

private:
    RedisPool& pool_;
    std::chrono::milliseconds acquire_timeout_;
};
```

### 构造函数

`UnreadCounter(RedisPool& pool, acquire_timeout)` 保存 Redis 连接池引用和 acquire 超时时间。

所有权边界：

- `UnreadCounter` 不拥有 `RedisPool`。
- 调用方必须保证 `RedisPool` 生命周期长于 counter。
- 每个 public 方法内部临时 acquire 一个 `RedisConnectionGuard`。
- guard 析构后 Redis client 自动归还池中。

线程边界：

- 方法会阻塞等待 Redis 连接和命令结果。
- 只能在 business 线程使用。
- 不允许在 Reactor I/O loop 直接调用。

关键成员：

- `RedisPool& pool_`：Redis 连接池。
- `std::chrono::milliseconds acquire_timeout_`：默认 200ms，避免业务线程永久等待连接。

### `incrUnread()`

```cpp
Status incrUnread(const UnreadKey& key, std::uint64_t delta, std::uint64_t& unread_count);
```

递增某个用户某个会话的未读数，并把递增后的值写入 `unread_count`。

输入要求：

- `key.user_id > 0`。
- `key.conversation.id > 0`。
- `key.conversation.type` 必须是 `kPrivate` 或 `kGroup`。
- `delta > 0`。
- `delta <= INT64_MAX`，因为 Redis integer / `INCRBY` 按有符号 64 位整数语义处理。

Redis key：

```text
unread:user:<user_id>:conversation:<conversation_type>:<conversation_id>
```

Redis 命令：

```text
EVAL "return redis.call('INCRBY', KEYS[1], ARGV[1])" 1 <key> <delta>
```

使用 `EVAL + INCRBY` 的原因是：

- `delta` 可能大于 1。
- 递增必须是单条 Redis 原子操作。
- 不需要扩展 `RedisClient` 的 public API。

失败语义：

- 参数非法返回 `InvalidArgument`。
- Redis 连接池 acquire 失败向上返回原错误。
- Redis 返回无法解析的数值返回 `ParseError`。

### `getUnread()`

```cpp
Status getUnread(const UnreadKey& key, std::uint64_t& unread_count);
```

读取当前未读数。

语义：

- 调用开始先把 `unread_count = 0`。
- key 不存在时返回 ok，未读数保持 0。
- key 存在时解析 Redis string value。
- value 不是合法无符号整数时返回 `ParseError`。

### `clearUnread()`

```cpp
Status clearUnread(const UnreadKey& key);
```

删除未读数 key。

语义：

- key 存在：删除并返回 ok。
- key 不存在：`DEL` 返回 0，但仍返回 ok。

这个接口是幂等的。用户重复打开同一会话时重复清理未读数不应该报错。

```cpp
class LoginRateLimiter {
public:
    explicit LoginRateLimiter(RedisPool& pool,
                              std::chrono::milliseconds acquire_timeout = std::chrono::milliseconds{200});

    Status allow(const LoginAttemptKey& key, std::uint32_t max_failures, bool& allowed);
    Status recordFailure(const LoginAttemptKey& key, std::chrono::seconds ttl);
    Status clear(const LoginAttemptKey& key);

private:
    RedisPool& pool_;
    std::chrono::milliseconds acquire_timeout_;
};
```

### 构造函数

`LoginRateLimiter(RedisPool& pool, acquire_timeout)` 和 `UnreadCounter` 一样，只保存 Redis pool 引用，不拥有连接池。

线程边界也一样：

- 阻塞 Redis API。
- 只能在 business 线程使用。
- 不允许在 I/O loop 直接调用。

### `allow()`

```cpp
Status allow(const LoginAttemptKey& key, std::uint32_t max_failures, bool& allowed);
```

判断当前登录尝试是否允许。

输入要求：

- `key.username` 非空。
- `key.remote_ip` 非空。
- `max_failures > 0`。

Redis key：

```text
login:failure:<username_size>:<username>:<remote_ip_size>:<remote_ip>
```

key 中保存长度，是为了避免冒号造成歧义。IPv6 地址本身可能包含冒号，用户名理论上也不应该被 key 拼接格式限制。

语义：

- 调用开始先 `allowed = false`。
- key 不存在：说明当前窗口内没有失败记录，`allowed=true`。
- key 存在：解析失败次数。
- `failure_count < max_failures`：允许。
- `failure_count >= max_failures`：拒绝。

### `recordFailure()`

```cpp
Status recordFailure(const LoginAttemptKey& key, std::chrono::seconds ttl);
```

记录一次登录失败，并刷新失败窗口 TTL。

Redis 命令：

```text
EVAL "
  local value = redis.call('INCR', KEYS[1])
  redis.call('EXPIRE', KEYS[1], ARGV[1])
  return value
" 1 <key> <ttl_seconds>
```

使用 Lua 的原因：

- 失败次数递增和 TTL 设置应该一起完成。
- 避免只 `INCR` 成功但 `EXPIRE` 没设置时产生永久失败计数。

### `clear()`

```cpp
Status clear(const LoginAttemptKey& key);
```

清除登录失败计数。

典型调用点是后续 AuthService 登录成功后：

```text
密码校验成功 -> clear(username, remote_ip)
```

key 不存在也返回 ok，保持幂等。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

`UnreadCounter` 用在后续消息投递流程：

- 私聊接收方离线时，接收方对应私聊会话未读数 +1。
- 群聊中某些成员离线时，这些成员的群会话未读数 +1。
- 用户打开会话或拉取离线消息后，对应会话未读数清零。

`LoginRateLimiter` 用在后续登录流程：

- 登录前先检查当前用户名和来源 IP 是否允许尝试。
- 密码错误时记录一次失败。
- 失败次数达到阈值后，在 TTL 窗口内拒绝登录。
- 登录成功后清除失败计数。

### 2. 上下层调用连接

整体位置：

```text
AuthService / ChatService
    -> LoginRateLimiter / UnreadCounter
    -> RedisPool::acquire()
    -> RedisConnectionGuard
    -> RedisClient::get() / del() / eval()
    -> Docker Redis
```

上层 service 不需要知道 Redis key 格式，也不直接调用 `RedisClient`。

### 3. 整体运行链路

未读数递增：

```text
消息落 MySQL
    -> 判断接收方离线
    -> UnreadCounter::incrUnread()
    -> Redis INCRBY
    -> 返回新的未读数
```

未读数清零：

```text
用户打开会话
    -> 拉取历史 / 离线消息
    -> UnreadCounter::clearUnread()
    -> Redis DEL
```

登录失败限制：

```text
收到 LoginRequest
    -> LoginRateLimiter::allow()
    -> 允许则校验密码
    -> 密码错误 recordFailure()
    -> 密码正确 clear()
```

### 4. 自身内部运行流程

`UnreadCounter::incrUnread()` 内部流程：

```text
校验 UnreadKey 和 delta
    -> acquire RedisConnectionGuard
    -> EVAL INCRBY
    -> 解析返回值为 uint64
    -> 写入 unread_count
```

`UnreadCounter::getUnread()` 内部流程：

```text
校验 UnreadKey
    -> acquire RedisConnectionGuard
    -> GET key
    -> key 不存在返回 0
    -> key 存在解析 uint64
```

`LoginRateLimiter::allow()` 内部流程：

```text
校验 LoginAttemptKey 和 max_failures
    -> acquire RedisConnectionGuard
    -> GET failure key
    -> key 不存在 allowed=true
    -> 解析失败次数
    -> count < max_failures 才允许
```

`LoginRateLimiter::recordFailure()` 内部流程：

```text
校验 LoginAttemptKey 和 ttl
    -> acquire RedisConnectionGuard
    -> EVAL: INCR + EXPIRE
    -> 每次失败都会刷新 TTL
```

当前语义是滑动失败窗口：只要持续失败，每次 `recordFailure()` 都会刷新 key 的 TTL。`allow()` 和 `recordFailure()` 仍然是两个方法，后续 AuthService 如果需要强原子登录限流，可以再把“判断是否允许 + 本次失败递增”合成一个 Lua 脚本；第一版把它作为粗粒度防暴力破解保护。

### 5. 该项目代码在实际应用中的具体数据例子

Alice 给离线 Bob 发送 `conversation_id=10011002` 的私聊后，后续 ChatService 可以调用 `UnreadCounter::incrUnread({user_id=1002, conversation={1, 10011002}}, 1, unread)`，Redis key 是 `unread:user:1002:conversation:1:10011002`，返回 `unread=1`。登录限流则使用 `LoginAttemptKey{username=alice, remote_ip=127.0.0.1}`；连续失败 3 次后，`allow(key, 3, allowed)` 返回 `allowed=false`，登录成功再 `clear()`。

## 6. 关键实现点

- 保持模块职责单一。
- 失败时返回清晰错误，不吞掉异常状态。
- 不跨越本 Step 边界提前实现后续业务。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `UnreadCounter 和 LoginRateLimiter` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

新增 `tests/cache/unread_login_cache_test.cpp`。

覆盖内容：

- `UnreadCounter` 头文件和无 Redis 参数校验。
- 未读数从不存在 key 读取为 0。
- `incrUnread(delta)` 返回递增后的未读数。
- `clearUnread()` 后读取为 0。
- 不同用户、不同会话类型、不同会话 id 的 key 互相隔离。
- `LoginRateLimiter` 头文件和无 Redis 参数校验。
- 登录失败次数达到阈值后 `allow=false`。
- `clear()` 后重新允许。
- TTL 到期后重新允许。
- 同一用户名不同 IP、不同用户名同一 IP 互相隔离。

集成测试使用 `Config::defaults().redis`，也就是本地 Docker Redis `127.0.0.1:63790`。如果 Redis 不可用，测试会 skip。

## 8. 验证命令

```bash
cmake --build build
ctest --test-dir build -R "UnreadCounterTest|LoginRateLimiterTest|Step30CacheIntegrationTest" --output-on-failure
ctest --test-dir build -R "Redis|OnlineStatusCache|UnreadCounter|LoginRateLimiter|Step30CacheIntegrationTest" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

### 一句话

Step 30 在 RedisPool 上实现了两个业务 cache 组件：UnreadCounter 和 LoginRateLimiter。

### 展开说

可以这样讲：

> Step 30 在 RedisPool 上实现了两个业务 cache 组件：UnreadCounter 和 LoginRateLimiter。UnreadCounter 用 user id、conversation type、conversation id 组成 key，通过 Lua 调 Redis INCRBY 保证 delta 递增是单条原子操作，并把 delta 限制在 Redis 有符号 64 位整数范围内；不存在 key 读作 0，打开会话时 DEL 清零。LoginRateLimiter 用 username 和 remote_ip 组成失败计数 key，通过 Lua 把 INCR 和 EXPIRE 放在同一次 Redis 执行里，语义是滑动失败窗口。两个组件都只在 business 线程使用，不进入 Reactor I/O 线程，也不直接接入登录或聊天 service。

重点强调：

- Redis 保存短期状态，不保存消息正文。
- MySQL 仍是消息事实来源。
- key 格式被 cache 组件封装，业务层不手写 Redis 命令。
- 阻塞 Redis API 不进入 I/O 线程。
- TTL 负责登录失败窗口自动恢复。

### 容易被追问

- 未读计数为什么放 Redis？
- 登录限流为什么按 username + remote_ip 组合？

## 10. 面试常见追问

### Q1：未读计数为什么放 Redis？

未读数是高频状态，适合用 Redis 快速递增、读取和清零；消息正文仍以 MySQL 为准，Redis 不做最终消息来源。

### Q2：登录限流为什么按 username + remote_ip 组合？

只按用户名会让一个攻击源影响所有来源，只按 IP 又无法区分不同账号。组合 key 能覆盖第一版常见暴力尝试场景。
