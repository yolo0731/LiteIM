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

### 关键成员变量和 private helper

`TimerHeap` 内部有两份结构：

- `heap_`：`std::priority_queue<HeapEntry, vector<HeapEntry>, Compare>`，按 `expiration_ms` 排成小根堆，用来快速找到最早过期 timer。
- `timers_`：`std::unordered_map<TimerId, TimerEntry>`，保存仍然有效的 timer 和真正要执行的 callback。

`HeapEntry` 只保存：

- `expiration_ms`：过期时间，单位毫秒。
- `timer_id`：对应的 timer id。

`TimerEntry` 保存：

- `expiration_ms`：用于判断堆顶节点是否仍然匹配。
- `callback`：timer 过期后真正执行的函数。

`Compare` 让 priority_queue 表现成小根堆：过期时间越早越靠前；过期时间相同则 id 小的先触发。

`removeStaleTopEntries()` 负责清理堆顶陈旧节点。陈旧节点有两种：

1. timer id 已经不在 `timers_` 中，说明被 `cancel()` 取消。
2. timer id 还在，但 map 中的过期时间和堆节点不一致，说明堆节点已经不是当前有效记录。

当前接口没有重复 timer，`add()` 创建的都是 one-shot timer：触发一次后从 `timers_` 删除，不会自动重排下一次。

## TimerHeap 的作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

`TimerHeap` 是 `TimerManager` 内部保存 one-shot 定时任务的数据结构。当前最真实场景是 heartbeat 超时检测：`TcpServer` 通过 `TimerManager::runAfter()` 续排一次性 callback，callback 扫描 idle sessions，关闭超时连接后再调用 `scheduleHeartbeatCheck()` 排下一轮。

### 2. 上下层调用连接

```text
TcpServer
    -> TimerManager
    -> TimerHeap
    -> callback(closeIdleSessions + scheduleHeartbeatCheck)
    -> Session::close()
```

`TimerHeap` 的上游是 `TimerManager::runAfter()` / `cancel()` / `handleRead()`，下游只是 callback。它不关心 `Session`、`timerfd`、`EventLoop` 或 heartbeat 业务，业务全部在 callback 里。

### 3. 整体运行链路

