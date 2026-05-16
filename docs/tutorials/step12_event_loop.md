# Step 12：实现 EventLoop + eventfd 任务投递

## 0. 本 Step 结论

- 目标：EventLoop 是 Reactor 的调度层。
- 前置依赖：依赖 Step 0-11 已建立的工程、协议或运行时基础。
- 主要交付：`实现 EventLoop + eventfd 任务投递` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

`EventLoop` 是 Reactor 的调度层。Step 10 已经有 `Epoller`，Step 11 已经有 `Channel::handleEvent()`，Step 12 把它们串成真正能运行的事件循环：

```text
EventLoop::loop()
    -> Epoller::poll()
    -> active Channel list
    -> Channel::handleEvent()
```

同时，本 Step 从一开始加入 `eventfd`，让其他线程可以通过 `queueInLoop()` 唤醒阻塞在 `epoll_wait()` 的 I/O 线程。

### 为什么需要 EventLoop

`Epoller` 只封装 Linux epoll 系统调用，`Channel` 只保存 fd 事件和回调。它们都不应该负责“循环调度”。

`EventLoop` 的职责是：

- 持有一个 `Epoller`。
- 在 `loop()` 中阻塞等待 fd 事件。
- 遍历活跃 `Channel`。
- 调用 `Channel::handleEvent()`。
- 管理跨线程投递过来的任务。
- 用 `eventfd` 唤醒阻塞中的 `epoll_wait()`。

后续每个 I/O 线程都会拥有一个自己的 `EventLoop`，这就是 one-loop-per-thread 的基础。

### 为什么需要 eventfd

假设 I/O 线程阻塞在：

```cpp
epoll_wait(...)
```

这时业务线程想让它执行一个任务，比如后续发送响应。如果只把任务放进 vector，I/O 线程不会马上知道，因为它还在睡眠。

`eventfd` 的作用就是给这个 loop 一个内部唤醒 fd：

```text
queueInLoop()
    -> pending_tasks_.push_back(task)
    -> eventfd_write(wakeup_fd_, 1)
    -> epoll_wait 返回
    -> wakeup channel read callback
    -> doPendingTasks()
```

这样跨线程任务不会一直等到下一个网络事件才执行。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `实现 EventLoop + eventfd 任务投递` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/net/EventLoop.hpp` | 修改 | 补齐 loop、quit、runInLoop、queueInLoop 和 Channel 管理接口 |
| `src/net/EventLoop.cpp` | 修改 | 实现 epoll dispatch、eventfd wakeup 和 pending task 执行 |
| `src/net/CMakeLists.txt` | 修改 | 把 EventLoop 实现加入 `liteim_net` |
| `tests/net/event_loop_header_test.cpp` | 新增/更新 | 固定 EventLoop header 自包含和 API |
| `tests/net/event_loop_test.cpp` | 新增 | 覆盖任务投递、fd 事件和多任务执行 |
| `README.md` | 更新 | 记录 EventLoop 和 eventfd 任务队列 |
| `docs/tutorials/step12_event_loop.md` | 新增 | 讲解 Reactor loop 与跨线程唤醒 |
| `docs/process/task_plan.md / docs/process/findings.md / docs/process/progress.md` | 更新 | 记录 Step 12 过程和验证结果 |

## 4. 核心接口与契约

`EventLoop.hpp` 是 one-loop-per-thread Reactor 的调度接口。

public 类型和生命周期：

- `Functor = std::function<void()>`，表示要在 owner loop 线程执行的任务。
- 构造函数记录创建线程 id，创建 `Epoller`、eventfd 和 wakeup Channel。
- 析构时移除 wakeup Channel 并关闭 eventfd。
- copy 被禁用，一个 EventLoop 只能归属一个线程。

事件循环接口：

- `loop()` 只能在 owner 线程调用。它进入阻塞循环，执行 pending task，等待 fd 事件，再分发 Channel。
- `quit()` 设置退出标记；跨线程调用时会写 eventfd 唤醒阻塞中的 loop。

任务投递接口：

- `runInLoop(task)` 同线程立即执行；跨线程转成 `queueInLoop()`。
- `queueInLoop(task)` 把任务加入 `pending_tasks_`，必要时调用 `wakeup()`。
- 空 task 被忽略。

Channel 管理：

- `updateChannel(channel)` 和 `removeChannel(channel)` 都要求 owner loop 线程调用。
- 失败的 `Status` 会转成异常暴露给调用方，因为 epoll 注册失败属于 Reactor 层严重错误。

线程检查：

- `isInLoopThread()` 比较当前线程 id 和构造线程 id。
- `assertInLoopThread()` 发现跨线程误用时抛 `std::logic_error`。

关键 private 成员和 helper：

- `looping_` 防止同一个 loop 重入运行，`quit_` 表示退出请求。
- `thread_id_` 是 owner 线程。
- `epoller_` 持有 epoll 封装。
- `wakeup_fd_` 和 `wakeup_channel_` 负责跨线程唤醒。
- `pending_tasks_` 和 `mutex_` 保存跨线程任务队列。
- `calling_pending_tasks_` 用于判断同线程执行 pending task 时是否还需要唤醒。
- `wakeup()` 写 eventfd，`handleWakeup()` 读 eventfd，`doPendingTasks()` 执行任务队列。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

`EventLoop` 是 Reactor 的线程归属和事件循环。base loop 负责 `TcpServer` 生命周期、listen fd、heartbeat timerfd 和 signalfd；每个 I/O worker loop 负责自己绑定的 `Session` 读写。业务线程要发响应时，也必须通过 `Session::sendPacket()` 间接回到连接所属 EventLoop。

### 2. 上下层调用连接

```text
base thread / I/O thread
    -> EventLoop::loop()
    -> Epoller::poll()
    -> Channel::handleEvent()
    -> Acceptor / Session / TimerManager / SignalWatcher callback

