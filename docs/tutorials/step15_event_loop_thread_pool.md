# Step 15：实现 EventLoopThread 和 EventLoopThreadPool

## 0. 本 Step 结论

- 目标：本 Step 的目标是把 Step 12 的 EventLoop 放进独立线程里运行，为后续 Step 16 的多 Reactor TcpServer 做准备。
- 前置依赖：依赖 Step 0-14 已建立的工程、协议或运行时基础。
- 主要交付：`实现 EventLoopThread 和 EventLoopThreadPool` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不实现 `TcpServer`

## 1. 为什么需要这个 Step

本 Step 的目标是把 Step 12 的 `EventLoop` 放进独立线程里运行，为后续 Step 16 的多 Reactor `TcpServer` 做准备。

当前还不实现 `TcpServer`。本 Step 只解决一个问题：

```text
一个 I/O 线程如何拥有一个 EventLoop？
多个 I/O 线程如何被统一启动、停止和轮询分配？
```

### 为什么需要 EventLoopThreadPool

Step 12 的 `EventLoop` 只能在创建它的线程里运行。Step 14 的 `Session` 也要求所有 fd 读写都回到所属 `EventLoop`。

如果服务端只有一个 `EventLoop`，主线程既要 `accept()` 新连接，又要处理所有连接读写，很快会成为瓶颈。

多 Reactor 模型把职责拆开：

```text
main EventLoop
  epoll 监听 listen fd
  listen fd 可读
  -> Acceptor
  -> accept new fd
  -> choose one child EventLoop

child EventLoopThread
  -> owns one EventLoop
  -> handles Session read/write
```

这样主 Reactor 只负责接入，子 Reactor 负责连接 I/O。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `实现 EventLoopThread 和 EventLoopThreadPool` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不实现 `TcpServer`
- 不实现 新连接分发
- 不实现 在子 loop 中创建 `Session`
- 不实现 业务线程池
- 不实现 MySQL / Redis
- 不实现 慢客户端高水位策略

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/net/EventLoopThread.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `include/liteim/net/EventLoopThreadPool.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/EventLoopThread.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/EventLoopThreadPool.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/event_loop_thread_header_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/event_loop_thread_pool_header_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/event_loop_thread_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/event_loop_thread_pool_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `docs/process/progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `PROJECT_MEMORY.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

`EventLoopThread.hpp` 定义一个工作线程里的一个 EventLoop：

- 默认构造不启动线程。
- 析构自动 `stop()`，避免后台 I/O 线程泄漏。
- copy 被禁用，线程和 loop 不能复制。
- `startLoop()` 启动 `std::thread`，等待子线程内部构造 `EventLoop` 并发布 `EventLoop*`。重复启动抛 `std::logic_error`；子线程启动异常会通过 `exception_ptr` 回传并重新抛出。
- `stop()` 请求 loop 退出；外部线程调用会 join；在工作线程内调用只发出 quit 请求并返回。
- `running()` 查询当前线程是否仍在运行。
- 关键成员 `mutex_`、`condition_`、`thread_`、`thread_id_`、`loop_`、`started_`、`running_`、`join_started_` 和 `startup_exception_` 共同保证启动发布、停止 join 和异常传播。
- `threadFunc()` 是 private helper，在子线程栈上创建 `EventLoop`，运行 `loop.loop()`，退出时清理状态并通知等待者。

`EventLoopThreadPool.hpp` 定义多个 I/O loop 的管理器：

- 构造函数接收 `base_loop` 和 `thread_count`。`base_loop == nullptr` 抛 `std::invalid_argument`。
- 析构自动 `stop()`。
- `start()` 创建 N 个 `EventLoopThread`，保存每个 child loop 指针。异常时会停止已启动线程。
- `stop()` 停止所有线程，清空 `threads_`、`loops_`，重置 round-robin 下标。
- `getNextLoop()` 要求 pool 已启动；有 child loop 时轮询返回，没有 child loop 时返回 `base_loop_`。
- `loops()` 返回 child loop 观察指针列表。
- `threadCount()` 返回配置的线程数。
- `started()` 查询 pool 状态。
- 关键成员 `threads_` 拥有线程对象，`loops_` 保存观察指针，`next_` 实现 round-robin。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

`EventLoopThreadPool` 给 `TcpServer` 提供 one-loop-per-thread I/O 线程池。base loop 负责 accept，accepted fd 按 round-robin 分配给 child `EventLoop`，连接创建后固定在所属 I/O loop 里读写。

### 2. 上下层调用连接

```text
TcpServer::start()
    -> EventLoopThreadPool::start()
    -> EventLoopThread::startLoop()
    -> child thread constructs EventLoop
    -> publish EventLoop*

