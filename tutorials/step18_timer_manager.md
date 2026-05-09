# Step 18：实现 TimerManager + timerfd 心跳超时

本 Step 的目标是让服务端能主动清理长时间不活跃的连接。

到 Step 17 为止，LiteIM 已经有：

```text
TcpServer
  -> Acceptor 接收连接
  -> EventLoopThreadPool 分配 I/O loop
  -> Session 读写 Packet
  -> ThreadPool 预留业务执行位置
```

但如果客户端连接后一直不发数据，服务端不能永远保留这个 `Session`。Step 18 解决的问题是：

```text
服务端如何周期性检查连接活跃时间，并关闭超时连接？
```

答案是用 Linux `timerfd` 把定时事件也纳入 Reactor。

## 1. 为什么用 timerfd

普通定时器回调通常会引入额外线程或信号处理，这会让连接生命周期变复杂。

`timerfd` 的好处是：

- 它本身是一个 fd。
- 可以注册到 epoll。
- 触发时表现为 fd 可读。
- 回调仍在 `EventLoop` 线程中执行。

这样 LiteIM 的定时任务和 socket 事件走同一套 Reactor 流程：

```text
timerfd readable
  -> Channel read callback
  -> TimerManager::handleRead()
  -> run expired callbacks
```

## 2. 本 Step 新增文件

```text
include/liteim/timer/TimerHeap.hpp
include/liteim/timer/TimerManager.hpp
src/timer/CMakeLists.txt
src/timer/TimerHeap.cpp
src/timer/TimerManager.cpp
tests/timer/timer_heap_header_test.cpp
tests/timer/timer_heap_test.cpp
tests/timer/timer_manager_header_test.cpp
tests/timer/timer_manager_test.cpp
tutorials/step18_timer_manager.md
```

同时更新：

```text
include/liteim/net/TcpServer.hpp
src/net/TcpServer.cpp
src/net/Session.cpp
src/CMakeLists.txt
tests/CMakeLists.txt
tests/net/tcp_server_header_test.cpp
tests/net/tcp_server_test.cpp
README.md
tutorials/README.md
task_plan.md
findings.md
progress.md
PROJECT_MEMORY.md
```

`TimerManager` 依赖 `EventLoop` 和 `Channel`，所以当前源码放在 `src/timer/`，但通过 `src/timer/CMakeLists.txt` 编进 `liteim_net`。这样不会为了一个 timer target 和 net target 制造循环依赖。

## 3. TimerHeap.hpp 接口说明

### `TimerId`

```cpp
using TimerId = std::uint64_t;
```

每个 timer 都有一个自增 id，用于取消。

### `TimerCallback`

```cpp
using TimerCallback = std::function<void()>;
```

过期后执行的回调。

当前只支持无参数、无返回值回调。后续业务需要更多上下文时，可以通过 lambda 捕获。

### `add()`

```cpp
TimerId add(std::int64_t expiration_ms, TimerCallback callback);
```

添加一个 one-shot timer。

`expiration_ms` 是单调时钟毫秒时间点，不是延迟时长。`TimerManager::runAfter()` 会把“延迟多久”转换成“具体过期时间”。

### `cancel()`

```cpp
void cancel(TimerId timer_id);
```

取消 timer。

这里没有直接从堆里删除对应节点，因为 `std::priority_queue` 不支持按 id 删除中间元素。当前做法是从 `timers_` 映射中删除这个 id，堆里的旧节点等到浮到堆顶时再丢掉。

这就是 lazy deletion。

### `popExpired()`

```cpp
std::size_t popExpired(std::int64_t now_ms);
```

执行所有已过期 timer，并返回本次触发数量。

它会先清理堆顶的陈旧节点：

- timer 已经被 cancel。
- timer id 对应的过期时间和堆节点不一致。

然后按过期时间从小到大执行回调。

### `nextExpirationMilliseconds()`

```cpp
std::int64_t nextExpirationMilliseconds();
```

返回当前最早的有效过期时间。没有有效 timer 时返回 `-1`。

