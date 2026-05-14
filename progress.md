# LiteIM Progress

## 2026-05-14 Step 33 MessageRouter

本次进入 `Step 33：MessageRouter 异步分发框架`，目标是在不实现 AuthService / ChatService 的前提下，把网络层收到的 Packet 接到 service 层路由骨架。

TDD 过程：

- RED：新增 `tests/service/message_router_test.cpp` 并注册到 `tests/CMakeLists.txt` 后，`cmake --build build --target liteim_tests -j2` 失败于缺少 `liteim/service/MessageRouter.hpp`。
- GREEN：新增 `include/liteim/service/MessageRouter.hpp`、`src/service/MessageRouter.cpp`，更新 `src/service/CMakeLists.txt`、`server/CMakeLists.txt` 和 `server/main.cpp`。

已完成代码：

- `MessageRouter` 支持 `route(Session::Ptr, Packet)`、`registerHandler(MessageType, Handler, DispatchMode)`、`RouterRequest` 和 `DispatchMode::Inline / BusinessThread`。
- 默认 `HeartbeatRequest` inline 返回 `HeartbeatResponse`，不启动或使用业务线程池。
- 注册 handler 的请求会按 dispatch mode 执行；业务线程 handler 通过 `ThreadPool::submit()` 执行。
- Router 统一解析 TLV，统一构造 `ErrorResponse`，并强制响应 `seq_id` 等于请求 `seq_id`。
- 未知 / 非 request / 未注册 handler / TLV parse 失败 / handler 返回错误 / 线程池拒绝任务都会返回 `ErrorResponse`。
- 异步任务通过 `weak_ptr<Session>` 做开始前和发送前检查，关闭后不发送、不崩溃。
- `server/main.cpp` 启动业务 `ThreadPool`，创建 `MessageRouter`，并通过 `TcpServer::setMessageCallback()` 接入。

当前验证：

- `ctest --test-dir build -R "MessageRouter" --output-on-failure`：8/8 通过。
- `ctest --test-dir build -R "MessageRouter|Service|Session|TcpServer" --output-on-failure`：49/49 通过。
- `cmake --build build -j2`：通过。
- 首次 `ctest --test-dir build --output-on-failure`：271/272 通过，`LiteIMServerSignalTest.TerminatesOnSigterm` 失败，server 在 SIGTERM 后以 143 退出。
- 根因：`server/main.cpp` 先启动了业务 `ThreadPool`，后启动 `SignalWatcher`；`SignalWatcher::start()` 只阻塞当前线程信号，已创建的业务线程没有继承 SIGINT/SIGTERM 阻塞掩码，SIGTERM 可能落到业务线程按默认动作终止进程。
- 修复：调整 `server/main.cpp` 启动顺序，先 `signal_watcher.start()`，再 `business_pool.start()`；退出时先 `business_pool.stop()`，再 `signal_watcher.stop()`。
- `cmake --build build --target liteim_server -j2 && ctest --test-dir build -R "LiteIMServerSignal" --output-on-failure`：通过。
- `ctest --test-dir build --output-on-failure`：272/272 通过。
- `git diff --check`：通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，server 收到 SIGTERM 后通过 signalfd 退出。

## 2026-05-14 Step 32 SessionManager / OnlineService

本次按用户确认采用“踢旧保新”的重复登录策略，并同步相关 Markdown。

TDD 过程：

- RED：新增 `tests/service/session_manager_test.cpp` 和 `tests/service/online_service_test.cpp`，并把 `liteim_service` 加到测试链接；`cmake --build build --target liteim_tests -j2` 预期失败于缺失 `liteim/service/SessionManager.hpp`。
- GREEN：新增 `include/liteim/service/SessionManager.hpp`、`src/service/SessionManager.cpp`、`include/liteim/service/OnlineService.hpp`、`src/service/OnlineService.cpp`、`src/service/CMakeLists.txt`，并在 `src/CMakeLists.txt` 注册 `service` 模块。

已完成代码：

- `SessionManager` 维护 `user_id -> weak_ptr<Session>` 和 `session_id -> user_id`，支持绑定、按用户查 session、按 session 查用户、匹配解绑、stale weak_ptr 清理。
- 重复登录时，旧 session 从内存表移除后在锁外 `close()`，新 session 成为唯一当前绑定。
- `OnlineService` 通过 `ICache` 写入、刷新、删除 Redis 在线状态，并保证旧 session 的延迟解绑不会删除新 session 的在线 key。

已完成文档同步：

- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md`、README、`task_plan.md`、`findings.md`、本文件，以及 Step 21 / Step 29 / Step 31 相关教程中的 Step 32 边界描述。
- 新增 `tutorials/step32_session_manager_online_service.md`。

当前验证：

- `ctest --test-dir build -R "SessionManagerTest|OnlineServiceTest|OnlineServiceRedisIntegrationTest" --output-on-failure`：10/10 通过，其中 Redis 集成项因本机 Redis 不可用按规则 skipped。
- `cmake --build build -j2`：通过。
- `ctest --test-dir build --output-on-failure`：264/264 通过；MySQL / Redis 相关集成项因本机服务不可用按规则 skipped。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，server 启动后收到 timeout SIGTERM 并通过 signalfd 退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧路线 `server/net` / `server/protocol` / `SQLite` / `InMemory` 路径扫描：无输出。
- 教程结构扫描：`tutorials/step00` 到 `tutorials/step32` 的最后一个主章节都是“面试常见追问”；教程内无 `提交信息` 主章节；Step 32 含“该项目代码在实际应用中的具体数据例子”。

错误记录：

- 一次文案扫描命令把带反引号的 pattern 放进双引号，shell 执行了命令替换并输出 `SessionManager: command not found`。已改用单引号重新执行，确认没有旧 Step 32 / Pre-Step 31 表述残留。

## 2026-05-13 Step 31 Route Documentation Alignment

本次按用户确认做 Markdown-only 路线调整，不修改 C++ 代码逻辑，不回滚现有 dirty diff。

已完成文档修复：

- 将原独立存储/缓存契约小重构正式纳入 `Step 31：MySqlStorage 和 RedisCache 聚合适配层`。
- 将原 Step 31 `SessionManager and OnlineService` 后移为 Step 32，并把后续业务、CLI/测试/CI、Qt、最终文档阶段编号整体顺延为 Step 32-41、Step 42-46、Step 47-54、Step 55。
- 新增 `tutorials/step31_storage_cache_adapters.md`，按固定教程结构讲解 `MySqlStorage : IStorage`、`RedisCache : ICache`、事务边界、Redis 缓存边界、测试和面试追问。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md`、README、`task_plan.md`、`findings.md`、本文件，以及 Step 21 / Step 23 / Step 26 教程里的旧编号和 Pre-Step 说法。

当前验证：

- 残留旧路线扫描：无旧 Pre-Step 表述、无旧 Step 31 业务层表述、无旧阶段区间编号命中。
- 教程结构扫描：Step 00-31 的最后一个主章节都包含“面试常见追问”；教程内无提交信息章节。
- `git -C /home/yolo/jianli/LiteIM diff --check -- README.md task_plan.md findings.md progress.md tutorials/step21_storage_cache_interfaces.md tutorials/step23_mysql_connection.md tutorials/step26_message_dao.md tutorials/step31_storage_cache_adapters.md`：通过。
- 触碰文件 trailing whitespace 扫描：无命中。

## 2026-05-12 Markdown Contract Alignment / Doc Drift Fix

本次只做 Markdown / 项目记忆同步，不修改 C++ 代码逻辑，不回滚现有 dirty diff，也不作为 Step 31。

已完成文档修复：

- `/home/yolo/jianli/PROJECT_MEMORY.md` 的教程结构约束改为 `概念 -> hpp 接口说明 -> 作用场景和运行流程 -> 测试 -> 面试常见追问`；教程文件不再维护“提交信息”章节。
- `tutorials/step00_reset.md` 到 `tutorials/step30_unread_login_cache.md` 的运行流程第 5 小节统一改为“该项目代码在实际应用中的具体数据例子”，并改成带 LiteIM 真实数据的场景说明。
- 移除教程里的“提交信息 / 本 Step 提交信息”章节，补齐缺失的“面试常见追问”，并确保它是每个教程最后的主章节。
- 同步 MySQL history 索引字段名为 `(conversation_type, conversation_id, message_id)`。
- 同步 HeartbeatService 与 `Session::last_active_time` 分工：完整合法入站 Packet 在 `Session` 读路径刷新连接活跃时间，HeartbeatService 只返回响应并为已登录用户刷新 Redis 在线 TTL。
- 同步 `saveMessageWithOfflineRecipients()` 当前契约：重复离线用户先去重；`queryLastInsertedMessage()` 成功后、`COMMIT` 前失败会 `ROLLBACK` 并清空 `saved_message`。
- 更新 `task_plan.md` 和 `findings.md`，把本轮记为 Markdown contract alignment / doc drift fix，不记成 Step 31。

当前验证：

- Markdown 结构扫描：教程无提交信息章节；每个 Step 都包含“面试常见追问”和“该项目代码在实际应用中的具体数据例子”；每个教程最后一个主章节都是“面试常见追问”。
- 漂移扫描：`PROJECT_MEMORY.md` 无旧 `(conv_type, conv_id, id)` / `conv_id` / `conv_type`；当前教程和 PROJECT_MEMORY 无 HeartbeatService 直接刷新 `Session::last_active_time` 的实现描述；README / AGENTS / CLAUDE 无 `Current Status` / `当前状态`。
- `git diff --check`：通过。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build --output-on-failure`：254/254 通过，其中新增通过项为 `SaveMessageWithOfflineRecipientsDeduplicatesOfflineUsers`。

## 2026-05-12 Step 31 Storage / Cache Adapter Layer

本次按用户确认将原独立存储/缓存契约小重构纳入正式 Step 31 教学路线，不实现 `SessionManager` / `OnlineService`；业务层从 Step 32 开始。

目标边界：

- 补 `MySqlStorage : IStorage` 和 `RedisCache : ICache`，让 Step21 的抽象有真实可注入实现。
- 将好友列表返回类型改成公开资料 DTO，避免把认证字段带到业务响应边界。
- 给 `PreparedStatement` 补 `bindUInt64()`，对齐 MySQL `BIGINT UNSIGNED` schema。
- 补 MySQL 消息保存 + 离线记录保存的统一事务入口，避免消息落库成功但离线投递记录失败。
- 补未读计数 delta 的 Redis signed-int 范围校验，并把 LoginRateLimiter 的滑动失败窗口语义写清楚。

当前状态：

- 已完成项目总设计、planning 文件和现有 dirty diff 检查。
- 已确认已有 dirty diff 只在 `MySqlConnection` 注释附近；后续如果触碰该文件，会保留既有变更，不无意回滚。
- TDD RED：新增/更新测试后首次 `cmake --build build --target liteim_tests -j2` 按预期失败，错误集中在缺少 `UserProfileRecord`、`IStorage::saveMessageWithOfflineRecipients()` 和 `PreparedStatement::bindUInt64()`。

已完成代码：

- 新增 `include/liteim/storage/MySqlStorage.hpp` 和 `src/storage/MySqlStorage.cpp`，实现 `MySqlStorage : IStorage`。
- 新增 `include/liteim/cache/RedisCache.hpp` 和 `src/cache/RedisCache.cpp`，实现 `RedisCache : ICache`。
- 新增 `UserProfileRecord`，并将 `IStorage::getFriends()` / `FriendDao::getFriends()` 改为返回公开资料，不返回 `password_hash` / `password_salt`。
- 新增 `PreparedStatement::bindUInt64()`，DAO 的 `BIGINT UNSIGNED` id 绑定改用 unsigned bind。
- 新增 `MySqlStorage::saveMessageWithOfflineRecipients()`，在同一 MySQL 事务内写 `messages` 和 `offline_messages`；Redis 未读数仍由后续业务层在 MySQL commit 成功后处理。
- `UnreadCounter::incrUnread()` 拒绝超过 Redis signed 64-bit integer 范围的 delta。

新增/更新测试：

- 更新 `StorageInterfaceTest`、`FriendGroupDaoIntegrationTest`、`MySqlIntegrationTest` 和 Step30 invalid-input 测试。
- 新增 `MySqlStorageIntegrationTest`，覆盖真实 `IStorage` 适配、公开好友资料、消息 + 离线记录同事务提交和离线插入失败回滚。
- 新增 `RedisCacheIntegrationTest`，覆盖真实 `ICache` 聚合在线状态、未读计数和登录失败限制。

已完成文档同步：

- 更新 README、Step21/23/25/26/27/30/31 教程、`task_plan.md`、`findings.md`、本文件和 `/home/yolo/jianli/PROJECT_MEMORY.md`。
- 明确 `LoginRateLimiter` 当前是滑动失败窗口，`allow()` / `recordFailure()` 分离，后续 AuthService 如需强原子登录门禁再扩展 Lua 脚本。
- 明确当前 v1 MySQL schema 没有 `users.user_type`，BotGateway 前若需要数据库级 normal/bot 区分，应单独做迁移。

当前验证：

- RED：`cmake --build build --target liteim_tests -j2` 首次按预期失败。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -R "StorageInterfaceTest|MySqlConnection|FriendGroupDao|MySqlStorage|UnreadCounter|LoginRateLimiter|RedisCache|CacheInterfaceTest" --output-on-failure`：25/25 通过。
- `ctest --test-dir build -R "MySqlIntegrationTest|MessageDaoIntegrationTest" --output-on-failure`：10/10 通过。
- `cmake --build build -j2`：通过。
- `ctest --test-dir build --output-on-failure`：254/254 通过，新增通过项来自 `SaveMessageWithOfflineRecipientsDeduplicatesOfflineUsers`。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过。
- `git diff --check`：通过。

## 2026-05-11 Step 30 UnreadCounter / LoginRateLimiter

本次正式开启 `Step 30：实现 UnreadCounter 和 LoginRateLimiter`。

概念边界：

- `UnreadCounter` 只管理 Redis 未读计数 key，提供递增、读取和清零。
- `LoginRateLimiter` 只管理 Redis 登录失败计数 key，提供是否允许、记录失败和清除失败计数。
- 本 Step 不接入 AuthService、ChatService、TcpServer、Session 或任何运行时业务流程。
- MySQL/Redis 仍然只能在后续业务线程中调用，不能放进 Reactor I/O 线程。

已完成测试 RED：