Acceptor callback
    -> EventLoopThreadPool::getNextLoop()
    -> io_loop->queueInLoop(create Session)
    -> Session owner loop
```

上游是 `TcpServer`，下游是 `std::thread`、child `EventLoop` 和连接分配策略。

### 3. 整体运行链路

1. `TcpServer::start()` 先启动 I/O 线程池，再创建 Acceptor。
2. [EventLoopThreadPool::start()](../src/net/EventLoopThreadPool.cpp) 按配置数量创建 `EventLoopThread`。
3. 每个 [EventLoopThread::startLoop()](../src/net/EventLoopThread.cpp) 启动子线程并等待 loop 发布。
4. 子线程在 [threadFunc()](../src/net/EventLoopThread.cpp) 栈上构造 `EventLoop`。
5. 子线程把 `EventLoop*` 发布给主线程，然后进入 `loop.loop()`。
6. 新连接到来后，[getNextLoop()](../src/net/EventLoopThreadPool.cpp) round-robin 选择 loop。
7. `TcpServer` 把创建 `Session` 的任务投递给选中的 loop。
8. stop 时，[EventLoopThreadPool::stop()](../src/net/EventLoopThreadPool.cpp) 逐个停止线程并清空观察指针。

### 4. 自身内部运行流程

整体可以看成 4 步：启动线程、发布 loop、轮询分配、停止回收。

核心成员职责：

- `base_loop_` 是 0 线程 fallback 时返回的 loop。
- `thread_count_` 是请求的 I/O 线程数量。
- `threads_` 拥有 `EventLoopThread` 对象。
- `loops_` 保存 child `EventLoop*` 观察指针。
- `next_` 是 round-robin 下标。
- `EventLoopThread::loop_` 是子线程栈上 EventLoop 的观察指针。

核心函数流程：

- `EventLoopThread::startLoop()`：启动线程，等待 condition 直到 loop 发布或异常发生。
- `EventLoopThread::threadFunc()`：子线程构造 EventLoop、发布指针、进入 loop，退出时清理状态。
- `EventLoopThread::stop()`：请求 loop quit，外部线程 join；从 loop 线程调用时不能 join 自己。
- `EventLoopThreadPool::start()`：批量创建线程并保存 loop 指针。
- `EventLoopThreadPool::getNextLoop()`：已启动后按 `next_` 选择 loop；0 线程返回 base loop。
- `EventLoopThreadPool::stop()`：停止所有线程，清空 `threads_` / `loops_`，重置 `next_`。

`getNextLoop()` 可以理解成三种选择：

```text
TcpServer 需要为新连接选择 I/O loop
    ↓
线程池未启动：拒绝分配，暴露使用顺序错误
    ↓
没有子 I/O 线程：直接使用 base_loop_
    ↓
