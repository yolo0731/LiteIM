# Step 17：实现业务 ThreadPool

## 0. 本 Step 结论

- 目标：本 Step 的目标是新增 liteim_concurrency::ThreadPool，让后续登录、消息、历史查询、MySQL 和 Redis 操作有一个独立的业务执行位置。
- 前置依赖：依赖 Step 0-16 已建立的工程、协议或运行时基础。
- 主要交付：`实现业务 ThreadPool` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不实现 `future` 返回值。

## 1. 为什么需要这个 Step

本 Step 的目标是新增 `liteim_concurrency::ThreadPool`，让后续登录、消息、历史查询、MySQL 和 Redis 操作有一个独立的业务执行位置。

到 Step 16 为止，网络层已经能完成：

```text
TcpServer
  -> Acceptor accepts fd
  -> EventLoopThreadPool chooses I/O loop
  -> Session reads/writes Packet
```

但是 I/O 线程不能直接执行 MySQL、Redis、密码哈希或历史消息查询。原因很简单：这些操作可能阻塞，一旦放在 I/O loop 中，整个连接读写都会被拖住。

Step 17 只解决一个问题：

```text
阻塞业务任务应该放到哪里执行？
```

答案是业务线程池。

### 为什么需要业务 ThreadPool

Reactor I/O 线程应该做轻量工作：

- accept 新连接。
- 读写 socket。
- 解码 Packet / TLV。
- 把响应投递回连接所属 `EventLoop`。

业务线程池负责可能慢的工作：

- MySQL 查询和写入。
- Redis 在线状态、未读计数、限流状态。
- 密码哈希。
- 历史消息查询。
- 后续业务服务里的较重计算。

正确边界是：

```text
I/O thread
  -> decode Packet
  -> submit business task to ThreadPool

business worker
  -> run blocking business logic
  -> queue response back to Session's EventLoop
```