当前 `TimerManager` 使用固定 tick interval，没有动态重设 timerfd 到最近过期点；这个接口主要给测试和后续优化预留。

## 4. TimerManager.hpp 接口说明

### 构造函数

```cpp
TimerManager(EventLoop* loop, std::chrono::milliseconds tick_interval);
```

`TimerManager` 绑定一个 owner `EventLoop`。

它的 `start()`、`runAfter()` 和 `cancel()` 都要求在 owner loop 线程调用。这样 timer 回调和 `Channel` 生命周期都保持在同一个线程里。

### `start()`

```cpp
Status start();
```

启动 timerfd。

内部流程：

```text
timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)
  -> timerfd_settime()
  -> Channel(loop, timer_fd)
  -> setReadCallback(handleRead)
  -> enableReading()
```

失败场景：

- 不在 owner loop 线程调用，返回 `InvalidArgument`。
- tick interval 小于等于 0，返回 `InvalidArgument`。
- `timerfd_create()` 或 `timerfd_settime()` 失败，返回 `IoError`。

### `stop()`

```cpp
void stop() noexcept;
```

停止 timerfd，移除 `Channel`，清空 pending timer。

如果 `stop()` 正好在 timer 回调期间被调用，不会立即销毁正在执行的 `Channel` 对象，而是先移除 epoll 注册、关闭 fd，等当前回调栈自然返回后再由对象析构或后续 cleanup 释放资源。

### `runAfter()`

```cpp
TimerId runAfter(std::chrono::milliseconds delay, TimerCallback callback);
```

注册一个 one-shot timer。它把当前 `steady_clock` 时间加上 delay，交给 `TimerHeap::add()`。

### `cancel()`

```cpp
void cancel(TimerId timer_id);
```

取消一个还没执行的 timer。

## 5. TcpServer 如何接入 heartbeat timeout

当前选择把 `TimerManager` 注册在 base `EventLoop`，不是每个 I/O loop 各建一个 timer。

这样做的理由是：

- 符合 Step 18 允许的“主 EventLoop 或每个 I/O EventLoop”范围。
- 生命周期更简单，`TcpServer::stop()` 在 base loop 中停止 timer。
- session 表本来就由 `TcpServer` 统一维护，base loop 扫描快照很自然。
- 真正关闭连接仍通过 `Session::close()` 回到 Session 所属 I/O loop，不跨线程直接释放 fd 或 `Channel`。

核心流程：

```text
TcpServer::start()
  -> startHeartbeatTimer()
  -> scheduleHeartbeatCheck()

Timer callback
  -> closeIdleSessions()
  -> scheduleHeartbeatCheck()
```

默认配置：

```text
heartbeat_interval = 5 seconds
heartbeat_timeout = 90 seconds
```

测试中通过：

```cpp
tcp_server.setHeartbeatOptions(20ms, 60ms);
```

把等待时间缩短。

## 6. Session 活跃时间

`Session` 已经维护：

```cpp
std::int64_t lastActiveTimeMilliseconds() const noexcept;
```

本 Step 调整为：

```text
收到完整 Packet
  -> updateLastActiveTime()
  -> message callback
```

也就是说，只有协议层成功解出的完整包会续期。单纯发一点半包字节不会一直保活连接。

## 7. 本 Step 不做什么

本 Step 不实现：

- 协议层 `HeartbeatRequest` / `HeartbeatResponse`。
- `HeartbeatService`。
- 登录态续期。
- Redis 在线状态 TTL。
- Redis 未读计数。
- 客户端重连。
- signalfd 优雅退出。
- MySQL / Redis 接入。

这些都属于后续 Step。

## 8. 测试设计

新增测试文件：

```text
tests/timer/timer_heap_header_test.cpp
tests/timer/timer_heap_test.cpp
tests/timer/timer_manager_header_test.cpp
tests/timer/timer_manager_test.cpp
```

同时扩展：

```text
tests/net/tcp_server_header_test.cpp
tests/net/tcp_server_test.cpp
```

测试用例：

