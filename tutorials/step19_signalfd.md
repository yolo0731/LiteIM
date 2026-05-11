# Step 19：实现 signalfd 优雅关闭

本 Step 的目标是让 `liteim_server` 收到 `SIGINT` 或 `SIGTERM` 时，不再依赖传统 signal handler 或默认杀进程行为，而是把信号转换成 Reactor 中的普通 fd 事件。

到 Step 18.5 为止，LiteIM 已经能启动真实 echo server：

```text
EventLoop base_loop
  -> TcpServer
  -> Acceptor
  -> EventLoopThreadPool
  -> Session
```

但退出仍然不完整。用户按 `Ctrl-C`、部署脚本发送 `SIGTERM`、或者测试需要结束服务端时，都应该走同一条清晰路径：

```text
SIGINT / SIGTERM
  -> signalfd readable
  -> SignalWatcher callback in base loop
  -> TcpServer::stop()
  -> EventLoop::quit()
```

这样 `TcpServer` 仍然在 base loop 线程停止，符合 owner-loop 生命周期规则。

## 1. 为什么用 signalfd

传统 signal handler 的限制很强，handler 里只能安全调用少量 async-signal-safe 函数。直接在 handler 里关闭 `TcpServer`、写日志、加锁、释放 `Channel` 或 join 线程都不安全。

`signalfd` 的做法是：

- 先用 `pthread_sigmask()` 阻塞指定信号。
- 再用 `signalfd()` 创建一个 fd。
- 信号到来时，这个 fd 变成可读。
- `EventLoop` 像处理 socket fd 一样处理 signal fd。

这让退出逻辑回到普通 C++ 回调里执行：

```text
epoll_wait()
  -> Channel::handleEvent()
  -> SignalWatcher::handleRead()
  -> callback(SIGTERM)
```

## 2. 本 Step 新增文件

```text
include/liteim/net/SignalWatcher.hpp
src/net/SignalWatcher.cpp
tests/net/signal_watcher_header_test.cpp
tests/net/signal_watcher_test.cpp
tests/server_signal_shutdown_test.sh
tutorials/step19_signalfd.md
```

同时更新：

```text
src/net/CMakeLists.txt
server/main.cpp
tests/CMakeLists.txt
README.md
task_plan.md
findings.md
progress.md
PROJECT_MEMORY.md
```

## 3. SignalWatcher 的职责

`SignalWatcher` 是一个 Reactor 内部对象，职责很窄：

- 保存需要监听的信号集合。
- 在 owner loop 线程中阻塞这些信号。
- 创建 `signalfd`。
- 用 `Channel` 把 signal fd 注册到 `EventLoop`。
- fd 可读时读取 `signalfd_siginfo`，并调用用户 callback。

它不负责业务退出顺序。真正的退出动作由 `server/main.cpp` 中的 callback 决定。

## 4. SignalWatcher.hpp 接口说明

### 构造函数

```cpp
SignalWatcher(EventLoop* loop, std::vector<int> signals, SignalCallback callback);
```

含义：

- `loop` 是 owner `EventLoop`，通常是 base loop。
- `signals` 是需要通过 `signalfd` 接管的信号，例如 `SIGINT` 和 `SIGTERM`。
- `callback` 在 owner loop 线程执行。

`loop == nullptr` 会抛 `std::invalid_argument`。

### `start()`

```cpp
Status start();
```

必须在 owner loop 线程调用。

主要流程：

```text
rebuildSignalSet()
  -> pthread_sigmask(SIG_BLOCK)
  -> signalfd()
  -> Channel(loop, signal_fd)
  -> enableReading()
```

失败场景：

- 不在 owner loop 线程调用，返回 `InvalidArgument`。
- signal 列表为空，返回 `InvalidArgument`。
- callback 为空，返回 `InvalidArgument`。
- `sigaddset()`、`pthread_sigmask()`、`signalfd()` 或 `Channel` 注册失败，返回 `IoError`。

### `stop()`

```cpp
void stop() noexcept;
```

必须在 owner loop 线程调用。非 owner 线程直接调用会 `std::terminate()`。

这样做是为了保持 muduo-style 生命周期边界：`SignalWatcher` 拥有的 `Channel` 和 fd 必须在创建它们的 `EventLoop` 线程清理，不能在析构或 stop 路径里投递 `queueInLoop([this] { ... })`。

### 状态查询

```cpp
bool started() const noexcept;
int signalFd() const noexcept;
```

测试会用它们确认 watcher 已经启动，并且确实创建了 signal fd。

### 关键成员变量和 private helper

`SignalWatcher` 的关键成员包括：

- `loop_`：owner EventLoop，通常是 base loop。
- `signals_`：要接管的信号列表。
- `callback_`：收到信号后在 owner loop 线程执行。
- `signal_set_`：传给 `pthread_sigmask()` 和 `signalfd()` 的信号集合。
- `old_signal_set_`：启动前的线程 signal mask，用于 stop 时恢复。
- `signal_mask_saved_`：是否已经保存旧 mask。
- `signal_fd_`：signalfd 的 RAII owner。
- `signal_channel_`：把 signal fd 可读事件接入 EventLoop。
- `started_`、`channel_registered_`、`handling_signal_event_`：管理启动、注册和回调栈内清理。

