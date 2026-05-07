# Step 12：实现 EventLoop + eventfd 任务投递

`EventLoop` 是 Reactor 的调度层。Step 10 已经有 `Epoller`，Step 11 已经有 `Channel::handleEvent()`，Step 12 把它们串成真正能运行的事件循环：

```text
EventLoop::loop()
    -> Epoller::poll()
    -> active Channel list
    -> Channel::handleEvent()
```

同时，本 Step 从一开始加入 `eventfd`，让其他线程可以通过 `queueInLoop()` 唤醒阻塞在 `epoll_wait()` 的 I/O 线程。

## 1. 为什么需要 EventLoop

`Epoller` 只封装 Linux epoll 系统调用，`Channel` 只保存 fd 事件和回调。它们都不应该负责“循环调度”。

`EventLoop` 的职责是：

- 持有一个 `Epoller`。
- 在 `loop()` 中阻塞等待 fd 事件。
- 遍历活跃 `Channel`。
- 调用 `Channel::handleEvent()`。
- 管理跨线程投递过来的任务。
- 用 `eventfd` 唤醒阻塞中的 `epoll_wait()`。

后续每个 I/O 线程都会拥有一个自己的 `EventLoop`，这就是 one-loop-per-thread 的基础。

## 2. 本 Step 修改的文件

```text
include/liteim/net/EventLoop.hpp
src/net/EventLoop.cpp
tests/net/event_loop_header_test.cpp
tests/net/event_loop_test.cpp
tutorials/step12_event_loop.md
```

`EventLoop.hpp` 新增 `Functor`、`runInLoop()` 和 `queueInLoop()`，并补充内部任务队列、mutex、wakeup fd 和 wakeup channel。

`EventLoop.cpp` 实现阻塞事件循环、eventfd 唤醒、任务队列执行和线程归属检查。

`event_loop_test.cpp` 用真实线程、真实 pipe fd 和真实 eventfd wakeup 路径验证行为。

## 3. EventLoop 的核心接口

```cpp
using Functor = std::function<void()>;

void loop();
void quit() noexcept;
void runInLoop(Functor task);
void queueInLoop(Functor task);
void updateChannel(Channel* channel);
void removeChannel(Channel* channel);
bool isInLoopThread() const noexcept;
bool isStopped() const noexcept;
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
- `isStopped()`：判断 `loop()` 是否已经进入并返回，用于 `Acceptor::close()` 等跨线程清理判断 fallback 路径。刚构造但尚未进入 `loop()` 的对象不算 stopped，即使提前调用过 `quit()` 也不算 stopped。
- `assertInLoopThread()`：跨线程误用时抛出异常，尽早暴露问题。

## 4. loop() 怎么运行

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

Step 13 hardening round 3 进一步明确 `isStopped()` 的语义：它不是“当前没在 `loop()` 里”，而是“`loop()` 已经进入并退出”。这能区分几种状态：

| 状态 | `loop_exited_` | `looping_` | `isStopped()` |
| --- | --- | --- | --- |
| 刚构造，还没启动 | false | false | false |
| 第一次 `loop()` 前已经调用 `quit()` | false | false | false |
| `loop()` 正在运行 | false | true | false |
| `loop()` 已返回 | true | false | true |

这个区分服务于 `Acceptor::close()`：loop 还没启动但马上会启动时，close 仍应投递回 owner loop 删除 `Channel`；只有 loop 已经退出后，才走直接释放资源的 fallback。

## 5. 为什么需要 eventfd

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

## 6. runInLoop 和 queueInLoop 的区别

`runInLoop()` 更像“如果能立即执行就立即执行”：

```text
当前线程 == loop 所属线程
    -> 直接 task()
否则
    -> queueInLoop(task)