业务线程不能直接改 `Session`，因为 `Session` 属于某个 I/O loop。跨线程响应仍然要回到 `EventLoop::queueInLoop()` 或 `Session::sendPacket()`。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `实现业务 ThreadPool` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不实现 `future` 返回值。
- 不实现 任务优先级。
- 不实现 动态扩缩容。
- 不实现 work stealing。
- 不实现 定时任务。
- 不实现 MySQL / Redis 接入。
- 不实现 MessageRouter。
- 不实现 登录、注册、聊天业务。
- 不实现 直接操作 `Session`。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/concurrency/ThreadPool.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/concurrency/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/concurrency/ThreadPool.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/concurrency/thread_pool_header_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/concurrency/thread_pool_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tutorials/step17_thread_pool.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `PROJECT_MEMORY.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

### `Task`

```cpp
using Task = std::function<void()>;
```

第一版业务线程池只接收无返回值任务。

这里不做 `submit()` 返回 `future`，原因是后续业务响应不应该让 I/O 线程同步等待结果。业务处理完以后，应主动把响应投递回连接所属 loop。

### 构造函数

```cpp
explicit ThreadPool(std::size_t worker_count);
```

`worker_count` 是固定 worker 数。

本 Step 不做动态扩缩容，也不做 work stealing。固定大小更容易理解，也更符合当前 IM 服务端第一版需求。

### `start()`

```cpp
Status start();
```

启动 worker 线程。

失败场景：

- `worker_count == 0` 返回 `InvalidArgument`。
- 重复启动返回 `InvalidArgument`。
- 创建线程失败返回 `InternalError`。

`start()` 不在构造函数中自动执行，是为了让服务端后续能明确控制启动顺序：先准备配置和服务，再启动线程池和网络入口。

### `submit()`

```cpp
Status submit(Task task);
```

把任务放进队列，并唤醒一个 worker。

失败场景：

- 空任务返回 `InvalidArgument`。
- 线程池未启动返回 `InvalidArgument`。
- `stop()` 之后不再接收新任务，返回 `InvalidArgument`。

### `stop()`

```cpp
void stop() noexcept;
```

停止线程池。

它的语义是：

1. 停止接收新任务。
2. 唤醒所有 worker。
3. 已经入队的任务继续执行完。
4. 等待 worker 线程退出。

这叫优雅退出。它不是“立刻丢弃队列”。

内部用单一 `running_` 表示线程池是否运行：

- `start()` 把 `running_` 置为 true。
- `submit()` 只在 `running_ == true` 时接收任务。
- `stop()` 把 `running_` 置为 false，并唤醒 worker。
- `workerLoop()` 在 `running_ == false` 且队列为空时退出。

如果 `stop()` 是由某个 worker 任务内部调用的，这个 worker 不能 join 自己。当前实现通过 worker-local 标记识别 self-stop，只设置 `running_ = false` 并唤醒其他 worker，然后直接返回；随后由 owner 线程再次调用 `stop()` 或通过析构完成 join 和清理。

如果多个外部线程同时调用 `stop()`，join/cleanup 阶段会由 `stop_mutex_` 串行化，避免多个线程同时对同一个 `std::thread` 调用 `join()`。

这个问题的核心结论已经收敛到 `findings.md`：worker 内部 stop 只发出停止请求，最终 join/cleanup 仍由 owner 线程完成。

### 状态查询

```cpp
std::size_t workerCount() const noexcept;
std::size_t pendingTaskCount() const;
bool started() const noexcept;
```

- `workerCount()` 返回固定 worker 数。
- `pendingTaskCount()` 返回还在队列里等待执行的任务数，不包含正在执行的任务。
- `started()` 表示线程池当前是否处于启动状态。

### 关键成员变量

`ThreadPool` 的关键成员包括：

- `worker_count_`：固定 worker 数，构造后不变化。
- `mutex_`：保护任务队列和 workers 容器相关状态。
- `stop_mutex_`：串行化外部 stop/join 清理。
- `condition_`：worker 等待新任务或停止信号。
- `tasks_`：FIFO 任务队列。
- `workers_`：实际 `std::thread` 容器。
- `running_`：是否继续接收新任务，也是 worker 退出条件的一部分。

private helper：

- `workerLoop()` 是每个 worker 的主循环。它等待任务，取出队首任务，在 catch 边界内执行，任务异常不会杀死线程池。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

`ThreadPool` 是后续业务层执行阻塞工作的地方：MySQL 查询、Redis 操作、密码哈希和历史消息读取都不能跑在 I/O loop 里。I/O 线程只做收发和轻量协议解析，业务任务完成后再通过 `Session::sendPacket()` 或 `EventLoop::queueInLoop()` 回到连接 owner loop。

### 2. 上下层调用连接

```text
Session MessageCallback / TcpServer business callback
    -> business ThreadPool::submit(task)
    -> workerLoop()
    -> blocking MySQL / Redis / CPU work
    -> Session::sendPacket(response)
    -> Session owner EventLoop