- 新增 `tests/cache/unread_login_cache_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 30 测试。
- RED：新增测试后首次 `cmake --build build --target liteim_tests` 按预期失败，错误为缺少 `liteim/cache/LoginRateLimiter.hpp`。

已完成代码：

- 新增 `include/liteim/cache/UnreadCounter.hpp` 和 `src/cache/UnreadCounter.cpp`。
- 新增 `include/liteim/cache/LoginRateLimiter.hpp` 和 `src/cache/LoginRateLimiter.cpp`。
- `src/cache/CMakeLists.txt` 接入 Step 30 cache 源文件。
- `UnreadCounter` 通过 Redis key 维护用户在私聊/群聊会话中的未读数，支持递增、读取和清零。
- `LoginRateLimiter` 通过 Redis failure key + TTL 维护用户名和远端地址维度的登录失败窗口，支持允许判断、记录失败和清除计数。

新增测试：

- `UnreadCounterTest.RejectsInvalidInputBeforeBorrowingRedis`
- `LoginRateLimiterTest.RejectsInvalidInputBeforeBorrowingRedis`
- `Step30CacheIntegrationTest.UnreadCounterIncrementsReadsAndClearsCount`
- `Step30CacheIntegrationTest.UnreadCounterSeparatesUsersAndConversations`
- `Step30CacheIntegrationTest.LoginRateLimiterRejectsAfterFailureThresholdAndClearAllowsAgain`
- `Step30CacheIntegrationTest.LoginRateLimiterTtlExpiryAllowsAgain`
- `Step30CacheIntegrationTest.LoginRateLimiterSeparatesUsernameAndRemoteIp`

已完成文档同步：

- README 更新 cache 模块说明、本地 Redis 说明和 storage/cache 验证命令。
- 新增 `tutorials/step30_unread_login_cache.md`。
- 更新 `task_plan.md`、`findings.md` 和本文件。

当前验证：

- RED：新增测试后首次 `cmake --build build --target liteim_tests` 按预期失败，错误为缺少 `liteim/cache/LoginRateLimiter.hpp`。
- `cmake --build build --target liteim_tests`：通过。
- `ctest --test-dir build -R "UnreadCounterTest|LoginRateLimiterTest|Step30CacheIntegrationTest" --output-on-failure`：7/7 通过。
- `cmake --build build`：通过。
- `ctest --test-dir build -R "Redis|OnlineStatusCache|UnreadCounter|LoginRateLimiter|Step30CacheIntegrationTest" --output-on-failure`：29/29 通过。
- `ctest --test-dir build --output-on-failure`：246/246 通过。

## 2026-05-11 Step 21-29 Tutorial Format Alignment

本次按已有计划修正 `tutorials/step21_storage_cache_interfaces.md` 到 `tutorials/step29_online_status_cache.md` 的 Markdown 教程结构。

已完成：

- Step 21 补齐独立概念章节，扩写 `StorageTypes.hpp`、`IStorage.hpp`、`CacheTypes.hpp` 和 `ICache.hpp` 的 DTO、接口、失败语义、线程边界和后续实现边界。
- Step 22 改成 Docker Compose / SQL 脚本契约说明，补 MySQL/Redis 服务契约、schema、seed 数据、Redis 空实例边界和标准运行流程。
- Step 23-29 全部扩写到详细接口说明和两层运行流程：既说明它们在 LiteIM 业务架构里的上下游位置，也说明模块自身内部如何运行。
- 每个 Step 都补齐测试设计、验证命令、面试说法和面试常见追问章节。
- 本次只修改 Step 21-29 教程和 planning 记录，不修改 `tutorials/step20_backpressure.md`，也不改 C++/SQL 行为。

当前验证：

- 教程结构扫描：Step 21-29 均包含接口/契约说明、作用场景和运行流程、验证命令、面试常见追问；运行流程章节均包含 5 个固定小节。
- `git diff --check`：通过。
- `cmake --build build`：通过。
- `ctest --test-dir build -R "StorageInterfaceTest|CacheInterfaceTest|MySql|UserDao|MessageDao|FriendGroupDao|Redis|OnlineStatusCache" --output-on-failure`：60/60 通过。
- `ctest --test-dir build --output-on-failure`：237/237 通过。

## 2026-05-11 Step 29 OnlineStatusCache

本次进入 `Step 29：实现 OnlineStatusCache`，目标是在 Step 28 `RedisClient` / `RedisPool` 之上提供 Redis TTL 在线状态缓存。

概念边界：

- `OnlineStatusCache` 只管理 `online:user:<user_id>` 在线状态 key。
- 在线状态保存 `user_id`、`session_id`、`server_id` 和 `last_active_time_ms`。
- 本 Step 不实现未读计数、登录失败限制、业务 service、SessionManager、OnlineService，也不修改 `TcpServer` / `Session` 运行时行为。

已完成测试 RED：

- 新增 `tests/cache/online_status_cache_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 29 测试。
- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/cache/OnlineStatusCache.hpp`。

已完成代码：

- 新增 `include/liteim/cache/OnlineStatusCache.hpp`。
- 新增 `src/cache/OnlineStatusCache.cpp`。
- `src/cache/CMakeLists.txt` 接入 `OnlineStatusCache.cpp`。
- `OnlineStatusCache` 通过 `RedisPool::acquire()` 借 Redis 连接，使用 `SETEX` 上线写 TTL、`EXPIRE` 心跳刷新 TTL、`DEL` 下线删除、`GET` 查询和解析 session。

新增测试：

- `OnlineStatusCacheTest.HeaderIsSelfContained`
- `OnlineStatusCacheIntegrationTest.SetUserOnlineMakesUserQueryable`
- `OnlineStatusCacheIntegrationTest.ServerIdMayContainColon`
- `OnlineStatusCacheIntegrationTest.RefreshUserOnlineExtendsTtl`
- `OnlineStatusCacheIntegrationTest.SetUserOfflineRemovesOnlineSession`
- `OnlineStatusCacheIntegrationTest.TtlExpiryMakesUserOffline`
- `OnlineStatusCacheIntegrationTest.RefreshMissingUserReturnsNotFound`

已完成文档同步：

- README 更新 cache 模块说明、本地 Redis 说明和 storage/cache 测试命令。
- 新增 `tutorials/step29_online_status_cache.md`。
- 更新 `task_plan.md`、`findings.md` 和本文件。

当前验证：

- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/cache/OnlineStatusCache.hpp`。
- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "OnlineStatusCache" --output-on-failure`：7/7 通过。
- `ctest --test-dir build --output-on-failure`：237/237 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(cache): add redis online status cache
```

## 2026-05-11 Step 28 RedisClient / RedisPool

本次进入 `Step 28：实现 RedisClient 和 RedisPool`，目标是在 Step 21 cache 接口和 Step 22 Docker Redis 之上提供 hiredis 阻塞客户端与固定连接池。

概念边界：

- `RedisClient` 只封装 Redis 连接、认证、DB 选择和基础命令。
- `RedisPool` 只负责固定数量 Redis 连接、线程安全 `acquire(timeout, guard)`、显式 `release()`、RAII 归还和失效重连。
- 本 Step 不实现在线状态 cache、未读数、登录失败限制、不接入业务 service、不修改 `TcpServer` / `Session` 运行时行为。

已完成测试 RED：

- 新增 `tests/cache/redis_client_pool_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 28 测试。
- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/cache/RedisClient.hpp`。

已完成代码：

- 新增 `include/liteim/cache/RedisClient.hpp`。
- 新增 `include/liteim/cache/RedisPool.hpp`。
- 新增 `src/cache/RedisClient.cpp`。
- 新增 `src/cache/RedisPool.cpp`。
- `src/cache/CMakeLists.txt` 从 header-only interface target 升级为静态库，并通过 `pkg-config hiredis` 链接 hiredis。

新增测试：

- `RedisClientTest.HeaderIsSelfContained`
- `RedisClientTest.UnavailableRedisReturnsErrorStatus`
- `RedisPoolTest.RejectsZeroPoolSize`
- `RedisIntegrationTest.ConnectsAndPingsLocalRedis`
- `RedisIntegrationTest.SetexAndGetRoundTripValue`
- `RedisIntegrationTest.GetMissingKeyReturnsEmptyOptional`
- `RedisIntegrationTest.ExpireRefreshesTtl`
- `RedisIntegrationTest.IncrReturnsIncrementedInteger`
- `RedisIntegrationTest.DelRemovesExistingKey`
- `RedisIntegrationTest.EvalCanReadRedisKey`
- `RedisPoolIntegrationTest.AcquiresConnectedRedisClient`
- `RedisPoolIntegrationTest.AcquireTimesOutWhenAllClientsAreBorrowed`
- `RedisPoolIntegrationTest.ReleaseReturnsClientToPool`
- `RedisPoolIntegrationTest.MultipleThreadsAcquireAndReleaseClients`
- `RedisPoolIntegrationTest.ReconnectsClientThatWasClosedWhileBorrowed`

已完成文档同步：

- README 更新 cache 模块说明、hiredis 构建依赖和 Redis 集成测试说明。
- 新增 `tutorials/step28_redis_client_pool.md`。
- 更新 `task_plan.md`、`findings.md` 和本文件。

当前验证：

- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/cache/RedisClient.hpp`。
- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "RedisClientTest|RedisIntegrationTest|RedisPoolTest|RedisPoolIntegrationTest" --output-on-failure`：15/15 通过。
- `ctest --test-dir build --output-on-failure`：230/230 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(cache): implement redis client and pool
```

## 2026-05-11 Step 27 FriendDao / GroupDao

本次进入 `Step 27：实现 FriendDao 和 GroupDao`，目标是在 Step 23/24/25 的 MySQL wrapper、连接池和 users DAO 之上提供好友表和群组表 DAO。

概念边界：

- `FriendDao` 只做 `friendships` 表的双向好友关系写入和好友列表查询。
- `GroupDao` 只做 `chat_groups` / `group_members` 表的建群、成员增删查和按 id 查群。
- 本 Step 不做好友申请审批、群管理员/禁言/公告、Redis 缓存、不接入业务 service、不修改 `TcpServer` / `Session` 运行时行为。

已完成测试 RED：

- 新增 `tests/storage/friend_group_dao_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 27 测试。
- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/storage/FriendDao.hpp`。

已完成代码：

- 新增 `include/liteim/storage/FriendDao.hpp`。
- 新增 `include/liteim/storage/GroupDao.hpp`。
- 新增 `src/storage/FriendDao.cpp`。
- 新增 `src/storage/GroupDao.cpp`。
- `src/storage/CMakeLists.txt` 接入 Step 27 DAO 源文件。

新增测试：

- `FriendGroupDaoTest.HeadersAreSelfContained`
- `FriendGroupDaoIntegrationTest.AddFriendshipCreatesBidirectionalRelationship`
- `FriendGroupDaoIntegrationTest.RepeatedAddFriendshipDoesNotCreateDuplicates`
- `FriendGroupDaoIntegrationTest.CreateGroupPersistsGroupAndOwnerMembership`
- `FriendGroupDaoIntegrationTest.AddGroupMemberIsIdempotent`
- `FriendGroupDaoIntegrationTest.RemoveGroupMemberRemovesNormalMember`
- `FriendGroupDaoIntegrationTest.FindMissingGroupReturnsNotFound`

已完成文档同步：

- README 更新 storage 模块说明和 MySQL storage 测试说明。
- 新增 `tutorials/step27_friend_group_dao.md`。
- 更新 `task_plan.md`、`findings.md` 和本文件。

当前验证：

- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "FriendGroupDaoTest|FriendGroupDaoIntegrationTest" --output-on-failure`：7/7 通过。
- `ctest --test-dir build --output-on-failure`：215/215 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(storage): implement friend and group dao
```

## 2026-05-11 Step 26 MessageDao / OfflineMessageDao

本次进入 `Step 26：实现 MessageDao 和 OfflineMessageDao`，目标是在 Step 23/24/25 的 MySQL wrapper、连接池和 users DAO 之上提供消息表和离线消息表 DAO。

概念边界：

- `MessageDao` 只做 `messages` 表私聊/群聊消息落库和会话历史分页查询，消息落库的 insert + 查回记录在同一事务内完成。
- `OfflineMessageDao` 只做 `offline_messages` 表待投递记录保存、pending 拉取和 delivered 标记。
- 本 Step 不做 Redis 未读数、不接入业务 service、不修改 `TcpServer` / `Session` 运行时行为。

已完成测试 RED：

- 新增 `tests/storage/message_dao_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 26 测试。
- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/storage/MessageDao.hpp`。

已完成代码：

- 新增 `include/liteim/storage/MessageDao.hpp`。
- 新增 `include/liteim/storage/OfflineMessageDao.hpp`。
- 新增 `src/storage/MessageDao.cpp`。
- 新增 `src/storage/OfflineMessageDao.cpp`。
- `src/storage/CMakeLists.txt` 接入 Step 26 DAO 源文件。
- `MySqlConnection` 新增 `executeSimple()`，用于 `START TRANSACTION`、`COMMIT`、`ROLLBACK` 这类无参数控制语句。

新增测试：

- `MessageDaoTest.HeadersAreSelfContained`
- `MessageDaoIntegrationTest.SavePrivateMessagePersistsRecord`
- `MessageDaoIntegrationTest.SaveGroupMessagePersistsRecord`
- `MessageDaoIntegrationTest.OfflineMessageSaveFetchAndDeliveredFlow`
- `MessageDaoIntegrationTest.HistoryReturnsNewestMessagesBeforeCursor`
- `MessageDaoIntegrationTest.HistoryLimitIsCappedAtFifty`

已完成文档同步：

- README 更新 storage 模块说明和 MySQL storage 测试说明。
- 新增 `tutorials/step26_message_dao.md`。
- 更新 `task_plan.md`、`findings.md` 和本文件。

当前验证：

- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "MessageDaoTest|MessageDaoIntegrationTest" --output-on-failure`：6/6 通过。
- `ctest --test-dir build --output-on-failure`：208/208 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(storage): implement message dao and offline messages
```

## 2026-05-11 Step 25 UserDao / AuthDao

本次进入 `Step 25：实现 UserDao 和 AuthDao`，目标是在 Step 23/24 的 MySQL wrapper 和连接池之上提供 users 表 DAO。

概念边界：

- `UserDao` 只做用户创建和按 username/id 查询。
- `AuthDao` 当前只做 username 存在性查询。
- DAO 不做注册/登录业务判断、不做密码校验、不接入网络层或 Redis。

已完成代码：

- 新增 `include/liteim/storage/UserDao.hpp`。
- 新增 `include/liteim/storage/AuthDao.hpp`。
- 新增 `src/storage/UserDao.cpp`。
- 新增 `src/storage/AuthDao.cpp`。
- `src/storage/CMakeLists.txt` 接入 DAO 源文件。
- `ErrorCode` 新增 `AlreadyExists`。
- `PreparedStatement` 新增 `lastErrorNumber()`，用于 DAO 识别 MySQL duplicate key errno。
- 新增 `tests/storage/user_dao_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 25 测试。

新增测试：

- `UserDaoTest.HeadersAreSelfContained`
- `UserDaoIntegrationTest.CreateUserPersistsAndReturnsCreatedRecord`
- `UserDaoIntegrationTest.CreateUserWorksWithSingleConnectionPool`
- `UserDaoIntegrationTest.DuplicateUsernameReturnsAlreadyExists`
- `UserDaoIntegrationTest.FindUserByUsernameReturnsCreatedUser`
- `UserDaoIntegrationTest.FindUserByIdReturnsCreatedUser`
- `UserDaoIntegrationTest.FindMissingUserReturnsNotFound`
- `UserDaoIntegrationTest.UsernameExistsReportsExistingAndMissingUsers`

已完成文档同步：

- README 更新 storage 模块说明和 MySQL storage 测试说明。
- 新增 `tutorials/step25_user_dao.md`。
- 更新 `task_plan.md`、`findings.md` 和本文件。

当前验证：

- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/storage/AuthDao.hpp`。
- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "UserDaoTest|UserDaoIntegrationTest|ErrorCodeTest" --output-on-failure`：9/9 通过。
- `ctest --test-dir build --output-on-failure`：202/202 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(storage): implement user dao
```

## 2026-05-11 Step 24 MySqlPool / ConnectionGuard

本次进入 `Step 24：实现 MySqlPool 和 ConnectionGuard`，目标是在 Step 23 单连接封装之上提供业务线程可复用的固定 MySQL 连接池。

概念边界：

- `MySqlPool` 负责固定数量连接、线程安全 `acquire(timeout, guard)`、连接失效重连和关闭。
- `ConnectionGuard` 负责借出连接的 RAII 自动归还。
- 本 Step 不实现 DAO、AuthService、ChatService、Redis client，也不让网络 I/O 线程访问 MySQL。

已完成代码：

- 新增 `include/liteim/storage/MySqlPool.hpp`。
- 新增 `src/storage/MySqlPool.cpp`。
- `src/storage/CMakeLists.txt` 接入 `MySqlPool.cpp`。
- 新增 `tests/storage/mysql_pool_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 24 测试。

新增测试：

- `MySqlPoolTest.HeaderIsSelfContained`
- `MySqlPoolTest.RejectsZeroPoolSize`
- `MySqlPoolIntegrationTest.AcquiresConnectedConnection`
- `MySqlPoolIntegrationTest.AcquireTimesOutWhenAllConnectionsAreBorrowed`
- `MySqlPoolIntegrationTest.ConnectionGuardReturnsConnectionOnDestruction`
- `MySqlPoolIntegrationTest.CloseMakesAcquireFail`
- `MySqlPoolIntegrationTest.MultipleThreadsAcquireAndReleaseConnections`
- `MySqlPoolIntegrationTest.ReconnectsConnectionThatWasClosedWhileBorrowed`

已完成文档同步：

- README 更新 storage 模块说明和 MySQL storage 测试说明。
- 新增 `tutorials/step24_mysql_pool.md`。
- 更新 `task_plan.md`、`findings.md` 和本文件。

当前验证：

- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/storage/MySqlPool.hpp`。
- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "MySqlPoolTest|MySqlPoolIntegrationTest" --output-on-failure`：8/8 通过。
- `ctest --test-dir build --output-on-failure`：194/194 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(storage): implement mysql connection pool
```

## 2026-05-11 Step 23 MySqlConnection / PreparedStatement

本次进入 `Step 23：实现 MySqlConnection 和 PreparedStatement`，目标是封装 MySQL C API，为后续连接池和 DAO 层提供最底层的连接与 prepared statement 能力。

概念边界：

- `MySqlConnection` 只负责单连接 RAII、连接、ping 和关闭。
- `PreparedStatement` 只负责 SQL prepare、参数绑定、执行更新和执行查询。
- `MySqlQueryResult` 用输出参数保存列名和行数据，不引入 `Result<T>`。
- 本 Step 不实现连接池、DAO、AuthService、ChatService、Redis client，也不让网络 I/O 线程访问 MySQL。

已完成代码：

- 新增 `include/liteim/storage/MySqlConnection.hpp`。
- 新增 `src/storage/MySqlConnection.cpp`。
- `src/storage/CMakeLists.txt` 从 header-only interface target 改为静态库，并通过 `pkg-config mysqlclient` 链接 MySQL C API。
- 新增 `tests/storage/mysql_connection_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 23 测试。

新增测试：

- `MySqlConnectionTest.HeaderIsSelfContained`
- `MySqlIntegrationTest.ConnectsAndPingsLocalMySql`
- `MySqlIntegrationTest.PreparedStatementExecutesSimpleSelect`
- `MySqlIntegrationTest.ExecuteUpdateAndQueryRoundTripSpecialCharacters`
- `MySqlIntegrationTest.InvalidSqlReturnsErrorStatus`

