# Step 32：SessionManager 和 OnlineService

## 0. 本 Step 结论

- 目标：Step 32 的目标是进入业务层第一步：把“哪个用户绑定到哪个 TCP Session”放进当前进程内存，并把在线状态同步到 Redis。
- 前置依赖：依赖 Step 0-31 已建立的工程、协议或运行时基础。
- 主要交付：`SessionManager 和 OnlineService` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

Step 32 的目标是进入业务层第一步：把“哪个用户绑定到哪个 TCP Session”放进当前进程内存，并把在线状态同步到 Redis。

这一 Step 不做注册、密码校验、消息路由或聊天业务。它只解决后续 AuthService 登录成功之后必须马上处理的状态问题：

```text
user_id -> 当前进程里的 Session
session_id -> user_id
user_id -> Redis online:user:<user_id>
```

### 概念

网络层里的 `TcpServer` 只知道 session id，不知道这个连接属于哪个登录用户。存储层和缓存层已经有了 MySQL / Redis 能力，但还没有把“登录态”和“连接对象”关联起来。

Step 32 增加两个类：

- `SessionManager`：当前进程内的用户和 `Session` 绑定表。
- `OnlineService`：组合 `SessionManager` 和 `ICache`，负责上线、心跳续期、下线清理。

重复登录策略采用“踢旧保新”：

- 同一个 `user_id` 新登录成功时，新 session 成为当前绑定。
- 旧 session 会在 `SessionManager` 内部从表里移除，然后在锁外调用 `close()`。
- Redis 在线状态写成新 session 的 `session_id`。
- 旧 session 后续 close 回调只允许清理旧 `(user_id, session_id)`，不能删除新 session 的 Redis 在线 key。

边界：

- 不实现 AuthService，所以不校验密码。
- 不实现 MessageRouter，所以不解析登录 Packet。
- 不实现 ChatService，所以不投递私聊/群聊消息。
- 不接入 `TcpServer::setMessageCallback()`，避免提前把协议 runtime 和业务 service 混在一起。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `SessionManager 和 OnlineService` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/service/SessionManager.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/service/SessionManager.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `include/liteim/service/OnlineService.hpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/service/OnlineService.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/service/CMakeLists.txt` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/service/session_manager_test.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/service/online_service_test.cpp` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `tutorials/step32_session_manager_online_service.md` | 新增 | 承载本 Step 对应代码、测试或文档变化 |
| `src/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `/home/yolo/jianli/PROJECT_MEMORY.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

### SessionManager.hpp

```cpp
class SessionManager {
public:
    Status bindUser(std::uint64_t user_id, const Session::Ptr& session);
    Status unbindUser(std::uint64_t user_id, std::uint64_t session_id);
    Status unbindUser(std::uint64_t user_id, std::uint64_t session_id, bool& removed);
    Status getSessionByUser(std::uint64_t user_id, Session::Ptr& session);
    Status getUserBySession(std::uint64_t session_id, std::uint64_t& user_id);
    Status getBoundUserBySession(std::uint64_t session_id, std::uint64_t& user_id);

    std::size_t userCount() const;
    std::size_t sessionCount() const;

private:
    struct UserBinding {
        std::uint64_t session_id{0};
        std::weak_ptr<Session> session;
    };