other thread
    -> runInLoop() / queueInLoop()
    -> pending_tasks_
    -> eventfd wakeup
    -> doPendingTasks()
```

上游是拥有 loop 的线程和跨线程任务提交者，下游是 `Epoller`、`Channel`、eventfd 和各 fd owner callback。

### 3. 整体运行链路

1. [EventLoop 构造函数](../src/net/EventLoop.cpp) 记录当前线程 id，创建 Epoller、eventfd 和 wakeup Channel。
2. [loop()](../src/net/EventLoop.cpp) 只能在 owner 线程调用。
3. 每轮先执行启动前或上一轮留下的 pending tasks。
4. 如果没有退出请求，就阻塞在 `Epoller::poll(-1)`。
5. fd 活跃后逐个调用 `Channel::handleEvent()`。
6. 再执行一轮 pending tasks，处理回调期间排进来的任务。
7. [quit()](../src/net/EventLoop.cpp) 设置退出标记；跨线程 quit 会唤醒 eventfd。
8. 退出由 `quit_` 控制；上层 Reactor 对象不要依赖 stopped 查询做跨线程清理。

### 4. 自身内部运行流程

整体可以看成 5 步：初始化 wakeup fd、跑事件循环、投递任务、唤醒 loop、执行任务。

核心成员职责：

- `thread_id_` 固定 owner 线程。
- `epoller_` 管 fd 事件。
- `wakeup_fd_` 是 eventfd owner。
- `wakeup_channel_` 把 eventfd 接入 epoll。
- `pending_tasks_` 保存跨线程函数。
- `quit_` 和 `looping_` 表达 loop 状态。

核心函数流程：

- [loop()](../src/net/EventLoop.cpp)：主循环，处理任务、poll fd、分发 Channel、检查退出。
- [runInLoop()](../src/net/EventLoop.cpp)：同线程立即执行，跨线程转成 `queueInLoop()`。
- [queueInLoop()](../src/net/EventLoop.cpp)：任务入队，必要时唤醒 eventfd。
- [wakeup()](../src/net/EventLoop.cpp)：向 eventfd 写 1，让阻塞中的 epoll 返回。
- [handleWakeup()](../src/net/EventLoop.cpp)：读掉 eventfd 计数，避免 fd 一直可读。
- [doPendingTasks()](../src/net/EventLoop.cpp)：swap 出任务队列，在锁外执行并隔离异常。

`queueInLoop(task)` 可以理解成“把跨线程任务放进队列，并用 eventfd 叫醒 loop”：

```text
任意线程提交 Functor
    ↓
pending_tasks_ 保存任务
    ↓
跨线程提交或正在执行 pending tasks 时写 eventfd
    ↓
epoll_wait() 被唤醒
    ↓
owner loop 执行 doPendingTasks()
```

`loop()` 主循环可以理解成：

```text
owner 线程进入 EventLoop::loop()
    ↓
先执行已经排队的任务
    ↓
Epoller::poll() 等待 fd 事件或 eventfd 唤醒
    ↓
活跃 Channel 逐个 handleEvent()
    ↓
再次执行 pending tasks
    ↓