已完成文档同步：

- README 增加 MySQL C API wrapper、mysqlclient 构建依赖和 MySQL 集成测试说明。
- 新增 `tutorials/step23_mysql_connection.md`。
- 更新 `task_plan.md`、`findings.md` 和本文件。

验证：

- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/storage/MySqlConnection.hpp`。
- `cmake --build build`：通过。
- `ctest --test-dir build -R "MySqlConnectionTest|MySqlIntegrationTest|StorageInterfaceTest" --output-on-failure`：7/7 通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `ctest --test-dir build --output-on-failure`：186/186 通过。
- `git diff --check -- README.md task_plan.md findings.md progress.md tutorials/step23_mysql_connection.md tests/CMakeLists.txt tests/storage/mysql_connection_test.cpp include/liteim/storage/MySqlConnection.hpp src/storage/CMakeLists.txt src/storage/MySqlConnection.cpp`：通过。
- `ldd build/tests/liteim_tests | rg -n "mysql|maria|ssl|crypto|anaconda"`：确认链接到系统 `/lib/x86_64-linux-gnu/libmysqlclient.so.21`，没有链接 Anaconda mysqlclient。

提交信息：

```text
feat(storage): add mysql connection and prepared statement
```

## 2026-05-11 Step 22 Local Dev MySQL 8.0 / Password Alignment

本次在已完成的 Step 22 基础上更新本地开发依赖，不进入 Step 23。

已完成修改：

- `docker/docker-compose.yml`：MySQL 镜像改为 `mysql:8.0`；MySQL `liteim` 用户、MySQL root 用户、Redis 认证密码默认统一为 `6`；Redis 启动时开启 `requirepass`。
- `Config::defaults()`：MySQL 默认端口改为 `33060`、密码 `6`；Redis 默认端口改为 `63790`、密码 `6`。
- `tests/base/config_test.cpp`：补充默认 MySQL / Redis 开发端口和密码断言。
- `README.md`、`tutorials/step22_docker_mysql.md`、`task_plan.md`、`findings.md`：同步 MySQL 8.0、密码 `6`、Redis 认证和 `33060` 非 MySQL X Protocol 的说明。

验证：

- `docker compose -f docker/docker-compose.yml config`：通过。
- `docker compose -f docker/docker-compose.yml down -v && docker compose -f docker/docker-compose.yml up -d --wait`：重建本地开发数据卷并启动成功，MySQL / Redis 均 healthy。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 镜像为 `mysql:8.0`，端口仍为 `127.0.0.1:33060->3306`；Redis 端口为 `127.0.0.1:63790->6379`。
- MySQL 查询验证：`SELECT VERSION()` 返回 `8.0.46`；`SHOW TABLES` 返回 6 张表；seed 可查到 `alice`、`bob`、`mira_bot` 和两条未投递离线消息。
- Redis 认证验证：未带密码 `redis-cli ping` 返回 `NOAUTH Authentication required.`；`REDISCLI_AUTH=6 redis-cli ping` 返回 `PONG`。
- MySQL root 账号验证：`mysql -uroot -p6 -e "SELECT VERSION()"` 返回 `8.0.46`。
- MySQL Workbench keyring 中 `LiteIM Docker Local` 的 `liteim@127.0.0.1:33060` 密码已更新为 `6`。
- `cmake --build build`：通过。
- `ctest --test-dir build -R "ConfigTest" --output-on-failure`：7/7 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `ctest --test-dir build --output-on-failure`：181/181 通过。
- `git diff --check -- docker/docker-compose.yml include/liteim/base/Config.hpp tests/base/config_test.cpp README.md tutorials/step22_docker_mysql.md task_plan.md findings.md progress.md`：通过。

## 2026-05-10 Step 22 Docker Compose / MySQL Init SQL

本次进入 `Step 22：编写 Docker Compose 和 MySQL 初始化 SQL`，目标是为后续 MySQL / Redis 存储缓存实现准备可重复启动的本地开发环境。

概念边界：

- Docker Compose 只负责启动本地 MySQL / Redis。
- MySQL init SQL 只建主线表和索引。
- seed SQL 只写本地测试数据。
- 本 Step 不实现 C++ MySQL/Redis client、DAO、连接池、业务服务，也不让网络 I/O 线程执行阻塞存储调用。

已完成文件：

- 新增 `docker/docker-compose.yml`。
- 新增 `scripts/init_mysql.sql`。
- 新增 `scripts/seed_test_data.sql`。
- 新增 `tutorials/step22_docker_mysql.md`。
- 更新 `README.md`，加入 MySQL / Redis 本地开发说明和目录布局。
- 更新 `task_plan.md`、`findings.md` 和本文件。

MySQL schema：

- `users`
- `friendships`
- `chat_groups`
- `group_members`
- `messages`
- `offline_messages`

seed 数据：

- 用户：`alice`、`bob`、`mira_bot`。
- 群组：`dev_group`。
- 示例私聊、群聊消息和 pending offline message 记录。

验证：

- `docker compose -f docker/docker-compose.yml config`：通过。
- `LITEIM_MYSQL_PORT=33306 LITEIM_REDIS_PORT=36379 docker compose -p liteim-step22-verify -f docker/docker-compose.yml up -d --wait`：MySQL / Redis 均 healthy。
- MySQL 查询验证：`SHOW TABLES` 返回 6 张表；`SHOW INDEX FROM messages` 包含 `idx_messages_history`、`idx_messages_sender`、`idx_messages_receiver`；seed 可查到 `alice`、`bob`、`mira_bot`、`dev_group` 和两条未投递离线消息。
- 后续本地开发口径更新：MySQL 开发镜像固定为 `mysql:8.0`，避免 Workbench 8.0 对 MySQL 8.4 弹兼容性警告；MySQL/Redis 开发密码统一为 `6`。
- Redis 验证：`REDISCLI_AUTH=6 redis-cli ping` 返回 `PONG`。
- `LITEIM_MYSQL_PORT=33306 LITEIM_REDIS_PORT=36379 docker compose -p liteim-step22-verify -f docker/docker-compose.yml down -v`：临时验证容器和数据卷已清理。
- `cmake --build build`：通过。
- `ctest --test-dir build --output-on-failure`：181/181 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。

提交信息：

```text
feat(infra): add mysql redis docker environment
```

## 2026-05-10 Step 21 Storage / Cache Interfaces

本次进入 `Step 21：定义 IStorage / ICache 抽象`，目标是在接入真实 MySQL / Redis 前先建立业务层依赖的接口边界。

概念边界：

- `IStorage` 是未来 MySQL 持久化层的抽象。
- `ICache` 是未来 Redis 状态层的抽象。
- 业务层后续依赖接口，不直接 include MySQL / Redis 具体实现。
- 本 Step 不实现 MySQL client、Redis client、DAO、SQL schema、业务服务或运行时接入。

已完成代码：

- 新增 `include/liteim/storage/StorageTypes.hpp` 和 `IStorage.hpp`。
- 新增 `include/liteim/cache/CacheTypes.hpp` 和 `ICache.hpp`。
- 新增 `src/storage/CMakeLists.txt` 和 `src/cache/CMakeLists.txt`，以 header-only interface target 暴露接口层。
- `src/CMakeLists.txt` 接入 `storage` / `cache`。
- `tests/CMakeLists.txt` 接入 Step 21 测试并链接 `liteim_storage` / `liteim_cache`。

新增测试：

- `StorageInterfaceTest.HeaderIsSelfContained`
- `StorageInterfaceTest.CanBeImplementedByFakeStorage`
- `CacheInterfaceTest.HeaderIsSelfContained`
- `CacheInterfaceTest.NullCacheCanBeUsedAsTestDouble`

已完成文档同步：

- README 增加 `liteim_storage` / `liteim_cache` 模块说明和目录布局。
- 新增 `tutorials/step21_storage_cache_interfaces.md`。
- 更新 `task_plan.md`、`findings.md` 和本文件。

验证：

- `cmake --build build`：通过。
- `git diff --check`：通过。
- `ctest --test-dir build -R "(StorageInterfaceTest|CacheInterfaceTest)" --output-on-failure`：4/4 通过。
- `ctest --test-dir build --output-on-failure`：181/181 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(storage): define storage and cache interfaces
```

## 2026-05-10 Markdown Documentation Alignment

本次只做 Markdown 文档对齐，目标是把当前面向读者的文档同步到 `9dd671b refactor(net): simplify owner loop cleanup and session state` 后的代码状态。

已修改：

- `tutorials/step09_reactor_interfaces.md`：删除当前 `EventLoop` 接口说明里的 `isStopped()`，补齐真实成员字段，并说明 `EventLoop` 不再暴露 stopped 查询给外部清理路径。
- `docs/debug_cases/net_lifecycle_review_hardening.md`：保留 2026-05-09 第一轮 hardening 的历史记录，但明确当时的跨线程 `Acceptor::close()` 契约已被后续 owner-loop-only cleanup 取代，并更新面试回答。
- `tutorials/step16_tcp_server.md`：把“不做心跳超时 / 输出高水位 / sendToUser”限定为 Step 16 当时边界，并说明后续 Step 的当前状态。
- `tutorials/step14_session.md`：补充 heartbeat timeout 已在 Step 18 通过 `timerfd` / `TimerManager` 接入，避免继续写成当前后续项。
- `task_plan.md`、`findings.md`、`progress.md`：顶部追加本次 Markdown alignment 过程记录。

已复查但未修改：

- `README.md`
- `tutorials/step12_event_loop.md`
- `tutorials/step13_acceptor.md`
- `tutorials/step20_backpressure.md`
- `/home/yolo/jianli/AGENTS.md`
- `/home/yolo/jianli/CLAUDE.md`
- `/home/yolo/jianli/PROJECT_MEMORY.md`

验证：

- `git diff --check`：通过。
- `rg -n 'isStopped\(|loop_exited_|kSessionOutputHighWaterMark|Session::fd\(|TcpServer::sendToUser\(' include src tests server`：无输出，源码旧 API 残留检查通过。
- Markdown 漂移扫描：仍命中的内容是本次新增的当前说明、AGENTS/CLAUDE/PROJECT_MEMORY 的允许规则，以及 `findings.md` / `progress.md` / debug case 中明确作为历史记录保留的旧阶段流水；未发现 README 或当前教程继续把旧 API 写成当前事实。
- 本次未运行 `cmake --build build` 或 `ctest`，因为没有修改 C++、CMake 或测试文件。

提交信息：

```text
docs(net): align markdown with current lifecycle APIs
```

## 2026-05-10 Network Lifecycle/API Cleanup

本次按用户给定计划执行独立网络层 cleanup/refactor，不命名为 Step 20.1，不进入 Step 21。

概念边界：

- `Acceptor::close()` 统一为 owner-loop-only，直接非 owner 调用终止进程。
- `EventLoop` 删除 `isStopped()`，生命周期清理由 owner-loop-only API 契约保证。
- public API 只保留当前真实可用的能力：删除 `Session::fd()`、`TcpServer::sendToUser()` 占位接口和 `kSessionOutputHighWaterMark` 兼容别名。
- `SessionState` 收敛启动/关闭状态，但保留 `closed_` 给 `TcpServer` heartbeat/base loop 跨线程读关闭状态。
- `sendToSession()` 查表失败、空 session、调用时已关闭 session 都返回 `NotFound`。

已完成代码：

- `Acceptor::close()` 删除跨线程 promise/future/wait fallback，非 owner loop 线程调用 `std::terminate()`。
- `EventLoop` 删除 `loop_exited_` 和 `isStopped()`；`LoopingGuard` 只管理 `looping_`。
- `Session` 新增 `SessionState`，`startInLoop()` 只允许 `kNew -> kStarted`，读写发送只在 `kStarted` 下工作，`closeInLoop()` 推进到 `kClosing/kClosed`。
- `Session` 删除 public `fd()`，默认高水位只保留 `kSessionDefaultOutputHighWaterMark`。
- `TcpServer` 删除 `sendToUser()`，并让 `sendToSession()` 对空/已关闭 session 返回 `NotFound`。

新增/更新测试：

- `AcceptorTest.CloseFromNonOwnerThreadTerminates`
- `TcpServerTest.SlowClientAccumulatedSmallPacketsTriggerHighWaterMark`
- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `ReactorInterfaceTest.TcpServerHeaderIsSelfContained`
- 删除旧的跨线程 `Acceptor::close()` 成功返回测试、`EventLoop::isStopped()` 测试、`sendToUser()` 测试。

已完成文档同步：

- 更新 `/home/yolo/jianli/AGENTS.md` 和 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Acceptor owner-loop-only 口径。
- 更新 README、Step 12/13/14/16/20 教程、网络生命周期 debug case、`task_plan.md`、`findings.md` 和本文件。

验证：