```

上游是网络层收到的 Packet 和未来 service，下游是 worker 线程、任务队列和回投到 I/O loop 的响应。

### 3. 整体运行链路

1. server 或 service 创建固定 worker 数的 `ThreadPool`。
2. 启动期调用 [ThreadPool::start()](../src/concurrency/ThreadPool.cpp)。
3. I/O loop 收到业务 Packet 后调用 [submit()](../src/concurrency/ThreadPool.cpp)。
4. `submit()` 把任务放入队列并 `notify_one()`。
5. worker 在 [workerLoop()](../src/concurrency/ThreadPool.cpp) 中等待 condition variable。
6. worker 取出任务，在锁外执行阻塞业务逻辑。
7. 任务完成后通过 `Session::sendPacket()` 把响应安全投递回 owner loop。
8. 停止时，[stop()](../src/concurrency/ThreadPool.cpp) 停止接收新任务，唤醒 worker，等待队列排空并 join。

### 4. 自身内部运行流程

整体可以看成 4 步：启动 worker、提交任务、worker 消费、停止清理。

核心成员职责：

- `worker_count_` 是固定 worker 数。
- `workers_` 拥有 `std::thread`。
- `tasks_` 是等待执行的 `deque<Task>`。
- `mutex_` / `condition_` 保护队列并让 worker 休眠。
- `running_` 表示是否接收新任务。
- `stop_mutex_` 串行化外部 stop / join。
- `thread_local current_thread_pool` 识别 worker 内部 self-stop。

核心函数流程：

- `start()`：拒绝 0 worker 和重复启动，置 running 后创建固定 worker。
- `submit()`：拒绝空任务和非 running 状态，入队后唤醒一个 worker。
- `workerLoop()`：等待“停止或有任务”，取队首任务，锁外执行，吞掉单个任务异常。
- `stop()`：置 running false，唤醒所有 worker；外部线程 join，worker 内部调用只发停止请求。
- `pendingTaskCount()`：只统计仍在队列等待的任务，不统计正在执行的任务。

`workerLoop()` 可以理解成“等待任务、取出任务、锁外执行”：

```text
worker 线程启动
    ↓
标记当前线程属于这个 ThreadPool
    ↓
没有任务时在 condition_variable 上休眠
    ↓
拿到队首任务后立刻离开临界区
    ↓
在线程池锁外执行 task
    ↓
单个 task 异常只影响当前任务
    ↓
stop 后队列清空，worker 退出
```

这个流程保证业务任务不会持有 `mutex_` 执行，避免一个慢 MySQL / Redis 操作堵住其他 worker 取任务；任务异常也不会让整个线程池退出。

### 5. 该项目代码在实际应用中的具体数据例子

后续 ChatService 收到 Alice 发给 Bob 的消息后，会把阻塞 MySQL 操作投递给 business `ThreadPool`：任务保存 `sender_id=1001`、`receiver_id=1002`、`conversation_id=10011002`、`message_text=hello bob`。保存完成后再用 `queueInLoop()` 把响应投回对应 I/O loop，避免 Reactor 线程卡在数据库调用上。

## 6. 关键实现点

### ThreadPool.cpp 实现思路

核心数据结构是：

```text
mutex
stop_mutex
condition_variable
deque<Task>
vector<thread>
running flag
```

### `submit()` 为什么要加锁

多个线程可能同时提交业务任务，所以任务队列必须由 `mutex` 保护。

流程是：

```text
submit(task)
  -> lock mutex
  -> check running
  -> push task into deque
  -> unlock
  -> notify_one worker
```

`notify_one()` 只唤醒一个 worker，因为一个任务只需要一个 worker 执行。

### worker 如何等待任务

worker 线程循环执行：

```text
workerLoop()
  -> wait until not running or queue not empty
  -> if queue empty and not running: exit
  -> pop one task
  -> run task