收到 quit 请求后退出循环
```

这个设计把“谁可以提交任务”和“谁真正操作 fd / Channel”分开：外部线程只入队，真正执行仍回到 owner loop 线程。

### 5. 该项目代码在实际应用中的具体数据例子

ChatService 在业务线程保存 `message_id=5001` 后，不能直接写 Bob 的 fd。它只持有 `weak_ptr<Session>`，然后通过 Bob 所属 I/O loop 的 `queueInLoop()` 投递发送任务；eventfd 唤醒那个 EventLoop，最终在 owner thread 内调用 `Session::sendPacket()`，把 `PRIVATE_MESSAGE_PUSH seq_id=7` 放入 output buffer。

## 6. 关键实现点

### 本 Step 修改的文件

```text
include/liteim/net/EventLoop.hpp
src/net/EventLoop.cpp
tests/net/event_loop_header_test.cpp
tests/net/event_loop_test.cpp
docs/tutorials/step12_event_loop.md
```

`EventLoop.hpp` 新增 `Functor`、`runInLoop()` 和 `queueInLoop()`，并补充内部任务队列、mutex、wakeup fd 和 wakeup channel。

`EventLoop.cpp` 实现阻塞事件循环、eventfd 唤醒、任务队列执行和线程归属检查。

`event_loop_test.cpp` 用真实线程、真实 pipe fd 和真实 eventfd wakeup 路径验证行为。

### EventLoop 的核心接口

```cpp
using Functor = std::function<void()>;

void loop();
void quit() noexcept;
void runInLoop(Functor task);
void queueInLoop(Functor task);
void updateChannel(Channel* channel);
void removeChannel(Channel* channel);
bool isInLoopThread() const noexcept;
void assertInLoopThread() const;
```

接口职责：

- `loop()`：进入事件循环，反复 poll 活跃 fd。
- `quit()`：请求退出循环。
- `runInLoop()`：如果当前线程就是 loop 所属线程，立即执行任务；否则转入队列。
- `queueInLoop()`：把任务放入队列，必要时写 eventfd 唤醒 loop。
- `updateChannel()`：把 `Channel` 的关注事件更新给 `Epoller`。
- `removeChannel()`：把 `Channel` 从 `Epoller` 移除。
- `isInLoopThread()`：判断当前调用是否发生在 loop 所属线程。
- `assertInLoopThread()`：跨线程误用时抛出异常，尽早暴露问题。

### loop() 怎么运行

`loop()` 的主流程是：

```text
while (!quit_) {
    doPendingTasks()
    Epoller::poll(-1)
    for each active channel:
        try channel->handleEvent()
    doPendingTasks()
}
```

开头先执行一次 `doPendingTasks()`，是为了处理“任务已经在 loop 启动前进入队列”的情况，避免一上来就阻塞在 `epoll_wait()`。Step 13 hardening round 3 后，即使 `quit()` 早于第一次 `loop()`，`loop()` 也会先执行已经排队的 pending task，再退出；这保证了 Acceptor 这类关闭清理任务不会因为预先 quit 而永远没人执行。

`poll(-1)` 表示没有事件时一直阻塞。跨线程任务靠 `eventfd` 唤醒它。

Step 13 hardening round 2 后，`loop()` 用 RAII guard 管理 `looping_` 状态：即使 `poll()` 或内部逻辑抛异常，`looping_` 也会复位。活跃 `Channel` 回调和 pending task 会被逐个 `try/catch` 隔离并写日志，单个业务回调抛异常不会直接杀死整个 I/O loop。

后续网络层 cleanup 删除了 `isStopped()` 这个 public API。`EventLoop` 只负责运行、退出和任务投递；`Acceptor`、`TcpServer`、`TimerManager`、`SignalWatcher` 这类 Reactor-owned 对象用 owner-loop-only stop/close 契约保证生命周期，不再通过查询 loop stopped 状态在非 owner 线程做 fallback 清理。

### runInLoop 和 queueInLoop 的区别

`runInLoop()` 更像“如果能立即执行就立即执行”：

```text
当前线程 == loop 所属线程
    -> 直接 task()
否则
    -> queueInLoop(task)