- `cmake --build build`：通过。
- `ctest --test-dir build -R "Acceptor|EventLoop|Session|TcpServer|ReactorInterface" --output-on-failure`：59/59 通过。
- `ctest --test-dir build --output-on-failure`：177/177 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
refactor(net): simplify owner loop cleanup and session state
```

## 2026-05-09 Optional Step 18.6 Session Input Path Simplification

本次按用户确认执行 `Optional Step 18.6: Session 输入路径简化`，目标是删除 `Session` 输入路径里多余的一层 `input_buffer_`。

概念边界：

- `FrameDecoder` 已经维护 TCP 半包 / 粘包所需的内部缓存。
- `Session` 输入路径改为 `socket read -> FrameDecoder -> Packet callback`。
- 只有成功解出完整入站 `Packet` 后才刷新 `last_active_time`。
- 出站写仍不刷新 heartbeat 活跃时间。
- 本次不改 output buffer、不改 Step 20 高水位回压、不改 `Session` 状态机、不改 Packet/TLV wire format。

已完成代码：

- 删除 `Session::input_buffer_` 成员。
- 删除 `feedInputBuffer()`。
- `Session::handleRead()` 在 `read() > 0` 后直接调用 `decoder_.feed(buffer.data(), n, packets)`。
- `handleRead()` 继续保持 `EINTR` retry、`EAGAIN/EWOULDBLOCK` 返回、`n == 0` 关闭、解码失败关闭。
- `closeInLoop()` 不再清理输入 buffer，只清理输出 buffer。

新增测试：

- `SessionTest.SplitPacketAcrossReadsInvokesCallbackAfterSecondRead`
- `SessionTest.MalformedPacketClosesSession`

已完成文档同步：

- 更新 Step 14 Session 教程，把读路径改为直接喂 `FrameDecoder`。
- 更新 Step 7 Buffer 教程，明确输入半包/粘包缓存由 `FrameDecoder` 维护，`Buffer` 主要用于输出缓冲。
- 更新 `task_plan.md`、`findings.md` 和 `progress.md`。

验证：

- `cmake --build build && ctest --test-dir build -R "SessionTest|TcpServerTest|FrameDecoderTest" --output-on-failure`：37/37 通过。
- `ctest --test-dir build --output-on-failure`：183/183 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。

提交信息：

```text
refactor(net): simplify session input decoding path
```

## 2026-05-09 Step 20 Slow-Client Backpressure Hardening

本次进入 `Step 20: 完善慢客户端回压保护`，目标是在已有默认 4MB 超限关闭基础上，把输出缓冲区高水位整理为可配置、可观测、可测试的策略。

概念边界：

- `Session` 的输出高水位默认仍是 4MB。
- 每次服务端发送编码后的 Packet 前，在 owner loop 中检查 `pending output + incoming encoded bytes`。
- 超过高水位时记录 warning 日志，并关闭该 `Session`。
- `TcpServer` 创建新 `Session` 时把 server 级高水位传入连接。
- `Config` 新增 `server.output_high_water_mark_bytes` 配置键。
- `pendingOutputBytes()` 仍然只能 owner-loop 查询。
- 本次不做暂停读、低水位恢复、消息优先级丢弃、群聊广播优化、`Session::input_buffer_` 简化或状态机重构。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试先失败在 `Config::session_output_high_water_mark` 等接口缺失，证明测试覆盖了 Step 20 缺失能力。

已完成代码：

- `Config` 增加 `session_output_high_water_mark`，默认 `4 * 1024 * 1024`。
- `Config::loadFromFile()` 支持 `server.output_high_water_mark_bytes`，并拒绝 0。
- `Session` 增加 `kSessionDefaultOutputHighWaterMark`、`output_high_water_mark_`、`setOutputHighWaterMark()` 和 `outputHighWaterMark()`。
- `Session::sendEncodedInLoop()` 改为使用实例级高水位；超限时记录 `pending`、`incoming`、`limit` 并 `closeInLoop()`。
- `TcpServer` 增加 `setSessionOutputHighWaterMark()`，要求 base loop 线程、启动前调用、阈值大于 0。
- `TcpServer::createSessionInLoop()` 在 I/O loop 中创建 `Session` 后设置输出高水位。
- `server/main.cpp` 把 `Config` 中的输出高水位传入 `TcpServer`。

新增/更新测试：

- `ConfigTest.ZeroHighWaterMarkFails`
- `SessionTest.DefaultOutputHighWaterMarkIsFourMegabytes`
- `SessionTest.RejectsZeroOutputHighWaterMark`
- `SessionTest.CloseWhenPendingOutputExceedsConfiguredHighWaterMark`
- `TcpServerTest.NormalClientDoesNotTriggerConfiguredHighWaterMark`
- `TcpServerTest.RejectsZeroSessionOutputHighWaterMark`
- `TcpServerTest.SessionOutputHighWaterMarkMustBeSetBeforeStart`
- `TcpServerTest.SlowClientIsClosedWhenOutputExceedsHighWaterMark`
- `TcpServerTest.ClosedSlowClientIsRemovedFromSessionTable`
- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `ReactorInterfaceTest.TcpServerHeaderIsSelfContained`

已完成文档同步：

- 更新 README，写入可配置输出高水位和配置键。
- 更新 Step 2 Config 教程，补充新字段、新 key 和 0 值校验。
- 更新 Step 14 Session 教程，把固定 4MB 文案改为 Step 20 可配置高水位。
- 新增 `tutorials/step20_backpressure.md`。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 中 Step 20 的配置键说明。
- 更新 `task_plan.md`、`findings.md` 和 `progress.md`。

最终验证：

- `cmake --build build && ctest --test-dir build -R "(ConfigTest|SessionTest|TcpServerTest|ReactorInterfaceTest)" --output-on-failure`：42/42 通过。
- `cmake --build build && ctest --test-dir build --output-on-failure`：181/181 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。

提交信息：

```text
feat(net): add session high water mark backpressure
```

## 2026-05-09 Session Activity Semantics Fix

本次只处理 P0：`last_active_time` 出站刷新语义错误。

问题：

- 设计语义应为“客户端发来完整、合法、成功解码的入站 Packet 才算连接活跃”。
- 旧实现里 `Session::sendEncodedInLoop()` append output buffer 后会刷新活跃时间。
- 旧实现里 `Session::handleWrite()` 每次写出字节后也会刷新活跃时间。
- 这会导致服务端持续 push / echo / Bot 回复时，沉默客户端被误判为活跃连接。

TDD RED 已确认：

```bash
cmake --build build && ctest --test-dir build -R "ServerWritesDoNotRefreshHeartbeatActivity" --output-on-failure
```

新增 `TcpServerTest.ServerWritesDoNotRefreshHeartbeatActivity` 后先失败：`closed_by_heartbeat` 为 `false`，证明服务端持续写会错误续期连接。

已完成代码：

- 删除 `Session::sendEncodedInLoop()` 中的出站 `updateLastActiveTime()`。
- 删除 `Session::handleWrite()` 中的出站 `updateLastActiveTime()`。
- 保留 `feedInputBuffer()` 成功解出完整入站 Packet 后的 `updateLastActiveTime()`。

已完成文档同步：

- 更新 Step 14 教程，把 `last_active_time` 语义改为完整入站 Packet 活跃时间。
- 更新 Step 18 教程，明确服务端出站写不续期。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 中 Step 18 的活跃时间描述。
- 更新 `findings.md` 和 `task_plan.md` 记录 P0 修复边界。

边界：

- 不做 Step 20 慢客户端回压完善。
- 不配置 high water mark。
- 不删除 `Session::input_buffer_`。
- 不重构 `Session` 状态机。
- 不自动 commit，避免混入已有未提交的文档边界修正改动。

最终验证：

- `cmake --build build && ctest --test-dir build -R "ServerWritesDoNotRefreshHeartbeatActivity" --output-on-failure`：修复后通过。
- `cmake --build build && ctest --test-dir build -R "Session|TcpServer" --output-on-failure`：20/20 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `ctest --test-dir build --output-on-failure`：172/172 通过。
- `git diff --check`：通过。

## 2026-05-09 Documentation Boundary Correction

本次只修改 Markdown，目标是把文档职责重新分清：

- `/home/yolo/jianli/PROJECT_MEMORY.md` 只保留总设计、长期路线、架构约束、Step 目标、边界、测试要求和计划 commit message。
- `/home/yolo/jianli/AGENTS.md` 和 `/home/yolo/jianli/CLAUDE.md` 只保留 agent 约束和读取顺序，不记录 Step 完成状态、实际提交 hash 或活动下一步。
- `LiteIM/README.md` 继续作为对外说明文档，不记录过程进度。
- `task_plan.md`、`findings.md`、`progress.md` 负责进度、发现、验证结果和过程记忆。

已完成：

- 删除 `PROJECT_MEMORY.md` 顶部的优先级、完成状态、真实状态、最近提交、默认下一步等过程信息。
- 将 Step 18、Step 18.5、Step 19 段落改回路线层描述，删除实际完成 hash 和完成验证命令。
- 清理 `AGENTS.md` / `CLAUDE.md` 中的当前 Step 状态和默认下一步描述。
- 将 README 的 `Current Modules` 改为 `Core Components`，移除 `currently/current implementation` 这类进度措辞。
- 在 `task_plan.md`、`findings.md`、`progress.md` 靠前位置记录新的文档职责边界。

验证：

- 本次不改 C++、CMake 或测试代码，不需要重新编译。
- `planning-with-files` 的 session catchup 仍提示旧的纯概念问答未同步；该上下文不对应本次文档修改。
- `rg` 扫描确认 `AGENTS.md` / `CLAUDE.md` 不再包含当前状态、默认下一步或 Step 18/19 完成状态措辞。
- `rg` 扫描确认 `PROJECT_MEMORY.md` 不再包含顶部优先级、最近提交、完成验证、完成提交或默认下一步等过程记录措辞。
- `rg` 扫描确认 `README.md` 不再包含 `Current Status`、`当前状态`、`Current Modules`、`current` 或 `currently`。
- `git diff --check` 通过；额外 trailing whitespace 扫描对目标 Markdown 无输出。
- `git status --short` 显示 LiteIM Git 仓库内只修改 `README.md`、`task_plan.md`、`findings.md`、`progress.md`；根目录的 `PROJECT_MEMORY.md`、`AGENTS.md`、`CLAUDE.md` 位于 LiteIM 仓库外。

## 2026-05-09 Step 19 Signalfd Graceful Shutdown

本次进入 `Step 19: signalfd graceful shutdown`，目标是让 `liteim_server` 收到 `SIGINT` / `SIGTERM` 后走 Reactor 内部优雅退出路径。

概念边界：

- 信号不在传统 signal handler 中清理资源。
- `SIGINT` / `SIGTERM` 通过 `signalfd` 变成 fd 可读事件。
- signal callback 在 base `EventLoop` 线程执行。
- callback 中先 `server.stop()`，再 `loop.quit()`。
- `SignalWatcher` 和 `TcpServer` 一样遵守 owner-loop-only stop/destruct 规则。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试后先失败于 `fatal error: liteim/net/SignalWatcher.hpp: No such file or directory`，证明测试覆盖了 Step 19 缺失接口。

已完成代码：

- 新增 `include/liteim/net/SignalWatcher.hpp`。
- 新增 `src/net/SignalWatcher.cpp`。
- `SignalWatcher` 使用 `pthread_sigmask()`、`signalfd()`、`Channel` 和 `EventLoop` 把信号接入 Reactor。
- `SignalWatcher::start()` 要求 owner loop 线程调用，失败返回 `Status`。
- `SignalWatcher::stop()` 和析构要求 owner loop 线程调用，非 owner 线程直接 stop 会 `std::terminate()`。
- `server/main.cpp` 创建 `SignalWatcher(SIGINT, SIGTERM)`，启动后再启动 `TcpServer`，收到信号后停止 server 并退出主 loop。
- `src/net/CMakeLists.txt` 已把 `SignalWatcher.cpp` 编入 `liteim_net`。

新增测试：

- `tests/net/signal_watcher_header_test.cpp`
- `tests/net/signal_watcher_test.cpp`
- `tests/server_signal_shutdown_test.sh`
- `LiteIMServerSignalTest.TerminatesOnSigterm` CTest。

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build -R "SignalWatcher|LiteIMServerSignal" --output-on-failure
```

结果：Step 19 相关 4/4 tests passed。

最终验证已完成：

- `cmake -S . -B build && cmake --build build` 通过。
- `ctest --test-dir build -R "SignalWatcher|LiteIMServerSignal" --output-on-failure` 通过，4/4。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124` 通过，服务端收到 SIGTERM 后记录 graceful shutdown 日志。
- `ctest --test-dir build --output-on-failure` 通过，171/171。
- `git diff --check` 无输出。
- `.gitkeep` 和旧 SQLite / `InMemoryStorage` / old `server/net` 路径扫描无输出。

提交状态：

- 将按 `PROJECT_MEMORY.md` 使用提交信息 `feat(server): add signalfd graceful shutdown` 完成 Step 19 commit。

## 2026-05-09 PROJECT_MEMORY Markdown Alignment

本次按新版 `/home/yolo/jianli/PROJECT_MEMORY.md` 同步其余 Markdown，不改 C++ 代码。

已完成：

- 更新 `/home/yolo/jianli/AGENTS.md`，把它作为未来 agent 的约束文件：按当时进度写入 Step 18 / Step 18.5 已完成、后续进入 Step 19 `signalfd graceful shutdown`，并补充 muduo-style owner-loop 生命周期规则。
- 更新 `/home/yolo/jianli/CLAUDE.md`，同步当前进度、docs 边界、owner-loop 约束和 `docs/debug_cases/` 保留规则。
- 将 `docs/debug_cases/net_lifecycle_review_hardening.md` 从英文完整改写为中文，保留复盘背景、已接受 bug、不采纳项、验证命令和面试回答。
- 更新 `task_plan.md` 当前阶段，补上 Step 18.5 已完成记录和本轮 Markdown alignment 记录。
- 更新 `findings.md` 权威来源和文档边界，避免旧的 “从 Step 0 重新开始” 口径压过当前真实进度。

边界：

- 没有修改 C++ 源码、CMake 或测试代码。
- 没有恢复 `docs/architecture.md`、`docs/project_layout.md`、`docs/roadmap.md` 或 `tutorials/README.md`。
- 没有把 Optional Step 18.6 / Step 18.7 变成 Step 19 前置条件。

验证：

- `find /home/yolo/jianli -maxdepth 1 -name 'PROJECT_MEMORY*.md' -printf '%f\n' | sort` 只输出 `PROJECT_MEMORY.md`。
- `find docs -type f | sort` 只输出两个 debug case 文档。
- `find tutorials -maxdepth 1 -type f -name 'README.md' -print` 无输出。
- 合并残留和旧入口文案扫描无输出。
- `rg -n "Current Status|当前状态" README.md` 无输出。
- `git diff --check` 无输出。

## 2026-05-09 Muduo-style Lifecycle Hardening

本次根据外部 review 做一轮小范围 hardening，目标是让 Reactor 对象生命周期更接近 muduo：对象在哪个 loop 线程拥有，就在哪个 loop 线程停止和析构。

范围：

- 修复 `TcpServer::stop()` 非 owner 线程捕获裸 `this` 异步投递的 UAF 风险。
- 修复 `TimerManager::stop()` 同类非 owner 线程裸 `this` 异步投递风险。
- 把 `Session` 构造函数从裸 `int fd` 改为接收 `UniqueFd`，让 accepted fd 所有权全程由 RAII 类型表达。
- 让 `Session::pendingOutputBytes()` 先检查 owner loop 线程，避免跨线程读 `output_buffer_`。
- 把 `server/main.cpp` 从 scaffold 改为真实启动 `EventLoop + TcpServer` echo server。

TDD RED：

- `cmake --build build` 先失败在 `ReactorInterfaceTest.SessionHeaderIsSelfContained` 的三个 static_assert：`Session(EventLoop*, UniqueFd, id)` 尚不存在、裸 `int fd` 构造仍存在、`pendingOutputBytes()` 仍是 `noexcept`。

代码完成：

- `TcpServer::~TcpServer()` 和 `TcpServer::stop()` 改为 owner-loop-only；非 owner 线程直接调用 `stop()` 会 `std::terminate()`。
- `TimerManager::~TimerManager()` 和 `TimerManager::stop()` 改为 owner-loop-only；非 owner 线程直接调用 `stop()` 会 `std::terminate()`。
- `TcpServer::createSessionInLoop()` 直接把 accepted `UniqueFd` move 给 `Session`，删除裸 fd `release()` 串联。
- `Session(EventLoop*, UniqueFd, id)` 在构造体内接管 fd，构造失败也由 `UniqueFd` 自动关闭 fd。
- `Session::pendingOutputBytes()` 去掉 `noexcept` 并调用 `loop_->assertInLoopThread()`。
- `server/CMakeLists.txt` 让 `liteim_server` 链接 `liteim_net`；`server/main.cpp` 创建 `EventLoop` 和 `TcpServer`，启动后进入 `loop()`。

新增/更新测试：

- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `SessionTest.PendingOutputBytesRequiresOwnerLoopThread`
- `TcpServerTest.StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis`
- `TimerManagerTest.StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis`

验证完成：

- `cmake -S . -B build` 通过。
- `cmake --build build` 通过。
- `ctest --test-dir build -R "(Session|TcpServer|TimerManager)" --output-on-failure` 通过，23/23。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124` 通过，输出 `LiteIM server is listening on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure` 通过，167/167。
- `git diff --check` 无输出。
- `.gitkeep` 和旧 SQLite / `InMemoryStorage` / old `server/net` 路径扫描无输出。

## 2026-05-09 Step 18 TimerManager

本次进入 `Step 18: implement TimerManager + timerfd heartbeat timeout`。

概念边界：

- `TimerHeap` 只负责 timer id、过期时间、小根堆、回调保存、取消标记和 lazy deletion。
- `TimerManager` 只负责把 Linux `timerfd` 接入 `EventLoop`，读取 tick 计数并触发已过期 timer。
- `TcpServer` 本轮只接入服务端 idle session cleanup：每 5 秒检查一次，90 秒未活跃连接关闭。
- `Session` 收到完整 `Packet` 后刷新活跃时间；后续真正的心跳协议、登录态续期、Redis 在线状态和 `HeartbeatService` 留到后续 Step。

准备新增 RED 测试：

- `tests/timer/timer_heap_header_test.cpp`
- `tests/timer/timer_heap_test.cpp`
- `tests/timer/timer_manager_header_test.cpp`
- `tests/timer/timer_manager_test.cpp`
- `tests/net/tcp_server_test.cpp` 增加 idle close / active survive 回归测试。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试后先失败于 `TcpServer::setHeartbeatOptions` 接口不存在，证明测试覆盖到了 Step 18 的 TcpServer 心跳超时配置入口。

已完成代码：

- 新增 `include/liteim/timer/TimerHeap.hpp` 和 `src/timer/TimerHeap.cpp`。
- 新增 `include/liteim/timer/TimerManager.hpp` 和 `src/timer/TimerManager.cpp`。
- 新增 `src/timer/CMakeLists.txt`，把 timer 源码编进 `liteim_net`，避免 `TimerManager` 依赖 `EventLoop` 时形成库循环依赖。
- `TimerHeap` 支持 one-shot timer、取消、过期回调、小根堆顺序和 lazy deletion。
- `TimerManager` 使用 `timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)`，并通过 `Channel` 注册到 owner `EventLoop`。
- `TcpServer` 增加 `setHeartbeatOptions()`，默认 5 秒检查一次、90 秒未活跃关闭。
- `TcpServer` 在 base loop 上创建 `TimerManager`，每轮扫描 session 快照，过期连接通过 `Session::close()` 回到所属 I/O loop。
- `Session` 调整为收到完整 `Packet` 后刷新 `last_active_time`。

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Timer|TcpServerTest\\.(IdleSessionIsClosedByHeartbeatTimeout|ActiveSessionSurvivesHeartbeatTimeout)|ReactorInterfaceTest.TcpServerHeaderIsSelfContained"
```

结果：10/10 Step 18 相关测试通过。

全量测试已通过：

```bash
ctest --test-dir build --output-on-failure
```

结果：164/164 tests passed。

文档同步：

- README、docs/architecture.md、docs/project_layout.md、docs/roadmap.md、tutorials/README.md、Step 18 tutorial、task_plan、findings、progress 和 PROJECT_MEMORY 已同步 Step 18 结果。

最终验证已通过：

```bash
cmake -S . -B build && cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：configure/build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 164/164 通过；`git diff --check` 无输出；`.gitkeep` 检查无输出；旧路线路径检查无输出。

提交状态：

- 将按 `PROJECT_MEMORY.md` 使用提交信息 `feat(timer): integrate timerfd heartbeat timeout` 完成 Step 18 commit。

## 2026-05-09 Network Review Hardening

本次根据外部 review 核对 Step 17 后的网络/并发代码，只接受能在当前代码中证实、并且能用回归测试固定住的问题。

已接受并修复：