1. `TcpServer::scheduleHeartbeatCheck()` 调用 `TimerManager::runAfter(interval, callback)`。
2. `TimerManager` 把 delay 转成绝对 `expiration_ms`。
3. [TimerHeap::add()](../src/timer/TimerHeap.cpp#L14) 生成递增 `TimerId`，同时写入 `heap_` 和 `timers_`。
4. timerfd tick 到来后，`TimerManager::handleRead()` 调用 [popExpired(now_ms)](../src/timer/TimerHeap.cpp#L25)。
5. `popExpired()` 先清理堆顶 stale entry，再按过期时间取出所有到期 timer。
6. 每个到期 timer 从 `timers_` 删除后同步执行 callback。
7. heartbeat callback 执行 `closeIdleSessions()`，随后再次 `scheduleHeartbeatCheck()`，形成 one-shot 续排。

### 4. 自身内部运行流程

整体可以看成 5 步：添加 timer、取消 timer、查询最近到期、弹出过期 timer、惰性删除 stale 堆顶。

核心成员职责：

- `heap_` 负责排序，快速找到最近过期 timer；它只保存 `{expiration_ms, timer_id}`。
- `timers_` 负责判断 timer 是否仍有效，并保存真正要执行的 callback。
- `next_timer_id_` 负责生成递增 id。
- `Compare` 让 `std::priority_queue` 表现成小根堆，过期时间更早的节点排在前面。

核心函数流程：

- [add()](../src/timer/TimerHeap.cpp#L14)：分配 id，把 `{expiration_ms, callback}` 放入 `timers_`，把 `{expiration_ms, id}` 放入 `heap_`。
- [cancel()](../src/timer/TimerHeap.cpp#L21)：只从 `timers_` 删除 id，不扫描 `heap_`。
- [nextExpirationMilliseconds()](../src/timer/TimerHeap.cpp#L51)：先 `removeStaleTopEntries()`，再返回有效堆顶；没有有效 timer 返回 `-1`。
- [popExpired(now_ms)](../src/timer/TimerHeap.cpp#L25)：循环清理 stale top，弹出所有 `expiration_ms <= now_ms` 的有效 timer，move callback 后执行。
- [removeStaleTopEntries()](../src/timer/TimerHeap.cpp#L72)：如果堆顶 id 不在 `timers_`，或 map 中过期时间和堆节点不一致，就弹掉堆顶。

`add(expiration_ms, callback)` 伪流程：

```text
timer_id = next_timer_id_++
timers_[timer_id] = {expiration_ms, callback}
heap_.push({expiration_ms, timer_id})
return timer_id
```

`cancel(timer_id)` 伪流程：

```text
timers_.erase(timer_id)
# heap_ 里对应节点暂时不动，后续由 removeStaleTopEntries() 惰性删除
```

`popExpired(now_ms)` 伪流程：

```text
fired = 0
while true:
    removeStaleTopEntries()
    if heap_ empty or heap_.top.expiration_ms > now_ms:
        return fired
    heap_entry = heap_.top()
    heap_.pop()
    timer_it = timers_.find(heap_entry.timer_id)
    if timer missing or expiration mismatch:
        continue
    callback = move timer_it.callback
    timers_.erase(timer_it)
    if callback: callback()
    ++fired
```

### 5. 小例子和边界

小例子：

```text
add(100, A) -> id=1
add(200, B) -> id=2
cancel(1)
```

这时 `heap_` 顶部仍可能是 `{100, 1}`，但 `timers_` 已经没有 `1`。下一次 `nextExpirationMilliseconds()` 或 `popExpired()` 会通过 `removeStaleTopEntries()` 把 id 1 弹掉，id 2 才是最近有效 timer。

边界：`TimerHeap` 没有锁，当前由 `TimerManager` 在 owner loop 线程使用；callback 在 `popExpired()` 中同步执行，不能长时间阻塞 loop；当前 timer 是一次性定时器 / one-shot timer，触发后自动删除，重复执行要由 callback 自己重新 `runAfter()`；`std::priority_queue` 不支持按 id 删除中间节点，所以 `cancel()` 用惰性删除避免 O(n) 扫描。

## 4. TimerManager.hpp 接口说明

### 构造函数

```cpp
TimerManager(EventLoop* loop, std::chrono::milliseconds tick_interval);
```

`TimerManager` 绑定一个 owner `EventLoop`。

它的 `start()`、`stop()`、`runAfter()` 和 `cancel()` 都要求在 owner loop 线程调用。这样 timer 回调和 `Channel` 生命周期都保持在同一个线程里。非 owner 线程直接调用 `stop()` 会 `std::terminate()`，避免析构时还有捕获裸 `this` 的 stop task 留在 owner loop 队列里。

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

`TimerManager` 当前定位是 heartbeat tick timer：用固定 tick 驱动过期任务扫描。它不是完整 muduo `TimerQueue`；后续如果要做通用定时器，再用 `TimerHeap::nextExpirationMilliseconds()` 把 timerfd 动态 rearm 到最近过期点。

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

### 关键成员变量和 private helper

`TimerManager` 的关键成员包括：

- `loop_`：owner EventLoop，决定 start/stop/runAfter/cancel 的线程归属。
- `tick_interval_`：固定 timerfd tick 间隔。
- `timer_fd_`：timerfd 的 RAII owner。
- `timer_channel_`：把 timerfd 可读事件接入 EventLoop，不拥有 fd。
- `timers_`：内部 `TimerHeap`。
- `started_`：是否已经启动。
- `channel_registered_`：timer channel 是否已经注册到 epoll。
- `handling_timer_event_`：当前是否在 timer callback 栈内，用于 stop 时延迟释放 Channel。

private helper：

- `startInLoop()` 创建 timerfd、设置固定周期、注册 Channel。
- `stopInLoop()` 移除 Channel、关闭 fd、清空 timers。
- `handleRead()` 读 timerfd 计数，然后执行过期 timer。
- `steadyNowMilliseconds()` 用 `steady_clock` 返回单调时间毫秒，避免系统时间调整影响 timer 到期判断。

## TimerManager 的作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

`TimerManager` 把 Linux `timerfd` 接入 Reactor，并用内部 `TimerHeap` 管理 one-shot callback。当前最真实场景是 `TcpServer` base loop 上的固定 tick heartbeat：定期触发 callback，扫描并关闭 idle sessions。

### 2. 上下层调用连接

```text
TcpServer::startHeartbeatTimer()
    -> TimerManager(base_loop, heartbeat_interval)
    -> timerfd + Channel
    -> EventLoop / Epoller
    -> TimerManager::handleRead()
    -> TimerHeap::popExpired(now)
    -> heartbeat callback
    -> TcpServer::closeIdleSessions()
    -> Session::close()
```

上游是 `TcpServer`，下游是 timerfd、Channel、EventLoop 和 `TimerHeap`。Timer callback 在 owner loop 线程同步执行。

### 3. 整体运行链路

1. `TcpServer::startHeartbeatTimer()` 在 base loop 创建 `TimerManager`。
2. [TimerManager::start()](../src/timer/TimerManager.cpp#L50) 检查必须在 owner loop 线程调用。
3. [startInLoop()](../src/timer/TimerManager.cpp#L89) 创建 `timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)`。
4. `startInLoop()` 用 `timerfd_settime()` 设置固定 tick interval。
5. `startInLoop()` 创建 timer Channel，设置 read callback 并启用读事件。
6. `TcpServer` 调用 [runAfter()](../src/timer/TimerManager.cpp#L70) 把 heartbeat callback 放进 `TimerHeap`。
7. timerfd 可读后，EventLoop 调用 [handleRead()](../src/timer/TimerManager.cpp#L149)。
8. `handleRead()` 读掉 timerfd expirations，然后调用 `timers_.popExpired(steadyNowMilliseconds())`。
9. callback 扫描 idle sessions，关闭超时连接，并由 `TcpServer` 续排下一次检查。
10. [stop()](../src/timer/TimerManager.cpp#L58) 在 owner loop 停止 timer、移除 Channel、关闭 fd、清空 timers。

### 4. 自身内部运行流程

整体可以看成 5 步：启动 timerfd、注册 Channel、添加 timer、处理 tick、停止清理。

核心成员职责：

- `loop_` 是 owner EventLoop，决定 start/stop/runAfter/cancel 的线程归属。
- `tick_interval_` 是固定 timerfd tick 间隔。
- `timer_fd_` 是 timerfd 的 RAII owner。
- `timer_channel_` 把 timerfd 可读事件接入 EventLoop，不拥有 fd。
- `timers_` 是内部 `TimerHeap`，保存 one-shot callback。
- `started_` 表示是否启动。
- `channel_registered_` 表示 timer Channel 是否已经注册到 epoll。
- `handling_timer_event_` 表示当前是否在 timer callback 栈内，用于 stop 时延迟释放 Channel。

核心函数流程：

- `start()`：拒绝非 owner 线程，转入 `startInLoop()`。
- `startInLoop()`：校验 tick、创建 timerfd、设置周期、创建 Channel、启用读事件。
- `runAfter(delay, callback)`：assert owner loop，把 delay 小于 0 的情况折成 0，再加入 `TimerHeap`。
- `cancel(timer_id)`：assert owner loop，转给 `TimerHeap::cancel()`。
- `handleRead()`：循环处理 `EINTR`，读掉 timerfd 计数，再执行过期 callback。
- `stopInLoop()`：清空 timers、移除 Channel、关闭 fd，必要时延迟 reset Channel。
- `steadyNowMilliseconds()`：用 steady clock 返回单调毫秒，避免系统时间跳变影响 timer 判断。

`handleRead()` 伪流程：

```text
if timer_fd_ invalid: return
while true:
    n = read(timer_fd, expirations)
    if n == sizeof(expirations): break
    if errno == EINTR: continue
    if errno == EAGAIN: return
    return
handling_timer_event_ = true
try timers_.popExpired(steadyNowMilliseconds())
catch all: swallow
handling_timer_event_ = false
```

### 5. 小例子和边界

小例子：`TcpServer::scheduleHeartbeatCheck()` 每次 `runAfter(heartbeat_interval, callback)`。callback 关闭 idle session 后再次调用 `scheduleHeartbeatCheck()`，形成固定间隔的一次性定时器续排，而不是 `TimerHeap` 自动重复。

边界：`TimerManager` start/stop/destruct 必须在 owner loop 线程；非 owner 线程直接 stop 会终止进程，避免排队裸 `this` 清理；timer callback 在 owner loop 执行，不能直接做阻塞 MySQL / Redis；`timer_channel_` 不拥有 fd，`timer_fd_` 才是 owner；当前是固定 tick heartbeat timer，不是动态 rearm 的完整 TimerQueue。

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
收到完整入站 Packet
  -> updateLastActiveTime()
  -> message callback
```

也就是说，只有协议层成功解出的客户端入站完整包会续期。单纯发一点半包字节不会一直保活连接，服务端主动 push / echo / 系统通知等出站写也不会刷新活跃时间。

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
TEST(TimerManagerTest, StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis)
TEST(TcpServerTest, IdleSessionIsClosedByHeartbeatTimeout)
TEST(TcpServerTest, ActiveSessionSurvivesHeartbeatTimeout)
```

正常路径：

- timer 到期后能执行回调。
- 多个 timer 按 deadline 顺序执行。
- 活跃连接持续发送完整入站 Packet 时不会被 idle timeout 关闭。

异常和边界路径：

- 被取消的 timer 不执行。
- 非 owner 线程直接调用 `TimerManager::stop()` 会终止进程，验证 owner-loop 生命周期契约。
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
timeout 1s ./build/server/liteim_server || test $? -eq 124
ctest --test-dir build --output-on-failure
```

本 Step 完成后，CTest 通过 164 个测试；后续 lifecycle hardening 补充 owner-only stop 和 `Session` 线程归属测试后，CTest 应通过 167 个测试。

## 10. 面试时怎么讲

可以这样讲：

> 我在 Reactor 里用 timerfd 统一处理定时事件。timerfd 本身是 fd，所以能像 socket 一样注册到 epoll。TimerManager 持有 timerfd 和 Channel，触发时读取 timerfd 计数，然后从 TimerHeap 里弹出过期任务执行。当前 TimerManager 定位是 heartbeat tick timer，用固定 tick 扫描过期任务；TimerHeap 的 next-expiration 接口保留给后续动态 rearm 成更完整的 TimerQueue。TimerHeap 用小根堆按过期时间排序，取消时用 lazy deletion，避免 priority_queue 中间删除。

继续说明 heartbeat：

> TcpServer 在 base loop 上注册一个 TimerManager，默认每 5 秒扫描 session 快照。如果某个 Session 90 秒没有收到完整入站 Packet，就调用 Session::close()。close() 不直接跨线程释放 fd，而是投递回 Session 所属 I/O loop，所以连接生命周期仍然遵守 one-loop-per-thread 边界。

这个回答的重点是：

- 定时事件也走 epoll。
- timer callback 不在业务线程池里执行。
- `TimerManager` 在 owner loop 线程停止和析构。
- 关闭连接仍回到 Session 所属 loop。
- `TimerHeap` 的取消用 lazy deletion。

## 11. 面试常见追问

### 为什么不用普通线程 sleep？

普通线程 sleep 后要跨线程操作连接，会让同步和生命周期更复杂。timerfd 可以直接注册到 epoll，让定时事件和网络事件都在 Reactor 线程中处理。

### 为什么不用每个 I/O loop 一个 timer？

Step 18 第一版选择 base loop timer，主要是生命周期更简单，也足够验证 idle timeout。真正关闭连接仍回到 Session 所属 I/O loop。后续如果连接规模很大，可以改成每个 I/O loop 本地维护自己的 session timer，减少 base loop 扫描压力。

### lazy deletion 是什么？

取消 timer 时不从堆里删除节点，只从有效 timer 表里删除 id。等堆顶弹出时，如果发现这个 id 已经不存在，就丢掉它。这样取消是 O(1)，不需要在线性扫描堆。

### 为什么 Session 只在完整入站 Packet 后刷新活跃时间？

因为半包字节不一定代表合法业务活动。如果任意字节都续期，恶意客户端可以慢慢发送垃圾半包来长期占用连接。完整入站 Packet 更符合协议层活跃的定义。

服务端写数据也不能刷新活跃时间。否则服务端持续推送私聊、群聊、离线消息、Bot 回复或系统通知时，即使客户端一直不发心跳、不发业务包，也会被误判为活跃连接。

### timerfd 触发后为什么要 read？

timerfd 可读表示有定时事件发生。必须把里面的 8 字节计数读掉，否则 fd 会一直保持可读，epoll 会不断返回这个事件。
