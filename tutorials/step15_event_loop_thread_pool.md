# Step 15：实现 EventLoopThread 和 EventLoopThreadPool

本 Step 的目标是把 Step 12 的 `EventLoop` 放进独立线程里运行，为后续 Step 16 的多 Reactor `TcpServer` 做准备。

当前还不实现 `TcpServer`。本 Step 只解决一个问题：

```text
一个 I/O 线程如何拥有一个 EventLoop？
多个 I/O 线程如何被统一启动、停止和轮询分配？
```

## 1. 为什么需要 EventLoopThreadPool

Step 12 的 `EventLoop` 只能在创建它的线程里运行。Step 14 的 `Session` 也要求所有 fd 读写都回到所属 `EventLoop`。

如果服务端只有一个 `EventLoop`，主线程既要 `accept()` 新连接，又要处理所有连接读写，很快会成为瓶颈。

多 Reactor 模型把职责拆开：

```text
main EventLoop
  -> Acceptor
  -> accept new fd
  -> choose one child EventLoop

child EventLoopThread
  -> owns one EventLoop
  -> handles Session read/write
```

这样主 Reactor 只负责接入，子 Reactor 负责连接 I/O。

## 2. 本 Step 新增文件

```text
include/liteim/net/EventLoopThread.hpp
include/liteim/net/EventLoopThreadPool.hpp
src/net/EventLoopThread.cpp
src/net/EventLoopThreadPool.cpp
tests/net/event_loop_thread_header_test.cpp
tests/net/event_loop_thread_pool_header_test.cpp
tests/net/event_loop_thread_test.cpp
tests/net/event_loop_thread_pool_test.cpp
```

同时更新：

```text
src/net/CMakeLists.txt
tests/CMakeLists.txt
README.md
docs/architecture.md
docs/project_layout.md
docs/roadmap.md
tutorials/README.md
task_plan.md
findings.md
progress.md
PROJECT_MEMORY.md
```

## 3. EventLoopThread 的职责

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
3. `join()` 工作线程。

`quit()` 调用放在 `EventLoopThread` 自身 mutex 保护内，避免 loop 线程刚退出、局部 `EventLoop` 即将析构时，外部线程还拿着过期裸指针调用。

### 析构

`EventLoopThread` 析构时自动调用 `stop()`，所以测试或后续 `TcpServer` 即使忘记显式 stop，也不会留下后台 I/O 线程。

## 4. EventLoopThreadPool 的职责

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

## 5. 本 Step 不做什么

本 Step 不实现：

- `TcpServer`
- 新连接分发
- 在子 loop 中创建 `Session`
- 业务线程池
- MySQL / Redis
- 慢客户端高水位策略

这些属于后续 Step。

## 6. 测试设计

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

## 7. 如何运行测试

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

当前 Step 15 完成后，CTest 预期通过 133 个测试，其中 9 个是新增的 Step 15 测试。

## 8. 面试时怎么讲

可以这样讲：

> 我把 Reactor 线程模型拆成两个类：`EventLoopThread` 负责在线程内部构造并运行一个 `EventLoop`，保证 loop 的线程归属正确；`EventLoopThreadPool` 负责启动多个 I/O loops，并用 round-robin 给后续连接分配固定的 I/O 线程。这样主 Reactor 后续只负责 accept，连接读写会固定落到某个子 Reactor，避免一个连接在多个 I/O 线程之间迁移。

重点边界：

- `EventLoop` 不能跨线程随便调用。
- 连接创建后要固定绑定到一个 I/O loop。
- `EventLoopThread` 返回的是观察指针，线程对象才拥有 loop 生命周期。
- `stop()` 必须唤醒 `epoll_wait()`，否则线程可能无法退出。
- 本 Step 只做线程池，不做 `TcpServer`。

## 9. 面试常见追问

**为什么 EventLoop 要在线程函数里构造？**

因为 `EventLoop` 会记录创建它的线程 id。只有在线程函数里构造，`assertInLoopThread()` 才能判断出真正的 I/O 线程。

**为什么 getNextLoop() 用 round-robin？**

第一版只需要简单均匀分配连接。round-robin 可预测、容易测试，不引入负载统计复杂度。

**为什么 0 线程返回 base loop？**

这样 `TcpServer` 后续可以用同一套接口支持单 Reactor 调试模式和多 Reactor 模式。

**为什么 stop() 要调用 quit() 后 join？**

`quit()` 让 loop 从 `epoll_wait()` 中醒来并退出循环，`join()` 保证工作线程真正结束，避免后台线程悬挂。

## 10. 提交信息

```text
feat(net): add event loop thread pool
```