- `EventLoopThread::stop()` 自线程调用不再 detach，也不在 `stop()` 中清空 `loop_` / `running_`；状态清理统一放到 `threadFunc()` 末尾，owner 线程后续负责 join。
- `EventLoopThread` 增加 `thread_id_` 和 `join_started_`，避免 self-stop / owner-stop 边界上并发访问同一个 `std::thread` join 状态。
- `Session` 新增 `kSessionOutputHighWaterMark = 4MB`，append 输出缓冲前先检查高水位，超限直接关闭连接。
- `Session` 增加逻辑 `id()`；`TcpServer` 使用自增 `std::uint64_t next_session_id_` 作为 `sessions_` key，不再用 fd 当 session id。
- `TcpServer::sendToSession()` 接口改为 `std::uint64_t session_id`。
- `Channel::handleEvent()` 处理 `EPOLLERR` 后不再立即返回；如果同一轮还有可读事件，会继续执行 read callback。
- `Acceptor::close()` 保留跨线程 close 契约，但在 close task 已投递、loop 却先退出的竞态下不再永久等待，会检测 `isStopped()` 后走 fallback 清理。

TDD RED 已确认：

```bash
cmake --build build
```

新增/修改测试后先失败于 `TcpServer::sendToSession` 接口静态断言，证明测试覆盖到了 fd-as-id 到逻辑 session id 的 API 变化。

新增/更新测试：

- `ChannelTest.ErrorWithReadableEventInvokesErrorThenRead`
- `AcceptorTest.CloseFromOtherThreadWhileLoopExitsWithQueuedCloseDoesNotBlock`
- `EventLoopThreadTest.OwnerStopWaitsAfterStopIsRequestedInsideLoop`
- `SessionTest.CloseWhenPendingOutputExceedsHighWaterMark`
- `TcpServerTest.SendToSessionFromOtherThreadDeliversPacket` 改为使用 `session->id()`
- `ReactorInterfaceTest.TcpServerHeaderIsSelfContained` 改为校验 `sendToSession(std::uint64_t, Packet)`

本次不采用/暂不改：

- 不把 `Acceptor::close()` 改成 owner-loop-only 契约；当前项目保留跨线程 close，并修复 queued close 等不到执行的竞态。
- 不把 `ThreadPool::stop()` 改成 swap-and-join；当前 `stop_mutex_` 已有并发 stop 回归测试，worker 内部 delete pool 仍属于不支持的对象生命周期错误，detach 不能真正修好。
- 不把 `Session` 状态 bool、`EventLoop` flag、`FrameDecoder` buffer 结构等风格项混入本次 bugfix；这些需要单独测试和边界讨论。

文档同步：

- README、docs/architecture.md、docs/project_layout.md、docs/roadmap.md、Step 11/13/14/15/16 tutorials、task_plan、findings、progress 和 PROJECT_MEMORY 已同步本轮 hardening。
- 新增 `docs/debug_cases/net_lifecycle_review_hardening.md` 记录复盘过程、修复点、不采纳项和面试回答。

验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/server/liteim_server
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：build 通过；CTest 155/155 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；`git diff --check` 无输出；`.gitkeep` 检查无输出；旧路线路径检查无输出。

## 2026-05-08 Step 17 Business ThreadPool

本次进入 `Step 17: implement business ThreadPool`，目标是实现独立业务线程池，让后续 MySQL / Redis / 密码哈希 / 历史消息查询不阻塞 I/O loop。

概念边界：

- I/O 线程继续只负责 socket 读写、Packet/TLV 编解码和连接生命周期。
- 业务线程池负责执行可能阻塞或较重的业务任务。
- 业务线程不能直接修改 `Session` 内部状态；响应必须通过 `Session::sendPacket()` 或 `EventLoop::queueInLoop()` 回到连接所属 I/O loop。
- 第一版 `ThreadPool` 不提供 `future`、任务优先级、动态扩缩容或 work stealing。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试先失败于 `fatal error: liteim/concurrency/ThreadPool.hpp: No such file or directory`，证明测试覆盖了当前 Step 缺失的业务线程池接口。

已完成代码：

- 新增 `include/liteim/concurrency/ThreadPool.hpp`。
- 新增 `src/concurrency/CMakeLists.txt`。
- 新增 `src/concurrency/ThreadPool.cpp`。
- `ThreadPool::start()` 创建固定数量 worker，拒绝 0 worker 和重复启动。
- `ThreadPool::submit()` 把任务放入 `deque<Task>`，通过 `condition_variable` 唤醒 worker；未运行或空任务会返回 `InvalidArgument`。
- `ThreadPool::workerLoop()` 等待任务、取出任务并执行；任务抛异常时会被捕获，worker 继续处理后续任务。
- `ThreadPool::stop()` 停止接收新任务，唤醒 worker，并等待已入队任务执行完成后退出。
- `ThreadPool` 内部状态已收敛为单一 `running_`；`workerLoop()` 在 `!running_ || !tasks_.empty()` 时被唤醒，stop 后会先 drain 队列再退出。
- `ThreadPool::stop()` 如果从 worker 任务内部调用，会通过 worker-local 标记识别 self-stop，只发出停止请求并返回，等待 owner 线程后续清理。
- 外部多线程并发调用 `stop()` 时通过 `stop_mutex_` 串行化 join/cleanup，避免同时 join 同一个 `std::thread`。
- `pendingTaskCount()` 返回仍在队列中等待执行的任务数量。
- `src/CMakeLists.txt` 已接入 `concurrency` 模块；`tests/CMakeLists.txt` 已链接 `liteim_concurrency`。

新增测试：

- `tests/concurrency/thread_pool_header_test.cpp`
- `tests/concurrency/thread_pool_test.cpp`
- `ConcurrencyInterfaceTest.ThreadPoolHeaderIsSelfContained`
- `ThreadPoolTest.StartRejectsZeroWorkers`
- `ThreadPoolTest.SubmitExecutesTask`
- `ThreadPoolTest.MultipleWorkersRunConcurrently`
- `ThreadPoolTest.StopRejectsNewTasks`
- `ThreadPoolTest.StopCalledFromWorkerRequiresOwnerCleanupBeforeRestart`
- `ThreadPoolTest.ConcurrentStopCallsAreSerialized`
- `ThreadPoolTest.DestructorWaitsForQueuedTasks`
- `ThreadPoolTest.PendingTaskCountTracksQueuedTasks`

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "ThreadPool|ConcurrencyInterfaceTest.ThreadPool"
```

结果：9/9 Step 17 tests passed。该正则同时匹配了历史 `EventLoopThreadPool` 测试，CTest 输出中共运行 13 个测试，全部通过。

已完成文档同步：

- README、docs/architecture.md、docs/project_layout.md、docs/roadmap.md、tutorials/README.md、Step 17 tutorial、task_plan、findings、progress 和 PROJECT_MEMORY 已同步 Step 17 结果。
- 新增并更新 `docs/debug_cases/thread_pool_worker_stop.md`，记录 worker 内部调用 `ThreadPool::stop()`、并发外部 `stop()`、`running_` 状态收敛、RED 测试、修复设计、验证命令和面试回答。

全量验证已通过：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 151/151 通过；`git diff --check` 无输出；`.gitkeep` 检查无输出；旧路线路径检查无输出。

提交状态：

- 已按 `PROJECT_MEMORY.md` 使用提交信息 `feat(concurrency): add business thread pool` 完成 Step 17 commit。

## 2026-05-08 Step 16 TcpServer

本次进入 `Step 16: implement TcpServer multi-Reactor version`，目标是实现第一个多 Reactor echo server。

概念边界：

- base `EventLoop` 持有 `Acceptor`，负责监听和 accept。
- `EventLoopThreadPool` 提供子 I/O loops，新连接按 round-robin 分配。
- `Session` 在被选中的 I/O loop 中创建，后续读写固定在该 loop。
- `TcpServer` 维护线程安全的 `sessions_` 表，并提供 `sendToSession()` / 基础 `sendToUser()`。
- 第一版业务只做 echo，不进入 business `ThreadPool`、MySQL、Redis、登录态或 MessageRouter。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试先失败于 `fatal error: liteim/net/TcpServer.hpp: No such file or directory`，证明测试覆盖了当前 Step 缺失的 `TcpServer` 接口。

已完成代码：

- 新增 `include/liteim/net/TcpServer.hpp`。
- 新增 `src/net/TcpServer.cpp`。
- `TcpServer::start()` 在 base loop 线程启动 I/O 线程池、创建 `Acceptor` 并记录实际端口。
- `Acceptor` accept 到的 `UniqueFd` 由 `TcpServer` 分配到子 I/O loop。
- `TcpServer::createSessionInLoop()` 在目标 loop 中创建 `Session`、设置 message/close callback、写入 `sessions_` 表并启动读事件。
- 默认 message callback 为空时执行 echo。
- `sendToSession()` 支持从其他线程调用，实际发送仍回到 session 所属 loop。
- 当时曾保留用户发送占位接口并返回 `NotFound`；2026-05-10 cleanup 已删除该占位 API，等待后续登录态和 user-session 绑定实现真实用户路由。
- `src/net/CMakeLists.txt` 已接入 `TcpServer.cpp`。

新增测试：

- `tests/net/tcp_server_header_test.cpp`
- `tests/net/tcp_server_test.cpp`
- `ReactorInterfaceTest.TcpServerHeaderIsSelfContained`
- `TcpServerTest.EchoesPacketToClient`
- `TcpServerTest.DistributesConnectionsAcrossIoLoops`
- `TcpServerTest.RemovesSessionAfterClientDisconnects`
- `TcpServerTest.SendToSessionFromOtherThreadDeliversPacket`
- `TcpServerTest.SendToUnknownUserReturnsNotFound`

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "TcpServer|ReactorInterfaceTest.TcpServer"
```

结果：6/6 Step 16 tests passed。

已完成文档同步：

- README、docs/architecture.md、docs/project_layout.md、docs/roadmap.md、tutorials/README.md、Step 16 tutorial、task_plan、findings、progress 和 PROJECT_MEMORY 已同步 Step 16 结果。

全量验证已通过：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 142/142 通过；`git diff --check` 无输出；`.gitkeep` 检查无输出；旧路线路径检查无输出。

提交状态：

- 已按 `PROJECT_MEMORY.md` 使用提交信息 `feat(net): implement multi reactor tcp server` 完成 Step 16 commit。

## 2026-05-08 Pre-Step 16 Code Cleanup

本次继续上轮未完成收尾，目标是在 `Step 16: implement TcpServer multi-Reactor version` 前完成接口和代码卫生清理，不启动 TcpServer 实现。

已确认采用的清理项：

- `Epoller::owner_loop_` 不能闲置，必须用于检查 `Channel` 是否属于同一个 `EventLoop`。
- Packet/TLV 的大端读写 helper 有明确复用价值，应抽到协议层通用头文件。
- `Acceptor::NewConnectionCallback` 应接收 `UniqueFd`，用类型表达 accepted fd 所有权。
- `Acceptor::listening_` 是重复状态，可以由 `listen_fd_` 推导。
- 测试里的自制 `FdGuard` 应替换成生产 `UniqueFd`。
- 生产代码中过长教学注释应移到教程，源码只保留必要契约说明。

已完成代码：

- 新增 `include/liteim/protocol/ByteOrder.hpp`，提供 `appendUint16BE()`、`appendUint32BE()`、`appendUint64BE()`、`readUint16BE()`、`readUint32BE()`、`readUint64BE()`。
- `src/protocol/Packet.cpp` 和 `src/protocol/TlvCodec.cpp` 改为复用 `ByteOrder.hpp`，删除重复 helper，wire format 不变。
- `Epoller::updateChannel()` / `removeChannel()` 新增 owner-loop 校验，跨 `EventLoop` 的 `Channel` 返回 `InvalidArgument`。
- `Acceptor::NewConnectionCallback` 改为 `std::function<void(UniqueFd, const sockaddr_in&)>`。
- `Acceptor::handleRead()` 使用 `std::move(accepted_fd)` 把 accepted fd 所有权交给 callback，不再靠裸 fd 和手动 `release()` 表达。
- `Acceptor::listening_` 已删除，`listening()` 直接检查 `listen_fd_`。
- `tests/net/acceptor_test.cpp` 和 `tests/net/socket_util_test.cpp` 删除自制 `FdGuard`，统一使用 `liteim::UniqueFd`。
- 生产代码里明显的教学型长注释已清理，保留 `Channel` callback 生命周期契约这类必要注释。

新增/更新测试：

- `tests/protocol/byte_order_test.cpp`
- `ByteOrderTest.AppendsUnsignedIntegersAsBigEndianBytes`
- `ByteOrderTest.ReadsUnsignedIntegersFromBigEndianBytes`
- `EpollerTest.RejectsChannelOwnedByDifferentEventLoop`
- `ReactorInterfaceTest.AcceptorHeaderIsSelfContained` 更新为 `UniqueFd` callback 签名。
- Acceptor callback 测试均改为接收 `UniqueFd`。

已完成文档同步：

- README、docs/architecture.md、docs/project_layout.md、docs/roadmap.md、Step 4/5/10/13/15 tutorials、task_plan、findings、progress 和 PROJECT_MEMORY 已同步本次清理。

验证结果：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

结果：build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 136/136 通过。

## 2026-05-08 Step 15 EventLoopThreadPool

本次进入 `Step 15: implement EventLoopThread and EventLoopThreadPool`，目标是实现 one-loop-per-thread 多 Reactor 的线程基础，不启动 Step 16 `TcpServer`。

概念边界：

- `EventLoopThread` 表示“一个线程拥有一个 `EventLoop`”。
- `EventLoopThreadPool` 表示“多个 I/O loops 的管理器”。
- 主 Reactor 后续继续负责 accept；子 Reactor 后续负责已连接 `Session` 的 I/O。
- 本 Step 不创建 `TcpServer`，不做连接分发，也不进入业务线程池、MySQL 或 Redis。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试先失败于 `fatal error: liteim/net/EventLoopThread.hpp: No such file or directory`，证明测试覆盖了当前 Step 缺失的线程接口。

已完成代码：

- 新增 `include/liteim/net/EventLoopThread.hpp` 和 `src/net/EventLoopThread.cpp`。
- 新增 `include/liteim/net/EventLoopThreadPool.hpp` 和 `src/net/EventLoopThreadPool.cpp`。
- `EventLoopThread::startLoop()` 启动工作线程，等待线程内 `EventLoop` 构造完成后返回观察指针。
- `EventLoopThread::stop()` 通过 `EventLoop::quit()` 唤醒并退出 loop，然后 join 线程；析构自动 stop。
- `EventLoopThreadPool::start()` 启动指定数量的子 loops。
- `EventLoopThreadPool::getNextLoop()` round-robin 返回子 loop；线程数为 0 时返回 base loop。
- `src/net/CMakeLists.txt` 已接入 Step 15 新源码。

新增测试：