    void eraseBindingLocked(std::uint64_t user_id, std::uint64_t session_id);

    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, UserBinding> users_;
    std::unordered_map<std::uint64_t, std::uint64_t> sessions_;
};
```

#### `bindUser()`

```cpp
Status bindUser(std::uint64_t user_id, const Session::Ptr& session);
```

把一个登录用户绑定到一个 `Session`。

输入要求：

- `user_id > 0`。
- `session != nullptr`。
- `session->id() > 0`。
- session 当前不能已经关闭。

重复登录：

- 如果同一个用户已经绑定旧 session，旧绑定先从表中移除。
- 新绑定写入 `users_` 和 `sessions_`。
- 旧 session 在释放 mutex 后关闭，避免锁内触发 close callback 重入。

失败语义：

- 参数非法返回 `InvalidArgument`。
- 同一个 session id 已经绑定到另一个用户时返回 `AlreadyExists`。

#### `unbindUser()`

```cpp
Status unbindUser(std::uint64_t user_id, std::uint64_t session_id);
Status unbindUser(std::uint64_t user_id, std::uint64_t session_id, bool& removed);
```

只删除当前仍然匹配的 `(user_id, session_id)` 绑定。

这点是 Step 32 的关键：

```text
user_id=1002 旧 session_id=42
user_id=1002 新 session_id=43
旧 session 关闭后回调 unbindUser(1002, 42)
不能删除 session_id=43 的新绑定
```

带 `removed` 的重载用于 `OnlineService` 判断是否真的删除了当前绑定。只有 `removed=true` 时才能删除 Redis 在线状态。

#### `getSessionByUser()`

```cpp
Status getSessionByUser(std::uint64_t user_id, Session::Ptr& session);
```

按用户查当前进程内的 session。

语义：

- 找到且 weak_ptr 仍然有效：输出 `shared_ptr<Session>`。
- 找不到、weak_ptr 过期、session 已关闭：返回 `NotFound`。
- stale binding 会在查询时顺手清理。

#### `getUserBySession()`

```cpp
Status getUserBySession(std::uint64_t session_id, std::uint64_t& user_id);
```

按 session id 反查 user id。

后续心跳、断开回调和消息路由都需要这个方向：

```text
Packet 来自 session_id=43
    -> getUserBySession(43)
    -> user_id=1002
```

普通查询会检查 `weak_ptr<Session>` 是否还能 lock，以及 session 是否已经关闭。如果 session 已关闭或过期，会清理 stale binding 并返回 `NotFound`。

#### `getBoundUserBySession()`

```cpp
Status getBoundUserBySession(std::uint64_t session_id, std::uint64_t& user_id);
```

这个接口只查 `session_id -> user_id` 绑定，不因为 `Session::closed()` 就清理映射。它给连接关闭清理使用：close callback 触发时，session 可能已经进入 closed 状态，但 service 仍然需要先知道这个 session 原来绑定哪个用户，才能调用 `OnlineService::unbindUser(user_id, session_id)` 做 Redis online 清理。

#### 成员变量

`users_` 是主表：

```text
user_id -> {session_id, weak_ptr<Session>}
```

用 `weak_ptr` 是为了不让登录态管理器延长 `Session` 生命周期。真正拥有 session 的仍然是 `TcpServer` / close 流程。

`sessions_` 是反查表：

```text
session_id -> user_id
```

两个表都受同一个 mutex 保护。`Session::close()` 不在锁内调用。

### OnlineService.hpp

```cpp
class OnlineService {
public:
    OnlineService(SessionManager& sessions, ICache& cache, std::string server_id,
                  std::chrono::seconds online_ttl);

    Status bindUser(std::uint64_t user_id, const Session::Ptr& session);
    Status unbindUser(std::uint64_t user_id, std::uint64_t session_id);
    Status unbindSession(std::uint64_t session_id);
    Status refreshUserOnline(std::uint64_t user_id, std::uint64_t session_id);

    Status getSessionByUser(std::uint64_t user_id, Session::Ptr& session);
    Status getUserBySession(std::uint64_t session_id, std::uint64_t& user_id);

    const std::string& serverId() const noexcept;
    std::chrono::seconds onlineTtl() const noexcept;

private:
    Status validateSession(std::uint64_t user_id, const Session::Ptr& session) const;

    SessionManager& sessions_;
    ICache& cache_;
    std::string server_id_;
    std::chrono::seconds online_ttl_;
};
```

`OnlineService` 不拥有 `SessionManager`，也不拥有 `ICache`。真实运行时可以传入 `RedisCache`，单元测试可以传入 fake cache。

#### `bindUser()`

登录成功后调用：

```text
OnlineService::bindUser(user_id, session)
    -> ICache::setUserOnline(OnlineSession, ttl)
    -> SessionManager::bindUser(user_id, session)
```

`OnlineSession` 里保存：

- `user_id`
- `session_id`
- `server_id`
- `last_active_time_ms`

#### `unbindUser()`

断开连接或主动下线时调用：

```text
OnlineService::unbindUser(user_id, session_id)
    -> SessionManager::unbindUser(user_id, session_id, removed)
    -> removed=true 时 ICache::setUserOffline(user_id)