private helper：

- `startInLoop()` 校验参数、阻塞信号、创建 signalfd 并注册 Channel。
- `stopInLoop()` 移除 Channel、关闭 fd、恢复旧 signal mask。
- `rebuildSignalSet()` 重新构造 `sigset_t`。
- `blockSignals()` 保存旧 mask 并阻塞目标信号。
- `restoreSignalMask()` 恢复旧 mask。
- `handleRead()` 循环读取 `signalfd_siginfo` 并执行 callback。

## SignalWatcher 的作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

`SignalWatcher` 把 `SIGINT` / `SIGTERM` 转成 signalfd 可读事件，让 `liteim_server` 在 base `EventLoop` 线程里执行优雅退出。它替代传统 signal handler，避免在异步信号上下文中直接清理 C++ 对象。

### 2. 上下层调用连接

```text
server/main.cpp
    -> SignalWatcher(base_loop, SIGINT/SIGTERM, callback)
    -> pthread_sigmask(SIG_BLOCK)
    -> signalfd
    -> Channel / EventLoop / Epoller
    -> SignalWatcher::handleRead()
    -> callback(signo)
    -> TcpServer::stop()
    -> EventLoop::quit()
```

上游是 server 入口和操作系统信号，下游是 signalfd、Channel 和 shutdown callback。

### 3. 整体运行链路