- `ReactorInterfaceTest.EventLoopThreadHeaderIsSelfContained`
- `ReactorInterfaceTest.EventLoopThreadPoolHeaderIsSelfContained`
- `EventLoopThreadTest.StartLoopCreatesLoopOnWorkerThread`
- `EventLoopThreadTest.StopWithoutStartIsNoop`
- `EventLoopThreadTest.DestructorStopsRunningLoop`
- `EventLoopThreadPoolTest.StartCreatesRequestedNumberOfLoops`
- `EventLoopThreadPoolTest.GetNextLoopUsesRoundRobinOrder`
- `EventLoopThreadPoolTest.ZeroThreadsReturnsBaseLoop`
- `EventLoopThreadPoolTest.ChildLoopsRunTasksOnDistinctThreads`

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "EventLoopThread|EventLoopThreadPool|ReactorInterfaceTest.EventLoopThread"
```

结果：9/9 Step 15 tests passed。

全量验证已通过：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . \( -path './server/net' -o -path './server/protocol' -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 133/133 通过；`git diff --check` 无输出；旧路线路径检查无输出。

## 2026-05-08 Pre-Step 15 Byte/Bytes API Cleanup

本次在进入 `Step 15: implement EventLoopThread and EventLoopThreadPool` 前，先收口上次未完成的字节类型清理，目标是不让后续网络线程池和 `TcpServer` 继续扩散混用 `std::vector<char>` / `std::vector<std::uint8_t>` / `std::string_view` 的公共接口。

已完成代码：

- 新增 `include/liteim/base/Types.hpp`，定义 `liteim::Byte` 和 `liteim::Bytes`。
- `Packet`、`TlvCodec`、`FrameDecoder`、`Buffer`、`Session` 和相关 tests 已切换到 `Byte` / `Bytes`。
- `Buffer` 公共接口保留 `append(const Byte*, len)`、`append(const Bytes&)` 和 `append(const std::string&)`；`ensureWritableBytes()` 变成内部实现细节。
- `ErrorCode::toString()` 不再返回 `std::string_view`；`Logger::parseLogLevel()` 不再在公共接口接收 `std::string_view`。

已完成文档同步：

- README、docs、Step 4/5/6/7 tutorials、task_plan、findings、progress 和 PROJECT_MEMORY 已同步 `Byte` / `Bytes` API 说明。

验证结果：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . \( -path './server/net' -o -path './server/protocol' -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 124/124 通过；`git diff --check` 无输出；旧路线路径检查无输出；旧接口扫描只剩规则说明和本次清理记录中的说明性命中。

## 2026-05-07 Step 14 Session

本次进入 `Step 14: implement Session`，目标是实现单个已连接 fd 的生命周期和 Packet I/O，不启动 Step 15 多 Reactor 线程池。

概念边界：

- `Session` 是连接 owner，接管一个已连接 fd。
- `Acceptor` 只负责 accept，后续 `TcpServer` 会创建 `Session`；本 Step 不实现 `TcpServer`。
- `Session` 持有 `Channel`、输入 `Buffer`、输出 `Buffer` 和 `FrameDecoder`。
- I/O 线程负责非阻塞读写和协议编解码，不做 MySQL / Redis 阻塞业务。
- 跨线程 `sendPacket()` 通过 `EventLoop::queueInLoop()` 回到连接所属 loop。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试先失败于 `fatal error: liteim/net/Session.hpp: No such file or directory`，证明测试覆盖了当前 Step 缺失的 `Session` 接口。

已完成代码：

- 新增 `include/liteim/net/Session.hpp`。
- 新增 `src/net/Session.cpp`。
- `Session` 使用 `std::enable_shared_from_this`，`start()` 中调用 `Channel::tie()`。
- `handleRead()` 循环读到 `EAGAIN`，输入字节经过 `Buffer` 后交给 `FrameDecoder`。
- `sendPacket()` 编码 Packet，跨线程调用时投递回 owner `EventLoop`。
- `handleWrite()` 发送 output buffer，未写完的数据保留，写完后关闭写兴趣。
- `close()` / `closeInLoop()` 在 loop 线程移除 `Channel`、关闭 fd、清理缓冲并触发 close callback。
- `Channel` 对象释放延迟到当前事件回调栈帧之后，避免在 `Channel::handleEvent()` 正执行时析构当前 `Channel`。
- `last_active_time` 使用原子毫秒时间戳保存。
- `liteim_net` 链接 `liteim_protocol`，`src/CMakeLists.txt` 构建顺序调整为 base -> protocol -> net。

新增测试：

- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `SessionTest.CompletePacketInvokesMessageCallback`
- `SessionTest.HalfPacketDoesNotInvokeMessageCallback`
- `SessionTest.StickyPacketsInvokeCallbackForEachPacket`
- `SessionTest.PeerCloseInvokesCloseCallback`
- `SessionTest.SendPacketFromOtherThreadDeliversEncodedPacket`
- `SessionTest.LargePacketLeavesPendingOutputWhenPeerDoesNotRead`
- `SessionTest.LastActiveTimeIsInitialized`

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Session"
```

结果：8/8 Session tests passed。