```

这里的关键是 stop 后仍然会先把队列里的任务取完。只有队列为空并且 `running_ == false` 时，worker 才退出。

### 为什么任务异常不能杀死 worker

业务任务里以后会有数据库、缓存、业务校验和响应构造。某个任务抛异常不应该让整个 worker 线程消失。

所以当前实现会捕获任务异常，保证 worker 继续处理后续任务。后续业务层应该把业务失败转换成 `Status` 或错误响应 Packet，而不是依赖异常穿透线程边界。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `实现业务 ThreadPool` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

新增测试文件：

```text
tests/concurrency/thread_pool_header_test.cpp
tests/concurrency/thread_pool_test.cpp
```

测试用例：

```cpp
TEST(ConcurrencyInterfaceTest, ThreadPoolHeaderIsSelfContained)
TEST(ThreadPoolTest, StartRejectsZeroWorkers)
TEST(ThreadPoolTest, SubmitExecutesTask)
TEST(ThreadPoolTest, MultipleWorkersRunConcurrently)
TEST(ThreadPoolTest, StopRejectsNewTasks)
TEST(ThreadPoolTest, StopCalledFromWorkerRequiresOwnerCleanupBeforeRestart)
TEST(ThreadPoolTest, ConcurrentStopCallsAreSerialized)
TEST(ThreadPoolTest, DestructorWaitsForQueuedTasks)
TEST(ThreadPoolTest, PendingTaskCountTracksQueuedTasks)
```

这些测试覆盖：

- 头文件可以独立 include。
- 0 worker 会被拒绝。
- `submit()` 的任务确实会执行。
- 多 worker 能并发运行，而不是只有一个线程消费任务。
- `stop()` 后不再接受新任务。
- worker 内部调用 `stop()` 后，必须等 owner 线程完成清理后才能重启线程池。
- 多个外部线程并发调用 `stop()` 时不会同时 join 同一个 worker。
- 析构会等待已经入队的任务结束。
- `pendingTaskCount()` 只统计等待中的任务。

运行命令：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "ThreadPool|ConcurrencyInterfaceTest.ThreadPool"
```

全量验证：

```bash
ctest --test-dir build --output-on-failure
```

## 8. 验证命令

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "ThreadPool|ConcurrencyInterfaceTest.ThreadPool"
ctest --test-dir build --output-on-failure
```

## 9. 面试表达

### 一句话

LiteIM 的网络层采用 oneloopperthread Reactor。

### 展开说

可以这样讲：

> LiteIM 的网络层采用 one-loop-per-thread Reactor。I/O 线程只负责 epoll、socket 读写和协议编解码，不能执行 MySQL 或 Redis 这种阻塞操作。Step 17 单独实现了一个固定大小业务线程池，后续登录、发消息、历史查询等业务会以任务形式提交到 `ThreadPool`。业务线程处理完后不会直接操作 `Session`，而是把响应投递回连接所属的 `EventLoop`，保证连接生命周期和 socket 写入仍然由 I/O 线程管理。

设计取舍：

- 固定 worker 数，避免第一版引入动态扩缩容复杂度。
- `submit()` 不返回 `future`，避免 I/O 线程等待业务结果。
- `stop()` 采用优雅退出，已经入队的任务会执行完。
- 队列用 `mutex + condition_variable`，简单直接，足够支撑后续业务服务接入。
- 任务异常被 worker 捕获，避免单个业务任务杀死工作线程。

### 容易被追问

- 为什么不用 `std::async`？
- 为什么 `submit()` 不返回 `future`？
- `pendingTaskCount()` 为什么不统计正在执行的任务？
- 业务线程能直接调用 `Session::sendPacket()` 吗？
- stop 时为什么不丢弃队列？

## 10. 面试常见追问

### 为什么不用 `std::async`？

`std::async` 的调度策略和线程复用不适合做长期服务端业务池。IM 服务端需要固定数量 worker、明确停止语义和队列长度诊断。

### 为什么 `submit()` 不返回 `future`？

因为 I/O 线程不应该等待业务结果。业务完成后应该异步把响应投递回连接所属 loop，而不是让调用方阻塞等待。

### `pendingTaskCount()` 为什么不统计正在执行的任务？

它表达的是队列积压量，用来观察业务线程池是否被提交压力打满。正在执行的任务已经离开队列，不属于 pending。

### 业务线程能直接调用 `Session::sendPacket()` 吗？

可以调用 `Session::sendPacket()` 这个跨线程安全入口，因为它内部会把真实写操作投递回所属 I/O loop。但业务线程不能直接改 `Session` 内部状态或操作 fd。

### stop 时为什么不丢弃队列？

第一版选择优雅退出，避免已经接收的业务任务被静默丢掉。后续如果需要快速关停，可以单独设计 cancel / deadline 语义。
