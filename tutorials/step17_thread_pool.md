# Step 17：实现业务 ThreadPool

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

## 1. 为什么需要业务 ThreadPool

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

## 2. 本 Step 新增文件

```text
include/liteim/concurrency/ThreadPool.hpp
src/concurrency/CMakeLists.txt
src/concurrency/ThreadPool.cpp
tests/concurrency/thread_pool_header_test.cpp
tests/concurrency/thread_pool_test.cpp
tutorials/step17_thread_pool.md
```

同时更新：

```text
src/CMakeLists.txt
tests/CMakeLists.txt
README.md
task_plan.md
findings.md
progress.md
PROJECT_MEMORY.md
```

## 3. ThreadPool.hpp 接口说明

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

## ThreadPool 的作用场景和运行流程

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
2. 启动期调用 [ThreadPool::start()](../src/concurrency/ThreadPool.cpp#L40)。
3. I/O loop 收到业务 Packet 后调用 [submit()](../src/concurrency/ThreadPool.cpp#L79)。
4. `submit()` 把任务放入队列并 `notify_one()`。
5. worker 在 [workerLoop()](../src/concurrency/ThreadPool.cpp#L146) 中等待 condition variable。
6. worker 取出任务，在锁外执行阻塞业务逻辑。
7. 任务完成后通过 `Session::sendPacket()` 把响应安全投递回 owner loop。
8. 停止时，[stop()](../src/concurrency/ThreadPool.cpp#L96) 停止接收新任务，唤醒 worker，等待队列排空并 join。

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

`workerLoop()` 伪流程：

```text
mark current_thread_pool
while true:
    lock mutex
    wait until !running_ or !tasks_.empty()
    if tasks_.empty(): return
    task = pop front
    unlock
    try task()
    catch all: continue
```

### 5. 小例子和边界

小例子：登录请求到达 I/O loop 后，后续业务层提交一个任务做密码哈希和 MySQL 查询。任务完成后不能直接改 `Session` 内部状态，而是调用 `session->sendPacket(response)`，由 Session 自己投递回 owner loop。

边界：`ThreadPool` 不知道 `Session` 生命周期，业务任务捕获连接时应使用 `weak_ptr` 或短期 `shared_ptr`；不要在 I/O 线程等待业务任务完成；stop 后不接收新任务，但已入队任务会执行完；worker 内部调用 stop 不 join 自己。

## 4. ThreadPool.cpp 实现思路

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

## 5. 本 Step 不做什么

本 Step 不实现：

- `future` 返回值。
- 任务优先级。
- 动态扩缩容。
- work stealing。
- 定时任务。
- MySQL / Redis 接入。
- MessageRouter。
- 登录、注册、聊天业务。
- 直接操作 `Session`。

这些都属于后续 Step。

## 6. 测试设计

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

## 7. 面试时怎么讲

可以这样讲：

> LiteIM 的网络层采用 one-loop-per-thread Reactor。I/O 线程只负责 epoll、socket 读写和协议编解码，不能执行 MySQL 或 Redis 这种阻塞操作。Step 17 单独实现了一个固定大小业务线程池，后续登录、发消息、历史查询等业务会以任务形式提交到 `ThreadPool`。业务线程处理完后不会直接操作 `Session`，而是把响应投递回连接所属的 `EventLoop`，保证连接生命周期和 socket 写入仍然由 I/O 线程管理。

设计取舍：

- 固定 worker 数，避免第一版引入动态扩缩容复杂度。
- `submit()` 不返回 `future`，避免 I/O 线程等待业务结果。
- `stop()` 采用优雅退出，已经入队的任务会执行完。
- 队列用 `mutex + condition_variable`，简单直接，足够支撑后续业务服务接入。
- 任务异常被 worker 捕获，避免单个业务任务杀死工作线程。

## 8. 面试常见追问

**为什么不用 `std::async`？**

`std::async` 的调度策略和线程复用不适合做长期服务端业务池。IM 服务端需要固定数量 worker、明确停止语义和队列长度诊断。

**为什么 `submit()` 不返回 `future`？**

因为 I/O 线程不应该等待业务结果。业务完成后应该异步把响应投递回连接所属 loop，而不是让调用方阻塞等待。

**`pendingTaskCount()` 为什么不统计正在执行的任务？**

它表达的是队列积压量，用来观察业务线程池是否被提交压力打满。正在执行的任务已经离开队列，不属于 pending。

**业务线程能直接调用 `Session::sendPacket()` 吗？**

可以调用 `Session::sendPacket()` 这个跨线程安全入口，因为它内部会把真实写操作投递回所属 I/O loop。但业务线程不能直接改 `Session` 内部状态或操作 fd。

**stop 时为什么不丢弃队列？**

第一版选择优雅退出，避免已经接收的业务任务被静默丢掉。后续如果需要快速关停，可以单独设计 cancel / deadline 语义。