```

如果是旧 session 的延迟 close 回调，`removed=false`，不删 Redis。

#### `unbindSession()`

连接关闭时，`TcpServer` 只天然知道 `session_id`。因此 runtime close cleanup 走：

```text
TcpServer close callback
    -> business ThreadPool
    -> OnlineService::unbindSession(session_id)
    -> SessionManager::getBoundUserBySession(session_id)
    -> OnlineService::unbindUser(user_id, session_id)
```

这里不能用普通 `getUserBySession()`，因为 close callback 触发时 session 可能已经 closed。`getBoundUserBySession()` 保留了 close-safe 的绑定反查能力；真正删除 Redis online key 时仍复用 `unbindUser()` 的“只有当前匹配才删除”语义，避免旧 session 误删新登录的 Redis key。

#### `refreshUserOnline()`

心跳成功后调用：

```text
OnlineService::refreshUserOnline(user_id, session_id)
    -> SessionManager::getUserBySession(session_id)
    -> 确认 session_id 仍属于 user_id
    -> ICache::refreshUserOnline(user_id, ttl)
```

旧 session 的心跳不能刷新新登录 session 的在线 TTL。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

Step 34 AuthService 登录成功后会调用：

```text
OnlineService::bindUser(user_id, session)
```

后续 ChatService 投递在线消息时会先走：

```text
SessionManager::getSessionByUser(receiver_id)
```

如果当前进程能找到 session，就优先本地投递；如果找不到，则走离线消息和 Redis 未读数。

### 2. 上下层调用连接

```text
AuthService / HeartbeatService / Session close callback
    -> OnlineService
    -> SessionManager
    -> ICache
    -> RedisCache
    -> OnlineStatusCache
    -> Redis
```

`SessionManager` 只处理内存表，不调用 Redis。

`OnlineService` 负责把内存表和 Redis 在线状态组合起来。

SessionManager
    只负责本进程内存关系
    不知道 Redis

OnlineService
    负责协调 SessionManager + ICache
    间接使用 Redis

RedisCache / OnlineStatusCache
    真正负责 Redis 读写

### 3. SessionManager 内部运行流程

用户第一次登录：

```text
bindUser(1002, session_id=42)
    -> users_[1002] = {42, weak_ptr(session)}
    -> sessions_[42] = 1002
```

同一用户再次登录：

```text
bindUser(1002, session_id=43)
    -> 找到旧绑定 {42, weak_ptr(old_session)}
    -> 删除 sessions_[42]
    -> users_[1002] = {43, weak_ptr(new_session)}
    -> sessions_[43] = 1002
    -> 解锁
    -> old_session->close()
```

旧 session close 回调：

```text
unbindUser(1002, 42, removed)
    -> 当前 users_[1002].session_id 是 43
    -> 不删除
    -> removed = false
```

### 4. OnlineService 内部运行流程

登录上线：

```text
bindUser(1002, session_id=43)
    -> setUserOnline({1002, 43, "liteim-dev-1", last_active_time_ms}, 90s)
    -> SessionManager::bindUser(1002, session)
```

心跳续期：

```text
refreshUserOnline(1002, 43)
    -> getUserBySession(43) == 1002
    -> refreshUserOnline(1002, 90s)
```

断开下线：

```text
unbindSession(43)
    -> getBoundUserBySession(43) 得到 user_id=1002
    -> unbindUser(1002, 43)
    -> SessionManager 删除当前绑定
    -> setUserOffline(1002)
```

### 5. 该项目代码在实际应用中的具体数据例子

Bob 在手机端已经登录：

```text
user_id = 1002
old_session_id = 42
server_id = liteim-dev-1
Redis key = online:user:1002
Redis value = user_id=1002, session_id=42, server_id=liteim-dev-1
```

Bob 又在电脑端登录：

```text
new_session_id = 43
OnlineService::bindUser(1002, session_id=43)
```

执行后：

```text
SessionManager:
  users_[1002] = {session_id=43, weak_ptr(new_session)}
  sessions_[43] = 1002
  sessions_[42] 不存在

Redis:
  online:user:1002 -> session_id=43

old_session:
  close() 被调用