```cpp
TEST(TimerInterfaceTest, TimerHeapHeaderIsSelfContained)
TEST(TimerInterfaceTest, TimerManagerHeaderIsSelfContained)
TEST(TimerHeapTest, PopExpiredRunsCallbacksInDeadlineOrder)
TEST(TimerHeapTest, CancelUsesLazyDeletionWithoutRemovingNewTimer)
TEST(TimerHeapTest, PopExpiredIgnoresFutureTimers)
TEST(TimerManagerTest, TimerFdTickRunsExpiredTimer)
TEST(TimerManagerTest, CancelledTimerDoesNotRun)
TEST(TcpServerTest, IdleSessionIsClosedByHeartbeatTimeout)
TEST(TcpServerTest, ActiveSessionSurvivesHeartbeatTimeout)
```

正常路径：

- timer 到期后能执行回调。
- 多个 timer 按 deadline 顺序执行。
- 活跃连接持续发送完整 Packet 时不会被 idle timeout 关闭。

异常和边界路径：

- 被取消的 timer 不执行。
- lazy deletion 不会误删同一时间点的新 timer。
- 未到期 timer 不会被提前触发。
- idle session 会被 `TcpServer` 的 heartbeat timeout 关闭。

## 9. 如何运行测试

定向测试：

```bash
ctest --test-dir build --output-on-failure -R "Timer|TcpServerTest\\.(IdleSessionIsClosedByHeartbeatTimeout|ActiveSessionSurvivesHeartbeatTimeout)|ReactorInterfaceTest.TcpServerHeaderIsSelfContained"
```

全量测试：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

本 Step 完成后，CTest 应通过 164 个测试。

## 10. 面试时怎么讲

可以这样讲：

> 我在 Reactor 里用 timerfd 统一处理定时事件。timerfd 本身是 fd，所以能像 socket 一样注册到 epoll。TimerManager 持有 timerfd 和 Channel，触发时读取 timerfd 计数，然后从 TimerHeap 里弹出过期任务执行。TimerHeap 用小根堆按过期时间排序，取消时用 lazy deletion，避免 priority_queue 中间删除。

继续说明 heartbeat：

> TcpServer 在 base loop 上注册一个 TimerManager，默认每 5 秒扫描 session 快照。如果某个 Session 90 秒没有收到完整 Packet，就调用 Session::close()。close() 不直接跨线程释放 fd，而是投递回 Session 所属 I/O loop，所以连接生命周期仍然遵守 one-loop-per-thread 边界。

这个回答的重点是：

- 定时事件也走 epoll。
- timer callback 不在业务线程池里执行。
- 关闭连接仍回到 Session 所属 loop。
- `TimerHeap` 的取消用 lazy deletion。

## 11. 面试常见追问

### 为什么不用普通线程 sleep？

普通线程 sleep 后要跨线程操作连接，会让同步和生命周期更复杂。timerfd 可以直接注册到 epoll，让定时事件和网络事件都在 Reactor 线程中处理。

### 为什么不用每个 I/O loop 一个 timer？

Step 18 第一版选择 base loop timer，主要是生命周期更简单，也足够验证 idle timeout。真正关闭连接仍回到 Session 所属 I/O loop。后续如果连接规模很大，可以改成每个 I/O loop 本地维护自己的 session timer，减少 base loop 扫描压力。

### lazy deletion 是什么？

取消 timer 时不从堆里删除节点，只从有效 timer 表里删除 id。等堆顶弹出时，如果发现这个 id 已经不存在，就丢掉它。这样取消是 O(1)，不需要在线性扫描堆。

### 为什么 Session 只在完整 Packet 后刷新活跃时间？

因为半包字节不一定代表合法业务活动。如果任意字节都续期，恶意客户端可以慢慢发送垃圾半包来长期占用连接。完整 Packet 更符合协议层活跃的定义。

### timerfd 触发后为什么要 read？

timerfd 可读表示有定时事件发生。必须把里面的 8 字节计数读掉，否则 fd 会一直保持可读，epoll 会不断返回这个事件。