全量验证已通过：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . \( -path './server/net' -o -path './server/protocol' -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：server scaffold 正常输出监听配置，124/124 tests passed，diff whitespace check 无输出，旧路线文件路径检查无输出。

## 2026-05-07 Step 13 Hardening Round 3

本次任务继续复核 Step 13 hardening round 2 后的审阅反馈，仍然限定在 `EventLoop` / `Acceptor` / `Channel` 网络层边界内，不启动 Step 14。

采纳并修复：

- `EventLoop::isStopped()` 从 `!looping_` 推断改为显式 `loop_exited_` 状态，区分“尚未启动”和“已经退出”，也覆盖 `quit()` 早于第一次 `loop()` 的场景。
- 新增 `EventLoopTest.IsStoppedIsFalseBeforeLoopEverStarts`，覆盖 loop 刚构造但尚未进入 `loop()` 的状态语义。
- 新增 `EventLoopTest.IsStoppedIsFalseAfterQuitBeforeLoopEverStarts`，覆盖 `quit()` 早于第一次 `loop()` 时不能误判 stopped。
- 新增 `EventLoopTest.LoopRunsQueuedTaskWhenQuitWasRequestedBeforeStart`，覆盖 `quit()` 早于第一次 `loop()` 时仍要先执行已排队清理任务。
- 新增 `AcceptorTest.CloseFromOtherThreadBeforeLoopStartsStillRemovesChannel`，覆盖 loop 尚未启动时跨线程 close 仍必须回到 owner loop 删除 listen channel。
- `Acceptor` fd 用尽 helper 保持 `noexcept`，但 warn 日志改走内部 no-throw wrapper，避免 `spdlog` 异常触发 `std::terminate()`。
- `Channel.hpp`、Step 11 教程和 findings 补充 callback 不复制后的契约：未 `tie()` 的 callback 不能销毁当前 `Channel` 或重置正在执行的 callback；可能释放 owner 的场景必须调用 `tie()`。
- `Acceptor::rejectOneConnectionAfterFdExhaustion()` 内联 idle fd 重建逻辑，删除只被单点调用的 private helper，让 fd 用尽后的“腾 fd、拒绝连接、补回 idle fd”流程连续可读。
- `Channel::handleEvent()` 合并事件分发 private helper，让 `tie()` 得到的局部 `shared_ptr` guard 和事件分发 body 保持在同一个栈帧，生命周期边界更直观。

定向 RED 已确认：

```bash
ctest --test-dir build --output-on-failure -R "EventLoopTest.IsStoppedIsFalseBeforeLoopEverStarts|AcceptorTest.CloseFromOtherThreadBeforeLoopStartsStillRemovesChannel"
ctest --test-dir build --output-on-failure -R "EventLoopTest.IsStoppedIsFalseAfterQuitBeforeLoopEverStarts"
ctest --test-dir build --output-on-failure -R "EventLoopTest.LoopRunsQueuedTaskWhenQuitWasRequestedBeforeStart"
```

旧实现下新增回归会失败：未启动状态和提前 `quit()` 状态下 `isStopped()` 返回 true，提前 `quit()` 后启动 loop 时 pending task 不执行，Acceptor close-before-loop-start 测试抛出 `fd already belongs to a different channel`。

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "EventLoopTest.IsStoppedIsFalseBeforeLoopEverStarts|EventLoopTest.IsStoppedIsFalseAfterQuitBeforeLoopEverStarts|EventLoopTest.LoopRunsQueuedTaskWhenQuitWasRequestedBeforeStart|AcceptorTest.CloseFromOtherThreadBeforeLoopStartsStillRemovesChannel"
ctest --test-dir build --output-on-failure -R "EventLoopTest|AcceptorTest|ChannelTest|LoggerTest"
```

全量验证已通过：

```bash
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
```

结果：116/116 tests passed，server scaffold 正常输出监听配置，diff whitespace check 无输出。

## 2026-05-07 Step 13 Hardening Round 2

本次任务来自外部代码评价复核，目标是在不启动 Step 14 `Session` 的前提下，继续收口 Step 13 已暴露的网络层健壮性问题。

采纳并修复：

- `Logger::get()` 不再重置日志级别；新增回归测试覆盖 `init(Debug)` 和 `setLevel(Debug)` 后多次 `get()` 仍保持 Debug。
- `Epoller` 构造时如果 `epoll_create1()` 失败，直接抛出 `std::runtime_error`，避免半初始化对象继续参与 `EventLoop` 构造。
- `EventLoop` 的 wakeup fd 改为 `UniqueFd` 持有；`looping_` 改为原子状态并由 RAII guard 复位。
- `EventLoop::loop()` 捕获 `Channel` 回调和 pending task 异常并记录日志，单个业务回调不再直接杀死 loop。
- 新增 `EventLoop::isStopped()`，供停止后的跨线程资源清理判断。
- `Acceptor` 增加 idle fd 保护，`EMFILE` / `ENFILE` 时拒绝一个 pending connection，避免 LT 模式 busy loop。
- `Acceptor::close()` 在 loop 已停止时不再等待 queued task，改走直接释放资源的 fallback。
- `Channel::handleEvent()` 不再复制四个 `std::function`，依靠 `Channel::tie()` 保证 owner 生命周期。未 `tie()` 的 `Channel` 必须保证 callback 不会销毁自身或重置当前 callback。

未采纳：

- 暂不把 `EventLoop::updateChannel()` / `removeChannel()` 改成返回 `Status`，避免扩大接口迁移；当前本地系统/编程错误仍通过异常暴露。
- `FrameDecoder::feed()` 的 `vector::erase()` 继续留到 Step 14 `Session` 输入路径使用 `Buffer` 时处理。

新增/更新测试：

- `LoggerTest.GetDoesNotResetConfiguredLevel`
- `LoggerTest.SetLevelSurvivesLaterGetCalls`
- `ChannelTest.HandleEventDoesNotCopyStoredCallbacks`
- `EventLoopTest.ChannelCallbackExceptionDoesNotEscapeLoop`
- `EventLoopTest.IsStoppedBecomesTrueAfterLoopReturns`
- `AcceptorTest.CloseFromOtherThreadAfterLoopStopsDoesNotBlock`
- `AcceptorTest.FdExhaustionRejectsPendingConnectionWithoutLaterCallback`

定向验证已通过：

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R "AcceptorTest.FdExhaustionRejectsPendingConnectionWithoutLaterCallback|AcceptorTest.CloseFromOtherThreadAfterLoopStopsDoesNotBlock|AcceptorTest.AcceptedFdIsClosedWhenCallbackThrowsBeforeTakingOwnership|EventLoopTest.ChannelCallbackExceptionDoesNotEscapeLoop|LoggerTest.GetDoesNotResetConfiguredLevel|ChannelTest.HandleEventDoesNotCopyStoredCallbacks"
```

全量验证已通过：

```bash
cmake -S . -B build && cmake --build build && ./build/server/liteim_server && ctest --test-dir build --output-on-failure
```

结果：112/112 tests passed。

## 2026-05-07 Step 13 Review Hardening

本次任务来自外部代码评价核对，目标是在不启动 Step 14 `Session` 的前提下，修复 Step 13 网络层中已经确认的局部风险。

确认结论：

- `Acceptor::close()` 非 loop 线程调用时没有从 `Epoller` 删除 `listen_channel_`，存在 stale `Channel*` 风险，需要回到 loop 线程执行关闭清理。
- `Acceptor::handleRead()` 中 accepted fd 缺少 RAII 所有权保护，callback 抛异常时可能泄漏 fd。
- `Channel` 需要补 `tie(std::shared_ptr<void>)` / `weak_ptr` 机制，为后续 `Session` 生命周期管理打基础。
- `EventLoop` 异常策略已在 hardening round 2 中收口；`FrameDecoder` 高吞吐输入消费方式留到后续 `Session` / `TcpServer` 设计中处理。

实施计划：

1. 先补 Acceptor / Channel 回归测试，并确认新测试在当前实现下失败。
2. 实现轻量 `UniqueFd`、Acceptor loop 内关闭、Channel tie。
3. 同步 README、docs、tutorials、task_plan、findings、progress。
4. 运行 CMake build、server smoke、CTest、旧路线残留检查，并 review diff。

当前完成：

- 已新增 `include/liteim/net/UniqueFd.hpp` 和 `src/net/UniqueFd.cpp`。
- 已让 `Acceptor` 使用 `UniqueFd` 管理 listen fd 和 accepted fd。
- 已把 `Acceptor::close()` 的实际清理移动到 `closeInLoop()`，非 loop 线程调用时通过 `queueInLoop()` 投递并等待完成。
- 已新增 `Channel::tie()`，用 `weak_ptr` 保护 owner 生命周期。
- 已新增 `UniqueFdTest`、Acceptor hardening 回归测试和 Channel tie 回归测试。
- 定向验证已通过：`cmake --build build && ctest --test-dir build --output-on-failure -R "UniqueFdTest|ChannelTest|AcceptorTest|ReactorInterfaceTest.Channel"`，20/20 passed。
- 全量验证已通过：`cmake -S . -B build && cmake --build build && ./build/server/liteim_server && ctest --test-dir build --output-on-failure`，105/105 passed。
- 路径级旧路线残留检查无输出：没有 `server/net`、`server/protocol`、SQLite、InMemoryStorage 或 `step15_sqlite` 文件路径。
- README/docs/tutorials 仓库外 `PROJECT_MEMORY` 链接检查无输出；公开入口已改为 `docs/roadmap.md`。
- 最终 diff review 未发现需要继续修改的代码逻辑；`FrameDecoder` 输入 Buffer 化仍记录为后续 `Session` / `TcpServer` 设计事项。

## 2026-05-05 Step 0 Reset

本次进度是 `Step 0: reset workspace`，目标是把 LiteIM 文件夹清理成可以从零教学推进的最小起点。

## 已完成

- 删除旧源码目录：`include/`、`src/`、`server/`。
- 删除旧测试目录：`tests/`。
- 删除旧教程目录：`tutorials/`。
- 删除旧文档目录：`docs/`。
- 删除旧 Qt 临时目录：`client_qt/`。
- 删除旧 SQL 目录：`sql/`。
- 删除旧构建产物：`build/`。
- 删除空的 `.codex` 临时文件。
- 删除未来 Step 才会使用的空目录和 `.gitkeep`。
- 将根 `CMakeLists.txt` 改成 Step 0 空 CMake 骨架。
- 重写 README、task_plan、findings、progress。
- 新增 `docs/architecture.md`、`docs/project_layout.md`、`tutorials/README.md`、`tutorials/step00_reset.md`。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md`，加入 Step 0 说明，并把 Qt 描述统一为常见 IM 三栏布局。

## 当前状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| Step 0 清理 | done | 旧路线代码、测试、教程和 build 产物已删除。 |
| Step 0 最小起点 | done | 不提前保留未来目录，也不保留 `.gitkeep`。 |
| Step 0 文档 | done | README、计划文件、docs 和 tutorial 已更新。 |
| Step 0 验证 | done | CMake、CTest、`.gitkeep` 检查和旧文件名检查已通过。 |
| Step 0 commit | done | 已提交：`29e41e9 chore: keep LiteIM step0 minimal`。 |
| Step 1 | pending | 下一步创建真正可构建的 server/test target。 |

## 下一步

验证 Step 0：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
find . -name .gitkeep
rg -n "SQLiteStorage|step15_sqlite|InMemoryStorage|server/net|server/protocol" .
```

然后进入 Step 1：`init: create LiteIM project structure with googletest`。

## Step 0 最小起点验证结果

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `ctest --test-dir build --output-on-failure`：通过，当前没有测试用例，符合 Step 0 预期。
- `find . -name .gitkeep`：无输出，说明没有 `.gitkeep` 残留。
- 旧路线文件名检查：无 `SQLite`、`InMemory`、`step15`、`server/net`、`server/protocol` 文件路径残留。

## 2026-05-05 Step 1 Project Init

本次进入 `Step 1: init CMake project structure`。

已完成：

- 创建 `server/` 和 `tests/` 目录。
- 更新根 `CMakeLists.txt`，接入 `server` 和 `tests` 子目录。
- 新增 `server/CMakeLists.txt` 和 `server/main.cpp`，生成最小 `liteim_server`。
- 新增 `tests/CMakeLists.txt` 和 `tests/test_main.cpp`，生成最小 `liteim_tests`。
- 将 Step 1 测试从自写 `main()` 改成 GoogleTest：`GTest::gtest_main` + `gtest_discover_tests` + `TEST(SmokeTest, GoogleTestWorks)`。
- 更新 README、docs、findings、task_plan 和 tutorials。
- 新增 `tutorials/step01_project_init.md`。

当前仍然没有创建 `include/`、`src/`、`client_qt/`、`bench/`、`scripts/`、`docker/` 等后续目录。

待验证：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过，生成 `liteim_server`、`liteim_tests`，并构建 GoogleTest / GoogleMock 依赖。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running.`。
- `ctest --test-dir build --output-on-failure`：通过，`SmokeTest.GoogleTestWorks` 1/1 passed。
- `find . -name .gitkeep`：无输出。
- 旧路线文件名检查：无 `SQLite`、`InMemory`、`step15`、`server/net`、`server/protocol`、`gitkeep` 文件路径残留。

提交：

```text
init: create LiteIM project structure with googletest
```


## 2026-05-05 PROJECT_MEMORY PersonaAgent Sync

用户更新了 `/home/yolo/jianli/PROJECT_MEMORY.md`，主要变化集中在项目二 PersonaAgent。

已同步到 LiteIM 文档的结论：

- PersonaAgent 新路线是 Authorized Style RAG Edition。
- PersonaAgent 保持 20 Step，但 Step 7-20 改为 6 节点 LangGraph + Knowledge/Memory/Authorized Style RAG + Persona + Safety + Tool Calling + Checkpoint + Trace + Evaluation。
- LiteIM 不嵌入 Python、LangGraph、LLM、embedding 或 vector DB，只提供 Python BotClient 可以复用的 TLV 协议和 BotGateway 接入点。
- Authorized Style RAG 样本必须有 consent manifest、来源、用途、脱敏、撤回和 SafetyGuard 边界。

## 2026-05-05 Step 2 Base Module

本次进入 `Step 2: add config logger and error code`。

概念完成：

- 明确 `base` 模块只放跨模块基础能力，不放业务和网络逻辑。
- `Config` 统一保存 server、MySQL、Redis、Qt 客户端默认连接配置。
- `Logger` 统一封装 `spdlog`，避免后续模块直接使用 `std::cout`。
- `ErrorCode` + `Status` 统一表达成功/失败，避免到处返回裸字符串。
- `Timestamp` 提供基础时间表达，后续消息、日志、压测统计可复用。

代码完成：

- 更新根 `CMakeLists.txt`，新增 `spdlog` v1.13.0 `FetchContent`，并添加 `src/` 子目录。
- 新增 `src/CMakeLists.txt` 和 `src/base/CMakeLists.txt`。
- 新增 `include/liteim/base/Config.hpp`、`ErrorCode.hpp`、`Status.hpp`、`Logger.hpp`、`Timestamp.hpp`。
- 新增 `src/base/Config.cpp`、`ErrorCode.cpp`、`Status.cpp`、`Logger.cpp`、`Timestamp.cpp`。
- 更新 `server/CMakeLists.txt`，让 `liteim_server` 链接 `liteim_base`。
- 更新 `server/main.cpp`，使用默认配置初始化日志并输出 `0.0.0.0:9000`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/base/` 下的 GoogleTest 文件。

测试完成：

- 新增 `tests/base/config_test.cpp`，覆盖默认配置、文件覆盖、缺失值保留默认值、缺失文件、未知 key、非法端口。
- 新增 `tests/base/error_code_test.cpp`，覆盖 `ErrorCode` 字符串和 `Status` 成功/失败状态。
- 新增 `tests/base/logger_test.cpp`，覆盖日志级别解析、未知级别回退和 logger 初始化。
- 新增 `tests/base/timestamp_test.cpp`，覆盖当前毫秒时间戳和 Unix epoch UTC 格式。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，15/15 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已新增 `tutorials/step02_base.md`。
- 已删除临时 `build/` 产物。
- 提交完成：`feat(base): add config logger and error code`。

恢复说明：

- `planning-with-files` 的 `session-catchup.py` 提示了旧版 Buffer 纯问答上下文，但该内容属于重构前路线，当前 Step 2 以 `/home/yolo/jianli/PROJECT_MEMORY.md` 和当前 LiteIM 文件为准。

## 2026-05-05 Step 3 Protocol Types

本次进入 `Step 3: define MessageType and TLV types`。

概念完成：

- 明确 Step 3 只做协议枚举和分类，不做 Packet 编解码。
- `MessageType` 表示一帧消息的业务类型，后续会进入 Packet header 的 `msg_type` 字段。
- `TlvType` 表示 TLV body 中每个字段的类型，例如用户名、消息文本、群组 ID、错误信息和 Persona ID。
- `Push` 消息用于后续服务端主动投递给接收方，它既不是 request，也不是 response。
- 补充修正：`Push` 需要 `isPushType()` 显式分类，不能只靠 `!isRequestType() && !isResponseType()`，否则会和 `Unknown` 混在一起。

代码完成：

- 新增 `include/liteim/protocol/MessageType.hpp`。
- 新增 `include/liteim/protocol/Tlv.hpp`。
- 新增 `src/protocol/CMakeLists.txt`。
- 新增 `src/protocol/MessageType.cpp`。
- 新增 `src/protocol/Tlv.cpp`。
- 更新 `src/CMakeLists.txt`，接入 `protocol` 子目录。
- 更新 `tests/CMakeLists.txt`，让 `liteim_tests` 链接 `liteim_protocol`。
- 补充修正 `MessageType`：新增 `ListGroupsRequest` / `ListGroupsResponse`，并把群聊消息编号调整为 `406/407/408`。
- 补充 `isPushType()`，显式识别 `PrivateMessagePush`、`GroupMessagePush` 和 `BotMessagePush`。

测试完成：

- 新增 `tests/protocol/message_type_test.cpp`，覆盖核心消息类型字符串、未知消息类型、请求类型分类、响应类型分类、Push 类型分类和 Unknown 不分类。
- 新增 `tests/protocol/tlv_type_test.cpp`，覆盖核心 TLV 字段字符串和未知 TLV 类型。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，23/23 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 3 测试清单。
- 已删除临时 `build/` 产物。
- 提交完成：`feat(protocol): define message and tlv types`。

## 2026-05-05 Step 4 Packet Encoding

本次进入 `Step 4: implement Packet encode/decode`。

概念完成：

- 明确 Step 4 只实现 fixed Packet header 编解码，不实现 TLV body 字段编解码。
- `PacketHeader` 固定 20 字节，字段为 `magic`、`version`、`flags`、`msg_type`、`seq_id`、`body_len`。
- Header 多字节字段使用网络字节序，避免结构体 padding、CPU 字节序和内存对齐影响 wire format。
- `body_len` 最大 1MB，异常 header 直接返回错误。
- TCP 半包 / 粘包处理留给 Step 6 `FrameDecoder`。

代码完成：

- 新增 `include/liteim/protocol/Packet.hpp`。
- 新增 `src/protocol/Packet.cpp`。
- 更新 `src/protocol/CMakeLists.txt`，把 `Packet.cpp` 加入 `liteim_protocol`，并链接 `liteim_base`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/protocol/packet_test.cpp`。
- 新增 `PacketHeader`、`Packet`、`validateHeader()`、`encodePacket()` 和 `parseHeader()`。

测试完成：

- 新增 `tests/protocol/packet_test.cpp`，覆盖普通 Packet 编解码、空 body、UTF-8 body、网络字节序、错误 magic、错误 version、body_len 超限、encode 超大 body、不完整 header 和空指针输入。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，33/33 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已新增 `tutorials/step04_packet.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 4 测试清单。
- 已删除临时 `build/` 产物。
- 提交完成：`feat(protocol): add packet encoding and header validation`。

## 2026-05-05 Step 5 TlvCodec

本次进入 `Step 5: implement TlvCodec`。

开始状态：

- 工作区已有用户未提交改动：`src/base/Config.cpp` 和 `src/protocol/Packet.cpp`。
- 这些改动主要是格式和注释调整，不属于 Step 5 范围；本 Step 保留它们，不纳入 Step 5 commit。

概念完成：

- 明确 Step 5 只实现 TLV body 字段编解码。
- TLV 格式固定为 `type(2 bytes) + len(4 bytes) + value(len bytes)`。
- `type` 和 `len` 使用网络字节序。
- 重复字段通过 `TlvMap` 保存为一个 type 对应多个 value。
- 缺失必需字段由 getter 返回 `NotFound`，不在通用 parser 中判断。
- TCP 半包 / 粘包处理留给 Step 6 `FrameDecoder`。

代码完成：

- 新增 `include/liteim/protocol/TlvCodec.hpp`。
- 新增 `src/protocol/TlvCodec.cpp`。
- 更新 `src/protocol/CMakeLists.txt`，把 `TlvCodec.cpp` 加入 `liteim_protocol`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/protocol/tlv_codec_test.cpp`。
- 新增 `appendString()`、`appendUint64()`、`parseTlvMap()`、`getString()`、`getUint64()`、`getRepeatedString()` 和 `getRepeatedUint64()`。

测试完成：

- 新增 `tests/protocol/tlv_codec_test.cpp`，覆盖单字段、多字段、UTF-8 字符串、重复字符串字段、重复 `uint64` 字段、`uint64` 网络字节序、TLV len 越界、不完整 TLV header、缺失字段、错误 `uint64` 长度和 Unknown 类型编码。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，45/45 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已新增 `tutorials/step05_tlv_codec.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 5 测试清单。
- 本次按用户要求保留本地 `build/` 目录。
- 提交完成：`feat(protocol): implement tlv codec`。

## 2026-05-06 Step 5 TlvCodec API Correction

根据 Step 34 / Step 37 后续好友列表、群列表等真实用例，修正 Step 5 API：

- 删除暂时没有真实字段需求的 `appendInt64()`。
- 新增 `getRepeatedUint64()`，用于读取重复 `FriendId`、`UserId`、`GroupId`、`MessageId` 等 ID 列表。
- 保持 C++17 和当前项目 `Status + output parameter` 风格，不改成 C++20 `std::span` 或 `std::byte`。

## 2026-05-06 Step 6 FrameDecoder

本次进入 `Step 6: implement FrameDecoder`。

开始状态：

- 工作区已有用户暂存改动：`include/liteim/protocol/TlvCodec.hpp`、`src/base/Config.cpp` 和 `src/protocol/Packet.cpp`。
- Step 6 不修改这 3 个文件，不把它们纳入 Step 6 commit。

概念完成：

- 明确 TCP 是字节流，不保留消息边界。
- `FrameDecoder` 只负责连续字节流到完整 `Packet` 的解包。
- 本 Step 不读 socket、不调用 epoll、不解析 TLV body、不做业务路由。
- 错误 header 进入 error 状态，等待上层关闭连接或显式 `reset()`。

代码完成：

- 新增 `include/liteim/protocol/FrameDecoder.hpp`。
- 新增 `src/protocol/FrameDecoder.cpp`。
- 更新 `src/protocol/CMakeLists.txt`，把 `FrameDecoder.cpp` 加入 `liteim_protocol`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/protocol/frame_decoder_test.cpp`。
- 新增 `FrameDecoder::feed()`、`hasError()`、`bufferedBytes()` 和 `reset()`。

测试完成：

- 新增 `tests/protocol/frame_decoder_test.cpp`，覆盖完整包、半包、粘包、半包后接粘包、错误 magic、错误 version、body_len 超限、error 状态拒绝继续解析和空指针输入。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，54/54 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已新增 `tutorials/step06_frame_decoder.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 6 测试清单。
- 本次按用户要求保留本地 `build/` 目录。
- 提交完成：`feat(protocol): implement tcp frame decoder`。

## 2026-05-06 Step 7 Buffer

本次进入 `Step 7: add buffer abstraction`。

概念完成：

- 明确 `Buffer` 是网络层通用字节缓冲区，不读 socket、不调用 epoll、不管理连接生命周期。
- `Buffer` 后续会作为 `Session` 的输入缓冲区和输出缓冲区底座。
- `read_index_` 和 `write_index_` 用来区分已读区域、可读区域和可写区域。
- `ensureWritableBytes()` 优先复用已读空间，空间仍不足时再扩容。
- `retrieve()` 越界返回 `InvalidArgument`，不使用进程级断言处理线上输入错误。

代码完成：

- 新增 `include/liteim/net/Buffer.hpp`。
- 新增 `src/net/CMakeLists.txt`。
- 新增 `src/net/Buffer.cpp`。
- 更新 `src/CMakeLists.txt`，接入 `net` 子目录。
- 新增 `tests/net/buffer_test.cpp`。
- 更新 `tests/CMakeLists.txt`，让 `liteim_tests` 链接 `liteim_net`。

测试完成：

- 新增 `BufferTest.DefaultBufferHasNoReadableBytes`。
- 新增 `BufferTest.AppendIncreasesReadableBytes`。
- 新增 `BufferTest.AppendStringStoresReadableData`。
- 新增 `BufferTest.AppendBytePointerStoresBytes`。
- 新增 `BufferTest.RetrieveAdvancesReadIndex`。
- 新增 `BufferTest.RetrieveAllResetsBuffer`。
- 新增 `BufferTest.RetrieveAllAsStringReturnsReadableDataAndClearsBuffer`。
- 新增 `BufferTest.AppendExpandsWhenNeeded`。
- 新增 `BufferTest.AppendCompactsReadableDataBeforeExpanding`。
- 新增 `BufferTest.AppendExpandsAndPreservesExistingData`。
- 新增 `BufferTest.RetrievePastReadableBytesReturnsError`。
- 新增 `BufferTest.NullAppendWithNonzeroLengthReturnsError`。
- 新增 `BufferTest.NullAppendWithZeroLengthIsOk`。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，67/67 tests passed。
- `git diff --check`：通过。
- `find . -name .gitkeep`：无输出。
- 旧路线文件名检查：没有恢复旧 `SQLiteStorage`、`step15_sqlite`、真实 `server/net` 或 `server/protocol` 文件路径。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 tutorials。
- 已新增 `tutorials/step07_buffer.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 7 测试清单。
- 本次按用户要求保留本地 `build/` 目录。
- 提交完成：`feat(net): add buffer abstraction`。

## 2026-05-06 Step 8 SocketUtil

本次进入 `Step 8: implement SocketUtil`。

开始状态：

- 工作区干净。
- 当前新路线中 Step 7 `Buffer` 已完成，下一步是 `SocketUtil`。
- 旧记忆里曾出现过 Step 8 `EventLoop`，但那属于重启前路线；本次以 `/home/yolo/jianli/PROJECT_MEMORY.md` 和当前 `task_plan.md` 为准。

概念进行中：

- `SocketUtil` 只封装 Linux socket 常用系统调用。
- 本 Step 不实现 `epoll`、`Channel`、`EventLoop`、`Acceptor`、`Session` 或 `TcpServer`。

概念完成：

- 明确 `SocketUtil` 是 Linux fd 工具层，不拥有连接生命周期。
- `createNonBlockingSocket()` 负责创建 `AF_INET` / `SOCK_STREAM` / `SOCK_NONBLOCK` / `SOCK_CLOEXEC` socket。
- `setNonBlocking()` 使用 `fcntl(F_GETFL)` + `fcntl(F_SETFL)` 补充设置非阻塞。
- socket option 封装只设置常用选项，不在工具函数里绑定地址、监听端口或退出进程。
- `closeFd()` 接收 fd 引用，关闭前保存当前 fd，然后把变量置为 `kInvalidFd`，避免同一变量重复关闭。

代码完成：

- 新增 `include/liteim/net/SocketUtil.hpp`。
- 新增 `src/net/SocketUtil.cpp`。
- 更新 `src/net/CMakeLists.txt`，把 `SocketUtil.cpp` 加入 `liteim_net`。
- 新增 `createNonBlockingSocket()`、`setNonBlocking()`、`setReuseAddr()`、`setReusePort()`、`setTcpNoDelay()`、`setKeepAlive()`、`closeFd()` 和 `getSocketError()`。

测试完成：

- 新增 `tests/net/socket_util_test.cpp`。
- 新增 `SocketUtilTest.CreateNonBlockingSocketReturnsNonblockingFd`。
- 新增 `SocketUtilTest.SetNonBlockingMarksPlainSocketNonblocking`。
- 新增 `SocketUtilTest.SocketOptionsCanBeEnabled`。
- 新增 `SocketUtilTest.InvalidFdReturnsError`。
- 新增 `SocketUtilTest.CloseFdInvalidatesDescriptorAndCanBeRepeated`。
- 新增 `SocketUtilTest.GetSocketErrorReturnsCurrentSoError`。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，73/73 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): add socket utility functions`。

## 2026-05-06 Step 10 Epoller

本次进入 `Step 10: implement Epoller`。

开始状态：

- LiteIM 仓库当前 HEAD 是 `cdfaa14 feat(net): define reactor core interfaces`。
- 工作区干净。
- `session-catchup.py` 提示的未同步内容仍来自旧的纯概念问答，不对应当前 Step 10 代码改动。

概念进行中：

- `Epoller` 是 Reactor 的系统调用层，只封装 `epoll_create1()`、`epoll_ctl()` 和 `epoll_wait()`。
- `Channel` 仍是 fd/event 状态对象，不拥有 fd。
- 本 Step 会补最小 `Channel.cpp` 状态函数以支撑 `Epoller` 测试，但不实现 `Channel::handleEvent()` 回调分发或自动 `EventLoop` 更新。
- 为满足“无效操作返回错误”，`Epoller` 接口会改为返回 `Status`，并通过输出参数返回 active channel list。

TDD RED：

- 更新 `tests/net/epoller_header_test.cpp`，要求 `Epoller::poll()`、`updateChannel()`、`removeChannel()` 返回 `Status`。
- 新增 `tests/net/epoller_test.cpp`，使用真实 `pipe()` fd 覆盖 add、mod、del、timeout 和无效操作。
- 更新 `tests/CMakeLists.txt` 注册 `epoller_test.cpp`。
- 运行 `cmake --build build`，预期失败于 `EpollerHeaderIsSelfContained` 的 `static_assert`，证明当前接口还不满足 Step 10 错误返回要求。

代码完成：

- 更新 `include/liteim/net/Epoller.hpp`，让 `poll()`、`updateChannel()`、`removeChannel()` 返回 `Status`。
- 新增 `src/net/Epoller.cpp`，实现 `epoll_create1(EPOLL_CLOEXEC)`、`EPOLL_CTL_ADD`、`EPOLL_CTL_MOD`、`EPOLL_CTL_DEL` 和 `epoll_wait()`。
- 新增 `src/net/Channel.cpp`，只实现构造、fd/event/revent 访问、读写事件 mask 修改和回调 setter。
- 更新 `src/net/CMakeLists.txt`，把 `Channel.cpp` 和 `Epoller.cpp` 加入 `liteim_net`。
- 本 Step 仍未实现 `Channel::handleEvent()`、`Channel::update()`、`EventLoop::loop()` 或 `eventfd`。

TDD GREEN：

- 运行 `cmake --build build`：通过。
- 运行 `ctest --test-dir build --output-on-failure -R Epoller`：通过，6/6 tests passed。

文档完成：

- 更新 README，把当前状态切到 Step 10，并补充 `Epoller.cpp`、`Channel.cpp`、LT 模式、`Status` 返回和 81 个测试总数。
- 更新 `docs/architecture.md`，补充 `Epoller` 系统调用层行为和当前边界。
- 更新 `docs/project_layout.md`，补充 Step 10 新增源码、测试文件和教程。
- 更新 `tutorials/README.md`，登记 Step 10 教程。
- 更新 `tutorials/step09_reactor_interfaces.md` 中 `Epoller` 的当前 `Status` 接口签名，避免教程和代码漂移。
- 新增 `tutorials/step10_epoller.md`，按概念、接口、实现规则、测试和面试问答展开说明。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 10 测试清单。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，81/81 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): implement epoller wrapper`。

## 2026-05-06 Step 9 Reactor Interfaces

本次进入 `Step 9: define Epoller / Channel / EventLoop interface`。

开始状态：

- 工作区干净。
- 当前新路线中 Step 8 `SocketUtil` 已完成，下一步是 Reactor 核心接口。
- `session-catchup.py` 提示的未同步消息来自之前纯概念问答，没有项目文件改动，本次不把它当作待同步实现内容。

概念进行中：

- `Channel` 表示一个 fd 的事件代理，保存关注事件和回调接口，但本 Step 不实现回调分发。
- `Epoller` 表示 epoll 系统调用封装边界，但本 Step 不调用 `epoll_create1()`、`epoll_ctl()` 或 `epoll_wait()`。
- `EventLoop` 表示 Reactor 调度层接口，拥有 `Epoller` 并管理 `Channel`，但本 Step 不实现阻塞事件循环和 `eventfd` 唤醒。

TDD RED：

- 新增 `tests/net/channel_header_test.cpp`、`tests/net/epoller_header_test.cpp`、`tests/net/event_loop_header_test.cpp`。
- 更新 `tests/CMakeLists.txt` 注册三个接口编译测试。
- 运行 `cmake --build build`，预期失败于 `fatal error: liteim/net/Channel.hpp: No such file or directory`，证明测试能捕获 Step 9 头文件尚未定义的问题。

代码完成：

- 新增 `include/liteim/net/Channel.hpp`，声明 fd、关注事件、实际事件、事件开关、回调设置和 `handleEvent()` 接口。
- 新增 `include/liteim/net/Epoller.hpp`，声明 `poll()`、`updateChannel()` 和 `removeChannel()` 接口。
- 新增 `include/liteim/net/EventLoop.hpp`，声明 `loop()`、`quit()`、`updateChannel()`、`removeChannel()` 和线程归属检查接口。
- 本 Step 没有新增 `src/net/Epoller.cpp`、`Channel.cpp` 或 `EventLoop.cpp`，也没有实现 epoll 系统调用。

TDD GREEN：

- 运行 `cmake --build build`：通过。
- 运行 `ctest --test-dir build --output-on-failure -R ReactorInterfaceTest`：通过，3/3 tests passed。

文档完成：

- 更新 README，把当前状态切到 Step 9，并补充 `Channel` / `Epoller` / `EventLoop` 接口说明和 76 个测试总数。
- 更新 `docs/architecture.md`，补充当前网络层中的 Reactor 接口边界。
- 更新 `docs/project_layout.md`，补充 Step 9 新增头文件、测试文件和教程。
- 更新 `tutorials/README.md`，登记 Step 9 教程。
- 新增 `tutorials/step09_reactor_interfaces.md`，按概念、接口、边界、测试、面试讲法展开说明。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 9 文件清单和测试清单。

错误记录：

- 一次 stale 文案扫描命令把包含反引号的 pattern 放在双引号里，shell 将反引号内容当作命令替换，出现 `Step: command not found`。已改用单引号重新执行，确认只有 Step 8 历史教程保留自身测试说明，不属于 stale 当前状态。
- `/home/yolo/jianli` 不是 Git 仓库，`PROJECT_MEMORY.md` 是工作区级元数据，不能纳入 `LiteIM` 仓库 commit；LiteIM commit 只会包含仓库内 Step 9 文件。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，76/76 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): define reactor core interfaces`。