存在子 I/O 线程：按 round-robin 返回下一个 EventLoop
```

`next_` 只在 base loop 线程里推进，所以这里不需要额外加锁；它只负责连接分布，不负责启动或停止具体 `Session`。

### 5. 该项目代码在实际应用中的具体数据例子

服务器配置 `server.io_threads=2` 时，base loop 只负责 accept，子 loop 轮转管理连接。Alice 的连接分到 `io-loop-0` 得到 `session_id=42`，Bob 的连接分到 `io-loop-1` 得到 `session_id=43`。后续 Alice 发 `conversation_id=10011002` 的消息时，读写都留在 Alice 的 owner loop，跨线程响应通过 `queueInLoop()` 回来。

## 6. 关键实现点

### EventLoopThread 的职责

`EventLoopThread` 表示：

```text
一个 std::thread 里面运行一个 EventLoop
```

核心接口：

```cpp
class EventLoopThread {
public:
    EventLoop* startLoop();
    void stop() noexcept;
    bool running() const noexcept;
};
```

### `startLoop()`

`startLoop()` 创建工作线程，并等待工作线程里的 `EventLoop` 构造完成。

这里必须在线程函数内部构造 `EventLoop`，不能在主线程构造后移动进去。原因是 `EventLoop` 会记录创建线程的 `thread_id_`，后续 `assertInLoopThread()` 依赖这个线程归属判断。

返回的 `EventLoop*` 是观察指针，不拥有 loop。它只在 `EventLoopThread` 运行期间有效。

### `stop()`

`stop()` 做三件事：

1. 调用 `EventLoop::quit()`。
2. 唤醒阻塞在 `epoll_wait()` 里的 loop。
3. 如果调用者不是这个工作线程本身，就 `join()` 工作线程。

`quit()` 调用放在 `EventLoopThread` 自身 mutex 保护内，避免 loop 线程刚退出、局部 `EventLoop` 即将析构时，外部线程还拿着过期裸指针调用。

Step 17 后的 review hardening 明确了一个边界：如果 `stop()` 在这个 `EventLoopThread` 自己的工作线程里被调用，它只负责发出 `quit()` 请求并返回，不 detach 自己，也不在 `stop()` 里清空 `loop_` / `running_`。状态清理统一放在 `threadFunc()` 末尾，owner 线程后续再调用 `stop()` 或析构时完成 join。这样避免 self-stop 后线程还在跑，而 `EventLoopThread` 对象已经被析构导致 UAF。

### 析构

`EventLoopThread` 析构时自动调用 `stop()`，所以测试或后续 `TcpServer` 即使忘记显式 stop，也不会留下后台 I/O 线程。

### EventLoopThreadPool 的职责

`EventLoopThreadPool` 表示：

```text
多个 EventLoopThread 的管理器
```

核心接口：

```cpp
class EventLoopThreadPool {
public:
    void start();
    void stop() noexcept;
    EventLoop* getNextLoop();