```

`queueInLoop()` 永远把任务放进队列。它适合跨线程调用，也适合在当前 loop 正在执行任务时继续追加任务。

本实现中，如果 `queueInLoop()` 被其他线程调用，或者当前 loop 正在执行 pending tasks，就会调用 `wakeup()`。

### wakeup channel

`EventLoop` 内部创建一个 `eventfd`：

```cpp
eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)
```

然后把它包装成一个普通 `Channel`，注册读事件：

```cpp
wakeup_channel_->setReadCallback([this]() { handleWakeup(); });
wakeup_channel_->enableReading();
```

这样 eventfd 和普通 socket fd 一样，都通过 epoll 管理。区别是 eventfd 只服务于内部任务唤醒，不代表客户端连接。

`wakeup_fd_` 使用 `UniqueFd` 持有，保证 `EventLoop` 构造或注册 wakeup channel 过程中出现异常时，已经创建出来的 eventfd 也有明确关闭责任。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `实现 EventLoop + eventfd 任务投递` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

本 Step 新增：

```cpp
TEST(EventLoopTest, RunInLoopExecutesImmediatelyOnOwnerThread)
TEST(EventLoopTest, QueueInLoopFromOtherThreadWakesAndExecutesTask)
TEST(EventLoopTest, LoopHandlesRegisteredFdEvent)
TEST(EventLoopTest, QueueInLoopRunsMultipleTasksAfterWakeup)
TEST(EventLoopTest, ChannelCallbackExceptionDoesNotEscapeLoop)
TEST(EventLoopTest, LoopRunsQueuedTaskWhenQuitWasRequestedBeforeStart)
```

这些测试覆盖：

- 所属线程调用 `runInLoop()` 立即执行。
- 其他线程调用 `queueInLoop()` 能通过 eventfd 唤醒 loop 并执行任务。
- loop 能处理普通 pipe fd 可读事件，并分发到 `Channel` read callback。
- 多个跨线程任务不会因为 wakeup 合并而丢失。
- 单个 `Channel` 回调抛异常不会逃出并终止 `EventLoop::loop()`。
- 第一次 `loop()` 前如果已经有 pending task 且已经调用过 `quit()`，`loop()` 仍会先执行 pending task 再退出。

TDD RED 阶段，新增接口测试后构建失败在：

```text
EventLoop::Functor does not name a type
runInLoop is not a member of EventLoop
queueInLoop is not a member of EventLoop
```

这说明测试确实覆盖了 Step 12 缺失接口。

运行全部验证：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

只运行本 Step 的测试：

```bash
ctest --test-dir build -R EventLoop --output-on-failure
```

## 8. 验证命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R EventLoop --output-on-failure
```

## 9. 面试表达

### 一句话

我把 EventLoop 作为 Reactor 的调度层：它持有 Epoller，在 loop() 中阻塞等待 fd 事件，拿到活跃 Channel 后调用 handleEvent()。

### 展开说

可以这样讲：

> 我把 `EventLoop` 作为 Reactor 的调度层：它持有 `Epoller`，在 `loop()` 中阻塞等待 fd 事件，拿到活跃 `Channel` 后调用 `handleEvent()`。为了支持跨线程任务投递，我给每个 `EventLoop` 内置一个由 `UniqueFd` 管理的 `eventfd`，并把它也注册成 `Channel`。其他线程调用 `queueInLoop()` 时，先把任务放进 mutex 保护的队列，再写 eventfd 唤醒 epoll。`loop()` 会隔离单个回调异常并保持自身状态可恢复，这样业务线程不会直接修改连接，而是把操作投递回连接所属 I/O 线程执行。

要强调三个边界：

- `EventLoop` 是调度层，不解析协议。
- `EventLoop` 不执行 MySQL / Redis 阻塞业务。
- 跨线程连接操作必须通过 `queueInLoop()` 或 `runInLoop()` 回到所属 I/O 线程。

### 容易被追问

- 为什么不用 pipe 唤醒，而用 eventfd？
- 为什么 `queueInLoop()` 需要 mutex？
- 为什么 `runInLoop()` 在所属线程直接执行？
- 为什么 `queueInLoop()` 有时当前线程也要 wakeup？
- 为什么 `updateChannel()` 要检查线程？

## 10. 面试常见追问

### 为什么不用 pipe 唤醒，而用 eventfd？

`eventfd` 专门用于事件通知，接口简单，只需要读写一个计数器；它比 pipe 更轻量，也更适合线程间 wakeup。

### 为什么 `queueInLoop()` 需要 mutex？

因为它可能被业务线程、其他 I/O 线程或当前 loop 线程调用。任务队列是共享数据，必须保护。

### 为什么 `runInLoop()` 在所属线程直接执行？

如果已经在正确的 I/O 线程，就不需要排队和 wakeup，直接执行更简单，也避免不必要的延迟。

### 为什么 `queueInLoop()` 有时当前线程也要 wakeup？

如果当前 loop 正在执行 pending tasks，任务里又追加了新任务，新任务会进入下一轮队列。写 eventfd 可以防止 loop 执行完当前批任务后直接阻塞，导致新任务无人处理。

### 为什么 `updateChannel()` 要检查线程？

one-loop-per-thread 要求 fd 关注事件只能在所属 loop 线程修改。跨线程直接改 epoll 注册会破坏连接线程归属，后续容易出现竞态。