## 2026-05-06 Step 11 Channel

本次进入 `Step 11: implement Channel`。

开始状态：

- 当前新路线中 Step 10 `Epoller` 已完成并提交。
- 工作区干净。
- `session-catchup.py` 提示的未同步内容来自旧的纯概念问答，不对应当前 Step 11 代码改动。
- 旧记忆里的 Step 11 `Session` 属于重启前路线；本次以 `/home/yolo/jianli/PROJECT_MEMORY.md` 当前 Step 11 `Channel` 为准。

概念完成：

- `Channel` 是 Reactor 中的 fd 事件分发器，不拥有 fd，不关闭 fd。
- `events_` 表示希望监听的事件，`revents_` 表示本轮 epoll 实际返回的事件。
- `handleEvent()` 根据 `revents_` 分发 read/write/close/error callback。
- `enableReading()`、`enableWriting()` 等关注事件变化函数需要通过 `EventLoop` 更新 epoll 关注状态。
- 本 Step 只补最小 `EventLoop` 更新桥接，不实现 `EventLoop::loop()`、`eventfd` 或跨线程任务队列。

TDD RED：

- 新增 `tests/net/channel_test.cpp`，覆盖事件 mask 修改和 read/write/close/error 回调分发。
- 更新 `tests/CMakeLists.txt` 注册 `channel_test.cpp`。
- 运行 `cmake --build build`，预期失败于 `undefined reference to liteim::Channel::handleEvent()`，证明新增测试能捕获当前缺失行为。

代码完成：

- 更新 `src/net/Channel.cpp`，实现 `handleEvent()` 和私有 `update()`。
- `handleEvent()` 处理 `EPOLLHUP`、`EPOLLERR`、`EPOLLIN`、`EPOLLPRI`、`EPOLLRDHUP` 和 `EPOLLOUT`。
- `handleEvent()` 只拷贝 active events，不复制 callbacks；owner 生命周期由 Step 13 的 `Channel::tie()` 接管，未 `tie()` 的 callback 必须保证不会销毁当前 `Channel` 或重置正在执行的 callback。
- `enableReading()`、`disableReading()`、`enableWriting()`、`disableWriting()` 和 `disableAll()` 修改 `events_` 后调用 `update()`。
- 新增 `src/net/EventLoop.cpp`，只实现 `updateChannel()` / `removeChannel()` 到 `Epoller` 的桥接、`quit()` 和线程归属检查。
- 更新 `src/net/CMakeLists.txt`，把 `EventLoop.cpp` 加入 `liteim_net`。

TDD GREEN：

- 运行 `cmake --build build`：通过。
- 运行 `ctest --test-dir build -R ChannelTest --output-on-failure`：通过，7/7 tests passed。

文档完成：

- 更新 README，把当前状态切到 Step 11，并补充 `Channel::handleEvent()`、`Channel::update()`、`EventLoop.cpp` 最小桥接和 88 个测试总数。
- 更新 `docs/architecture.md`，补充 `Channel` 事件分发和当前 `EventLoop` 边界。
- 更新 `docs/project_layout.md`，补充 Step 11 新增源码、测试文件和教程。
- 更新 `tutorials/README.md`，登记 Step 11 教程。
- 新增 `tutorials/step11_channel.md`，按概念、接口、实现规则、测试和面试问答展开说明。
- 更新 `findings.md`、`task_plan.md` 和 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 11 记录。

错误记录：

- 一次 stale 文案扫描命令又把包含反引号的 pattern 放进双引号，shell 将反引号内容当成命令替换，出现 `Step: command not found`、`Channel::handleEvent: command not found` 等输出。已改用单引号重新执行，确认命中项只是 Step 10 历史进度和 Step 10 计划段落，不是当前状态文案。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，88/88 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): implement channel event dispatching`。

## 2026-05-06 Step 12 EventLoop + eventfd

本次进入 `Step 12: implement EventLoop + eventfd task dispatch`。

开始状态：

- 当前新路线中 Step 11 `Channel` 已完成并提交。
- LiteIM 工作区干净。
- `session-catchup.py` 提示的未同步内容来自旧的纯概念问答，不对应当前 Step 12 代码改动。
- 旧记忆里的 Step 12 `TcpServer` 属于重启前路线；本次以 `/home/yolo/jianli/PROJECT_MEMORY.md` 当前 Step 12 `EventLoop + eventfd` 为准。

概念完成：

- `EventLoop` 是 Reactor 调度层，负责持有 `Epoller`、阻塞等待 fd 事件、遍历活跃 `Channel` 并调用 `handleEvent()`。
- 每个 `EventLoop` 内置一个 `eventfd` wakeup channel，用于跨线程唤醒阻塞中的 `epoll_wait()`。
- `runInLoop()` 在所属线程立即执行任务，跨线程调用时转入 `queueInLoop()`。
- `queueInLoop()` 把任务放进 mutex 保护的队列，并在需要时写 eventfd 唤醒 loop。
- 本 Step 不实现 `Acceptor`、`Session`、`TcpServer`、业务线程池、MySQL 或 Redis。

TDD RED：

- 更新 `tests/net/event_loop_header_test.cpp`，要求 `EventLoop::Functor`、`runInLoop()` 和 `queueInLoop()` 存在。
- 新增 `tests/net/event_loop_test.cpp`，使用真实线程、真实 pipe fd 和真实 eventfd wakeup 路径验证事件循环。
- 更新 `tests/CMakeLists.txt` 注册 `event_loop_test.cpp`。
- 运行 `cmake --build build`，预期失败于 `EventLoop::Functor does not name a type`、`runInLoop is not a member of EventLoop` 和 `queueInLoop is not a member of EventLoop`。

代码完成：

- 更新 `include/liteim/net/EventLoop.hpp`，新增 `Functor`、`runInLoop()`、`queueInLoop()`、wakeup fd、wakeup channel、pending task 队列、mutex 和 pending-task 执行标记。
- 更新 `src/net/EventLoop.cpp`，实现 `eventfd` 创建、wakeup channel 注册、`loop()`、`quit()`、`runInLoop()`、`queueInLoop()`、`wakeup()`、`handleWakeup()` 和 `doPendingTasks()`。
- `loop()` 先执行 pending tasks，再阻塞 `Epoller::poll(-1)`，返回后遍历活跃 `Channel` 并调用 `handleEvent()`，随后再次执行 pending tasks。
- `quit()` 跨线程调用时写 eventfd，保证阻塞中的 loop 能醒来退出。
- `EventLoop` 析构时从 `Epoller` 移除内部 wakeup channel，并关闭 wakeup fd。

TDD GREEN：

- `cmake --build build`：通过。
- `ctest --test-dir build -R EventLoop --output-on-failure`：通过，5/5 tests passed。

文档完成：

- 新增 `tutorials/step12_event_loop.md`，说明 EventLoop、eventfd、任务队列、线程边界、测试和面试问答。
- 更新 `findings.md` 记录 Step 12 约束和设计结论。
- 更新 README，把当前状态切到 Step 12，并补充 `EventLoop::loop()`、`runInLoop()`、`queueInLoop()`、eventfd wakeup 和 92 个测试总数。
- 更新 `docs/architecture.md`，补充当前网络层中的 `EventLoop` 职责、eventfd wakeup 和线程边界。
- 更新 `docs/project_layout.md`，补充 Step 12 新增测试文件和教程。
- 更新 `tutorials/README.md`，登记 Step 12 教程。
- 更新 `task_plan.md` 和 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 12 记录。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，92/92 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): add event loop with eventfd task queue`。

## 2026-05-07 Step 13 Acceptor

本次进入 `Step 13: implement Acceptor`。

开始状态：

- 当前新路线中 Step 12 `EventLoop + eventfd` 已完成并提交。
- 用户随后单独提交了 EventLoop 注释改动：`docs(net): add event loop explanatory comments`。
- `session-catchup.py` 提示的未同步内容来自旧的纯概念问答，不对应当前 Step 13 代码改动。
- 旧记忆里的 Step 13 可能和重启前路线不一致；本次以 `/home/yolo/jianli/PROJECT_MEMORY.md` 当前 Step 13 `Acceptor` 为准。

概念完成：

- `Acceptor` 是主 Reactor 上的监听器，只负责 listen socket 和新连接接收。
- `Acceptor` 不创建 `Session`，不解析 Packet/TLV，不做登录、聊天、MySQL 或 Redis。
- 构造时创建非阻塞 listen fd，设置 socket option，完成 `bind()` / `listen()`，并用 `Channel` 注册读事件到所属 `EventLoop`。
- listen fd 可读时循环 `accept4()`，直到 `EAGAIN` / `EWOULDBLOCK`。
- 新连接 fd 通过 callback 交给后续 `TcpServer`；如果没有 callback，立即关闭 accepted fd，避免泄漏。

TDD RED：

- 新增 `tests/net/acceptor_header_test.cpp`，要求 `liteim/net/Acceptor.hpp` 自包含，并声明 `NewConnectionCallback`、`listenFd()`、`port()`、`listening()` 和 `close()`。
- 新增 `tests/net/acceptor_test.cpp`，使用真实 `127.0.0.1` socket 连接验证监听、callback、多个 pending connection 和 close 行为。
- 更新 `tests/CMakeLists.txt` 注册 Acceptor 测试。
- 运行 `cmake --build build`，预期失败于 `fatal error: liteim/net/Acceptor.hpp: No such file or directory`。

代码完成：

- 新增 `include/liteim/net/Acceptor.hpp`，声明 `Acceptor`、`NewConnectionCallback`、状态查询和关闭接口。
- 新增 `src/net/Acceptor.cpp`，实现 `createNonBlockingSocket()`、`setReuseAddr()`、`setReusePort()`、`bind()`、`listen()`、listen channel 注册和 `accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)` 循环。
- 更新 `src/net/CMakeLists.txt`，把 `Acceptor.cpp` 加入 `liteim_net`。
- `Acceptor::close()` 从所属 `EventLoop` 移除 listen channel，并关闭 listen fd。
- `handleRead()` 不标记 `noexcept`，因为它会调用用户提供的 callback，异常不应在库边界被强制转成 `std::terminate()`。

TDD GREEN：

- `cmake --build build`：通过。
- `ctest --test-dir build -R Acceptor --output-on-failure`：通过，5/5 tests passed。

文档完成：

- 新增 `tutorials/step13_acceptor.md`，说明 Acceptor、listen fd、`accept4()`、callback、close 边界和测试。
- 更新 README，把当前状态切到 Step 13，并补充 Acceptor 文件、职责和 97 个测试总数。
- 更新 `docs/architecture.md`，补充 `Acceptor` 在当前网络层中的职责边界。
- 更新 `docs/project_layout.md`，补充 Step 13 新增头文件、源码、测试文件和教程。
- 更新 `tutorials/README.md`，登记 Step 13 教程。
- 更新 `findings.md`、`task_plan.md` 和 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 13 记录。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，97/97 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): implement nonblocking acceptor`。

## 2026-05-09 Markdown Cleanup Correction

User confirmed that the previous markdown simplification deleted too much useful memory. This correction does not rewrite history or reset the branch; it adds a follow-up commit after `d6a3830 docs: simplify LiteIM markdown memory`.

Changes made:

- Restored `docs/debug_cases/thread_pool_worker_stop.md` from `HEAD^`.
- Restored `docs/debug_cases/net_lifecycle_review_hardening.md` from `HEAD^`.
- Restored `task_plan.md`, `findings.md`, and `progress.md` from `HEAD^`, then appended this correction record.
- Kept `docs/architecture.md`, `docs/project_layout.md`, and `docs/roadmap.md` deleted.
- Deleted `tutorials/README.md` and removed current tutorial references to it.
- Rewrote `README.md` as a GitHub-facing LiteIM overview without `Current Status` / `当前状态` headings and without Codex process memory as public documentation.
- Updated `/home/yolo/jianli/PROJECT_MEMORY.md` and `/home/yolo/jianli/AGENTS.md` to state that `docs/debug_cases/` is useful internal retrospective material.

Verification completed:

- `find docs -type f | sort` printed only the two debug case files.
- `find tutorials -maxdepth 1 -type f -name 'README.md'` produced no output.
- `rg -n "Current Status|当前状态" README.md` produced no output.
- `rg -n "tutorials/README|docs/architecture|docs/project_layout|docs/roadmap" README.md tutorials /home/yolo/jianli/AGENTS.md /home/yolo/jianli/PROJECT_MEMORY.md` produced no output.
- `cmake -S . -B build` passed.
- `cmake --build build` passed.
- `./build/server/liteim_server` passed and printed `LiteIM server scaffold is running on 0.0.0.0:9000`.
- `ctest --test-dir build --output-on-failure` passed `164/164`.
- `git diff --check` produced no output.
- `.gitkeep` and stale SQLite / `InMemoryStorage` / old `server/net` path scans produced no output.