```

如果旧 session 的 close 回调稍后执行：

```text
OnlineService::unbindUser(1002, 42)
```

因为当前绑定已经是 `43`，这次解绑不会删除 Redis key，Bob 的新登录仍然保持在线。

### 6. 线程 / 生命周期 / 所有权注意点

- `SessionManager` 使用 mutex 保护两张 map。
- `SessionManager` 只保存 `weak_ptr<Session>`，不拥有连接生命周期。
- `bindUser()` 关闭旧 session 时先释放 mutex，避免 close callback 重入死锁。
- `OnlineService` 会调用阻塞的 `ICache` 实现，真实运行时应放在 business 线程，不放在 Reactor I/O 线程。
- `Session::close()` 自己会投递到 owner loop，所以 service 不直接操作 fd。

## 6. 关键实现点

- 保持模块职责单一。
- 失败时返回清晰错误，不吞掉异常状态。
- 不跨越本 Step 边界提前实现后续业务。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `SessionManager 和 OnlineService` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

新增测试：

```text
tests/service/session_manager_test.cpp
tests/service/online_service_test.cpp
```

`SessionManagerTest` 覆盖：

- header 自包含。
- 用户和 session 双向绑定查询。
- 重复登录踢旧保新。
- stale 旧 session 解绑不会删除新绑定。
- 连接已经 closed 时，close cleanup 仍能通过 `getBoundUserBySession()` 查到原 user id。
- weak_ptr 过期时查询清理 stale binding。

`OnlineServiceTest` 覆盖：

- 登录绑定写 cache 并写内存表。
- 重复登录后旧 session 关闭，新 session 的 Redis 在线状态不被旧解绑删除。
- `unbindSession(session_id)` 能清理已关闭当前 session 的内存绑定和 Redis online key。
- 旧 session 的 close cleanup 不会删除新 session 的 Redis online key。
- 当前 session 解绑时同时清理内存和 cache。
- 心跳刷新必须来自当前 session。
- `RedisCache` 集成测试验证真实 Redis online key 的写入、刷新和删除；本地 Redis 不可用时按项目规则 skip。

运行命令：

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "SessionManagerTest|OnlineServiceTest|OnlineServiceRedisIntegrationTest" --output-on-failure
```

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "SessionManagerTest|OnlineServiceTest|OnlineServiceRedisIntegrationTest" --output-on-failure
```

## 9. 面试表达

### 一句话

本 Step 的核心是把 `SessionManager 和 OnlineService` 做成边界清楚、可测试、可继续扩展的一层。

### 展开说

围绕为什么需要 `SessionManager 和 OnlineService`、它依赖哪些前置 Step、它暴露什么接口、失败时怎么返回、线程和生命周期边界在哪里展开。

### 容易被追问

- 为什么 `SessionManager` 用 weak_ptr，而不是 shared_ptr？
- 重复登录为什么选择踢旧保新？
- 为什么旧 session close 不能直接 setUserOffline？
- 为什么关闭旧 session 要在锁外做？
- Redis 在线状态和内存 SessionManager 分别解决什么？

## 10. 面试常见追问

### Q1：为什么 `SessionManager` 用 weak_ptr，而不是 shared_ptr？

因为 `TcpServer` / `Session` close 流程才是连接生命周期的主拥有者。登录态管理器只是索引当前在线用户，不应该因为 map 里有一项就让已经断开的连接无法释放。

### Q2：重复登录为什么选择踢旧保新？

IM 场景里用户重新登录时通常希望新客户端立即可用。如果拒绝新登录，旧连接半死不活时用户可能只能等待心跳超时。踢旧保新让 Redis 在线状态和内存绑定都指向最新 session。

### Q3：为什么旧 session close 不能直接 setUserOffline？

因为旧 close 回调可能晚于新登录。必须先确认 `(user_id, session_id)` 仍然是当前绑定，只有真正删除当前绑定时才能清 Redis；否则会把新登录误标记为离线。

### Q4：为什么关闭旧 session 要在锁外做？

`Session::close()` 可能触发 close callback，而 callback 又可能回到 `OnlineService::unbindUser()`。锁内 close 会造成重入死锁或复杂锁顺序问题。

### Q5：Redis 在线状态和内存 SessionManager 分别解决什么？

`SessionManager` 解决当前进程内快速投递：拿到 `shared_ptr<Session>` 后可以发消息。Redis 解决状态展示和后续多进程扩展：别的模块可以知道用户当前是否在线、在哪个 server/session 上。