```

`queueInLoop()` 永远把任务放进队列。它适合跨线程调用，也适合在当前 loop 正在执行任务时继续追加任务。

本实现中，如果 `queueInLoop()` 被其他线程调用，或者当前 loop 正在执行 pending tasks，就会调用 `wakeup()`。

## 7. 线程边界

`EventLoop` 在构造时记录：

```cpp
thread_id_(std::this_thread::get_id())
```

这表示这个 loop 属于构造它的线程。

后续要求：

- fd 事件回调在所属 loop 线程执行。
- `updateChannel()` 和 `removeChannel()` 必须在所属 loop 线程调用。
- 其他线程不能直接改连接状态，只能通过 `queueInLoop()` 投递任务。

这为后续 `Session` / `TcpConnection` 的线程安全打基础。

## 8. wakeup channel

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

## 9. 测试设计

本 Step 新增：

```cpp
TEST(EventLoopTest, RunInLoopExecutesImmediatelyOnOwnerThread)
TEST(EventLoopTest, QueueInLoopFromOtherThreadWakesAndExecutesTask)
TEST(EventLoopTest, LoopHandlesRegisteredFdEvent)
TEST(EventLoopTest, QueueInLoopRunsMultipleTasksAfterWakeup)
TEST(EventLoopTest, ChannelCallbackExceptionDoesNotEscapeLoop)
TEST(EventLoopTest, IsStoppedIsFalseBeforeLoopEverStarts)
TEST(EventLoopTest, IsStoppedIsFalseAfterQuitBeforeLoopEverStarts)
TEST(EventLoopTest, LoopRunsQueuedTaskWhenQuitWasRequestedBeforeStart)
TEST(EventLoopTest, IsStoppedBecomesTrueAfterLoopReturns)
```

这些测试覆盖：

- 所属线程调用 `runInLoop()` 立即执行。
- 其他线程调用 `queueInLoop()` 能通过 eventfd 唤醒 loop 并执行任务。
- loop 能处理普通 pipe fd 可读事件，并分发到 `Channel` read callback。
- 多个跨线程任务不会因为 wakeup 合并而丢失。
- 单个 `Channel` 回调抛异常不会逃出并终止 `EventLoop::loop()`。
- `loop()` 启动前 `isStopped()` 为 false，避免上层误认为 loop 已经结束。
- 第一次 `loop()` 前即使调用过 `quit()`，`isStopped()` 仍然为 false。
- 第一次 `loop()` 前如果已经有 pending task 且已经调用过 `quit()`，`loop()` 仍会先执行 pending task 再退出。
- `loop()` 返回后 `isStopped()` 为 true，方便上层对象判断停止后的清理路径。

TDD RED 阶段，新增接口测试后构建失败在：

```text
EventLoop::Functor does not name a type
runInLoop is not a member of EventLoop
queueInLoop is not a member of EventLoop
```

这说明测试确实覆盖了 Step 12 缺失接口。

## 10. 如何运行测试

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

Step 12 初次完成时 CTest 通过 92 个测试，其中 4 个是新增的 `EventLoopTest`。Step 13 hardening round 2 又补充了 2 个 `EventLoopTest`，用于验证回调异常隔离和 `isStopped()` 停止状态。Step 13 hardening round 3 追加 `IsStoppedIsFalseBeforeLoopEverStarts`、`IsStoppedIsFalseAfterQuitBeforeLoopEverStarts` 和 `LoopRunsQueuedTaskWhenQuitWasRequestedBeforeStart`，防止把“尚未启动”和“已经停止”混成同一种状态，并确保预先 quit 时仍执行已排队清理任务。

## 11. 面试时怎么讲

可以这样讲：

> 我把 `EventLoop` 作为 Reactor 的调度层：它持有 `Epoller`，在 `loop()` 中阻塞等待 fd 事件，拿到活跃 `Channel` 后调用 `handleEvent()`。为了支持跨线程任务投递，我给每个 `EventLoop` 内置一个由 `UniqueFd` 管理的 `eventfd`，并把它也注册成 `Channel`。其他线程调用 `queueInLoop()` 时，先把任务放进 mutex 保护的队列，再写 eventfd 唤醒 epoll。`loop()` 会隔离单个回调异常并保持自身状态可恢复，这样业务线程不会直接修改连接，而是把操作投递回连接所属 I/O 线程执行。

要强调三个边界：

- `EventLoop` 是调度层，不解析协议。
- `EventLoop` 不执行 MySQL / Redis 阻塞业务。
- 跨线程连接操作必须通过 `queueInLoop()` 或 `runInLoop()` 回到所属 I/O 线程。

## 12. 面试常见追问

**为什么不用 pipe 唤醒，而用 eventfd？**

`eventfd` 专门用于事件通知，接口简单，只需要读写一个计数器；它比 pipe 更轻量，也更适合线程间 wakeup。

**为什么 `queueInLoop()` 需要 mutex？**

因为它可能被业务线程、其他 I/O 线程或当前 loop 线程调用。任务队列是共享数据，必须保护。

**为什么 `runInLoop()` 在所属线程直接执行？**

如果已经在正确的 I/O 线程，就不需要排队和 wakeup，直接执行更简单，也避免不必要的延迟。

**为什么 `queueInLoop()` 有时当前线程也要 wakeup？**

如果当前 loop 正在执行 pending tasks，任务里又追加了新任务，新任务会进入下一轮队列。写 eventfd 可以防止 loop 执行完当前批任务后直接阻塞，导致新任务无人处理。

**为什么 `updateChannel()` 要检查线程？**

one-loop-per-thread 要求 fd 关注事件只能在所属 loop 线程修改。跨线程直接改 epoll 注册会破坏连接线程归属，后续容易出现竞态。