    const std::vector<EventLoop*>& loops() const noexcept;
    std::size_t threadCount() const noexcept;
    bool started() const noexcept;
};
```

### `start()`

`start()` 根据构造时传入的线程数启动 N 个 `EventLoopThread`，并保存每个线程返回的 child `EventLoop*`。

### `getNextLoop()`

`getNextLoop()` 使用 round-robin：

```text
loop0 -> loop1 -> loop2 -> loop0 -> loop1 -> ...
```

这样后续 `TcpServer` 每 accept 一个新连接，就可以把这个连接固定分配给某个子 Reactor。

### 0 线程 fallback

如果 `thread_count == 0`，线程池不创建子线程，`getNextLoop()` 返回 base loop。

这给后续调试和单 Reactor fallback 留了统一接口：

```text
child loops exist -> return child loop
no child loop      -> return base loop
```

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `实现 EventLoopThread 和 EventLoopThreadPool` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

新增测试文件：

```text
tests/net/event_loop_thread_header_test.cpp
tests/net/event_loop_thread_pool_header_test.cpp
tests/net/event_loop_thread_test.cpp
tests/net/event_loop_thread_pool_test.cpp
```

测试用例：

```cpp
TEST(ReactorInterfaceTest, EventLoopThreadHeaderIsSelfContained)
TEST(ReactorInterfaceTest, EventLoopThreadPoolHeaderIsSelfContained)
TEST(EventLoopThreadTest, StartLoopCreatesLoopOnWorkerThread)
TEST(EventLoopThreadTest, StopWithoutStartIsNoop)
TEST(EventLoopThreadTest, OwnerStopWaitsAfterStopIsRequestedInsideLoop)
TEST(EventLoopThreadTest, DestructorStopsRunningLoop)
TEST(EventLoopThreadPoolTest, StartCreatesRequestedNumberOfLoops)
TEST(EventLoopThreadPoolTest, GetNextLoopUsesRoundRobinOrder)
TEST(EventLoopThreadPoolTest, ZeroThreadsReturnsBaseLoop)
TEST(EventLoopThreadPoolTest, ChildLoopsRunTasksOnDistinctThreads)
```

这些测试覆盖：

- 头文件可以独立 include。
- `EventLoopThread` 返回的 loop 运行在工作线程。
- `stop()` 可以安全结束线程。
- 工作线程内部触发 `stop()` 后，owner 线程再次 `stop()` 会等待线程函数真正结束。
- 析构可以自动停止仍在运行的 loop。
- 线程池能启动指定数量的 child loops。
- round-robin 分配顺序正确。
- 0 线程时返回 base loop。
- 多个 child loops 确实运行在不同线程。

TDD RED 阶段，新增测试后构建失败在：

```text
fatal error: liteim/net/EventLoopThread.hpp: No such file or directory
```

这说明测试确实覆盖了 Step 15 缺失接口。

全量验证：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

只运行本 Step：

```bash
ctest --test-dir build --output-on-failure -R "EventLoopThread|EventLoopThreadPool|ReactorInterfaceTest.EventLoopThread"
```

## 8. 验证命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R "EventLoopThread|EventLoopThreadPool|ReactorInterfaceTest.EventLoopThread"
```

## 9. 面试表达

### 一句话

我把 Reactor 线程模型拆成两个类：EventLoopThread 负责在线程内部构造并运行一个 EventLoop，保证 loop 的线程归属正确；EventLoopThreadPool 负责启动多个 I/O loops，并用 roundrobin 给后续连接分配固定的 I/O 线程。

### 展开说

可以这样讲：

> 我把 Reactor 线程模型拆成两个类：`EventLoopThread` 负责在线程内部构造并运行一个 `EventLoop`，保证 loop 的线程归属正确；`EventLoopThreadPool` 负责启动多个 I/O loops，并用 round-robin 给后续连接分配固定的 I/O 线程。这样主 Reactor 后续只负责 accept，连接读写会固定落到某个子 Reactor，避免一个连接在多个 I/O 线程之间迁移。

重点边界：

- `EventLoop` 不能跨线程随便调用。
- 连接创建后要固定绑定到一个 I/O loop。
- `EventLoopThread` 返回的是观察指针，线程对象才拥有 loop 生命周期。
- `stop()` 必须唤醒 `epoll_wait()`，否则线程可能无法退出。
- 本 Step 只做线程池，不做 `TcpServer`。

### 容易被追问

- 为什么 EventLoop 要在线程函数里构造？
- 为什么 getNextLoop() 用 round-robin？
- 为什么 0 线程返回 base loop？
- 为什么 stop() 要调用 quit() 后 join？

## 10. 面试常见追问

### 为什么 EventLoop 要在线程函数里构造？

因为 `EventLoop` 会记录创建它的线程 id。只有在线程函数里构造，`assertInLoopThread()` 才能判断出真正的 I/O 线程。

### 为什么 getNextLoop() 用 round-robin？

第一版只需要简单均匀分配连接。round-robin 可预测、容易测试，不引入负载统计复杂度。

### 为什么 0 线程返回 base loop？

这样 `TcpServer` 后续可以用同一套接口支持单 Reactor 调试模式和多 Reactor 模式。

### 为什么 stop() 要调用 quit() 后 join？

`quit()` 让 loop 从 `epoll_wait()` 中醒来并退出循环，`join()` 保证工作线程真正结束，避免后台线程悬挂。