1. `server/main.cpp` 在 base loop 中创建 `SignalWatcher`。
2. [start()](../src/net/SignalWatcher.cpp#L49) 检查必须在 owner loop 线程调用。
3. [startInLoop()](../src/net/SignalWatcher.cpp#L77) 校验 signal 列表和 callback。
4. `startInLoop()` 调用 [rebuildSignalSet()](../src/net/SignalWatcher.cpp#L146) 构造 `sigset_t`。
5. `startInLoop()` 调用 [blockSignals()](../src/net/SignalWatcher.cpp#L156) 阻塞这些信号并保存旧 mask。
6. `startInLoop()` 创建 signalfd，创建 Channel，注册读事件。
7. SIGTERM 到来时，EventLoop 调用 [handleRead()](../src/net/SignalWatcher.cpp#L175)。
8. `handleRead()` 读出一个或多个 `signalfd_siginfo`，逐个调用 callback。
9. callback 里先 `server.stop()`，再 `loop.quit()`。
10. [stopInLoop()](../src/net/SignalWatcher.cpp#L123) 移除 Channel、关闭 fd、恢复旧 signal mask。

### 4. 自身内部运行流程

整体可以看成 5 步：构造 signal set、阻塞信号、创建 signalfd、分发 signal callback、停止恢复。

核心成员职责：

- `loop_` 是 owner EventLoop。
- `signals_` 是要接管的信号列表。
- `callback_` 是收到 signal 后的上层行为。
- `signal_set_` 是 signalfd 监听集合。
- `old_signal_set_` 用于 stop 时恢复线程 signal mask。
- `signal_fd_` 是 signalfd 的 `UniqueFd` owner。
- `signal_channel_` 代理 signalfd 读事件，不拥有 fd。
- `handling_signal_event_` 用于 callback 栈内停止时延迟释放 Channel。

核心函数流程：

- `start()`：拒绝非 owner 线程，转入 `startInLoop()`。
- `startInLoop()`：校验、构造 signal set、阻塞信号、创建 signalfd、注册 Channel。
- `rebuildSignalSet()`：清空 set 后逐个 `sigaddset()`。
- `blockSignals()`：调用 `pthread_sigmask(SIG_BLOCK, ...)` 并保存旧 mask。
- `handleRead()`：循环读取 signalfd，处理 `EINTR` / `EAGAIN`，每个 signo 调 callback。
- `stopInLoop()`：移除 Channel、关闭 fd、恢复旧 mask。

`handleRead()` 可以理解成“signalfd 可读后，把信号转换成普通 callback”：

```text
SIGINT / SIGTERM 到达进程
    ↓
signalfd 变为可读
    ↓
SignalWatcher::handleRead()
    ↓
读取 signalfd_siginfo
    ↓
把 ssi_signo 交给 callback
    ↓
server.stop()
    ↓
loop.quit()
```

内部边界是：`EINTR` 表示读取被打断，继续处理；`EAGAIN` 表示本轮信号已经读完；callback 执行期间用 `handling_signal_event_` 保护 Channel 延迟释放，避免在事件栈里销毁正在分发的对象。

### 5. 该项目代码在实际应用中的具体数据例子

线上进程收到 `SIGTERM` 时，不在传统 signal handler 里直接析构 `TcpServer`。base loop 的 `SignalWatcher` 通过 signalfd 读到 `SIGTERM`，调用关闭回调：先在 base loop 内 `server.stop()`，再 `loop.quit()`。这样即使当前还有 `session_id=42` 或 timerfd 事件，清理路径也保持 owner-loop 顺序。

## 5. server/main.cpp 如何退出

`server/main.cpp` 现在是真正的 echo server 入口：

```text
Config::defaults()
  -> Logger::init()
  -> EventLoop loop
  -> TcpServer server
  -> SignalWatcher signal_watcher(SIGINT, SIGTERM)
  -> signal_watcher.start()
  -> server.start()
  -> loop.loop()
```

信号 callback：

```cpp
[&](int signo) {
    Logger::get()->info("LiteIM server received signal {}, shutting down", signo);
    server.stop();
    loop.quit();
}
```

这里有两个关键点：

1. callback 在 base loop 线程执行，所以 `server.stop()` 满足 Step 18.5 的 owner-loop-only 约束。
2. `signal_watcher.start()` 在 `server.start()` 之前执行，所以后续创建的 I/O 线程会继承被阻塞的 `SIGINT` / `SIGTERM` mask，信号统一由 base loop 的 `signalfd` 读取。

## 6. 本 Step 不做什么

本 Step 不实现：

- 业务 `ThreadPool` 的退出编排。
- MySQL / Redis 连接池关闭。
- signalfd 以外的传统 signal handler。
- Session 输入路径简化。
- Session 状态机收敛。
- 动态 rearm `TimerManager`。

这些属于后续独立 Step 或可选清理。

## 7. 测试设计

新增测试：

```cpp
TEST(ReactorInterfaceTest, SignalWatcherHeaderIsSelfContained)
TEST(SignalWatcherTest, SignalfdDispatchesSignalInOwnerLoop)
TEST(SignalWatcherTest, StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis)
```

`SignalWatcherHeaderIsSelfContained` 验证头文件可以独立包含，并固定构造函数、`start()` 和 `stop()` 的 public API。

`SignalfdDispatchesSignalInOwnerLoop` 创建 `EventLoop + SignalWatcher`，从另一个线程发送 `SIGUSR1`，确认信号通过 `signalfd` 进入 owner loop callback。

`StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis` 是生命周期回归测试：非 owner 线程直接 stop 必须暴露契约错误，不能悄悄投递捕获裸 `this` 的清理任务。

同时新增 CTest 脚本：

```text
LiteIMServerSignalTest.TerminatesOnSigterm
```

这个测试会启动真实 `liteim_server`，等待日志中出现 listening 信息，然后发送 `SIGTERM`，并要求进程以 `0` 退出。

## 8. 验证命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build -R "SignalWatcher|LiteIMServerSignal" --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试时怎么讲

可以这样讲：

> 我没有用传统 signal handler 直接释放资源，因为那里面不能安全地做日志、加锁、析构 Reactor 对象或 join 线程。LiteIM 使用 `pthread_sigmask()` 阻塞 `SIGINT` / `SIGTERM`，再用 `signalfd` 把信号变成 fd 可读事件，注册到 base `EventLoop`。信号 callback 在 base loop 线程执行，所以可以安全调用 owner-loop-only 的 `TcpServer::stop()`，再 `loop.quit()` 退出主循环。

重点是：

- 信号事件和 socket / timer 事件走同一套 Reactor。
- `TcpServer` 不从异步 signal handler 清理。
- `TcpServer::stop()` 仍然只在 base loop 线程调用。
- I/O 线程在 `SignalWatcher` 启动之后创建，会继承阻塞信号的 mask。

## 10. 面试常见追问

### 为什么不用 `std::signal()`？

因为传统 signal handler 不能安全调用复杂 C++ 代码。`server.stop()` 会关闭 fd、移除 `Channel`、修改容器、停止线程池并写日志，这些都不应该在 signal handler 中执行。

### 为什么 `SignalWatcher::stop()` 也要 owner-loop-only？

它持有 `Channel` 和 `signalfd`，这些资源注册在某个 `EventLoop` 的 epoll 中。跨线程直接清理会破坏 Reactor 边界；跨线程投递裸 `this` 又有 UAF 风险。所以第一版采用硬 owner-loop 契约。

### 为什么 `signal_watcher.start()` 要在 `server.start()` 之前？

因为 `pthread_sigmask()` 是线程级 mask，新线程会继承创建它的线程当前 mask。先启动 `SignalWatcher`，再启动 I/O 线程，可以让 `SIGINT` / `SIGTERM` 不会被子 I/O 线程用默认动作处理，而是统一由 base loop 的 `signalfd` 消费。

### 收到信号后为什么先 `server.stop()` 再 `loop.quit()`？

`server.stop()` 需要在 base loop 线程执行完整清理：停止 accept、关闭 sessions、停止 I/O 线程池。如果先 quit，主循环可能直接退出，清理顺序会变得不明确。
