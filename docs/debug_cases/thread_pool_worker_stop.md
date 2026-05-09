# ThreadPool stop 并发复盘

本文记录 `liteim::ThreadPool` 停止语义的并发 bug 修复流程。它不是新的功能 Step，而是一个横向 debug case，用于复盘项目质量、测试方法和面试中的问题排查表达。

## 基本信息

- 模块：`liteim_concurrency`
- 相关文件：
  - `include/liteim/concurrency/ThreadPool.hpp`
  - `src/concurrency/ThreadPool.cpp`
  - `tests/concurrency/thread_pool_test.cpp`
- 涉及场景：
  - worker 线程正在执行的任务内部调用 `ThreadPool::stop()`
  - 多个外部线程并发调用 `ThreadPool::stop()`
- bug 类型：线程生命周期管理 / self-join 边界 / 并发 join UB / 停止状态语义不清
- 最终方案：单一 `running_` 状态 + worker-local self-stop 识别 + `stop_mutex_` 串行化外部 join/cleanup

## 背景

Step 17 引入固定大小业务线程池，后续 MySQL、Redis、密码哈希、历史消息查询等阻塞或较重的业务逻辑都会投递到这里执行。

线程池的基本模型是：

```text
submit(task)
  -> lock mutex
  -> push_back 到 deque<Task>
  -> notify_one()

workerLoop()
  -> wait(!running_ || !tasks_.empty())
  -> pop_front()
  -> task()
```

`ThreadPool::stop()` 的普通语义是：

1. 停止接收新任务。
2. 唤醒所有 worker。
3. 已经入队的任务继续执行完。
4. owner 线程等待 worker 退出。
5. 清空 worker 列表，让下一次 `start()` 可以重新创建 worker。

## 第一个问题：worker 内部 stop 后旧 worker 脱管

原实现中 `stop()` 为了避免 self-join，遇到当前 worker 时使用了 `detach()`：

```cpp
for (auto& worker : workers_) {
    if (!worker.joinable()) {
        continue;
    }
    if (worker.get_id() == std::this_thread::get_id()) {
        worker.detach();
    } else {
        worker.join();
    }
}

{
    std::lock_guard<std::mutex> lock(mutex_);
    workers_.clear();
    stopping_ = false;
    started_.store(false);
}
```

这段代码表面上避免了当前线程 join 自己，但隐藏了更大的生命周期问题。

关键时序如下：

```text
worker 正在执行某个 task
    -> task 内部调用 pool.stop()
    -> stop() 设置 stopping_ = true
    -> stop() notify_all()
    -> 遇到当前 worker，detach 当前线程
    -> stop() 清空 workers_
    -> stop() 把 stopping_ 改回 false
    -> stop() 返回到当前 task
    -> 当前 task 执行结束
    -> workerLoop() 进入下一轮
    -> 看到 stopping_ 已经是 false
    -> 队列为空时重新 wait()
```

此时这个 worker 已经被 `detach()`，也已经不在 `workers_` 管理列表里。线程池对象认为自己已经停完了，但实际上还有一个旧 worker 可能继续等待。

这会造成两个风险：

- 线程生命周期脱离 `ThreadPool` 管理。
- `start()` 可能重新创建新的 workers，旧 worker 和新 worker 同时使用同一个 `ThreadPool` 的 mutex、condition variable 和任务队列。

## 第二个问题：外部并发 stop 会并发 join 同一个 std::thread

修复 self-stop 后，如果两个外部线程同时调用 `stop()`，还存在另一个真实隐患：

```text
线程 A 进入 stop()
  -> 设置停止状态
  -> 释放 mutex
  -> 进入 join 循环

线程 B 进入 stop()
  -> 看到 workers_ 还没清空
  -> 也进入 join 循环

线程 A 和线程 B 同时对同一个 std::thread 对象调用 join()
```

`std::thread::join()` 不允许多个线程并发调用同一个 `std::thread` 对象。这个行为是未定义行为，可能表现为崩溃、抛异常、`std::terminate()` 或卡住。

这说明只修 self-stop 还不够，owner 侧的 join/cleanup 也必须序列化。

## 第三个问题：started_ + stopping_ 状态语义不统一

旧实现同时使用：

```cpp
bool stopping_{false};
std::atomic_bool started_{false};
```

它们容易出现语义重叠：

- `started_` 表示是否启动/是否接受任务。
- `stopping_` 表示是否正在停止。

但 stop 过程中真正需要问的问题只有一个：

```text
线程池现在是否还运行、是否还接受新任务、worker 是否还应该继续等待新任务？
```

这个问题用单一 `running_` 更直接：

```cpp
std::atomic_bool running_{false};
```

语义变成：

```text
running_ == true:
    线程池运行中
    submit() 可以接收任务
    worker 队列为空时继续 wait

running_ == false:
    线程池未运行、正在停止或已经停止
    submit() 拒绝新任务
    worker 被唤醒
    worker drain 完已有任务后退出
```

`running_` 不能单独防住并发 join，但它能消除 `started_ + stopping_` 的状态一致性问题。线程资源是否清理干净则由 `workers_.empty()` 判断。

## RED 测试 1：worker 内部 stop 后不能提前 restart

第一个回归测试：

```cpp
TEST(ThreadPoolTest, StopCalledFromWorkerRequiresOwnerCleanupBeforeRestart) {
    liteim::ThreadPool pool(1);
    const auto start_status = pool.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();

    std::mutex mutex;
    std::condition_variable condition;
    bool worker_stop_returned = false;

    const auto submit_status = pool.submit([&]() {
        pool.stop();
        {
            std::lock_guard<std::mutex> lock(mutex);
            worker_stop_returned = true;
        }
        condition.notify_all();
    });
    ASSERT_TRUE(submit_status.isOk()) << submit_status.message();

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, 1s, [&]() { return worker_stop_returned; }));
    }

    const auto restart_status_before_cleanup = pool.start();
    EXPECT_FALSE(restart_status_before_cleanup.isOk());

    pool.stop();

    const auto restart_status_after_cleanup = pool.start();
    ASSERT_TRUE(restart_status_after_cleanup.isOk()) << restart_status_after_cleanup.message();
    pool.stop();
}
```

它验证两点：

- worker 内部调用 `stop()` 后，owner 线程清理前，线程池不能重启。
- owner 线程后续调用 `stop()` 完成 join 和清理后，线程池可以重新 `start()`。

旧实现失败点：

```text
Value of: restart_status_before_cleanup.isOk()
  Actual: true
Expected: false
```

## RED 测试 2：并发外部 stop 必须串行化

第二个回归测试：

```cpp
TEST(ThreadPoolTest, ConcurrentStopCallsAreSerialized) {
    liteim::ThreadPool pool(1);
    const auto start_status = pool.start();
    ASSERT_TRUE(start_status.isOk()) << start_status.message();

    std::mutex mutex;
    std::condition_variable condition;
    bool task_started = false;
    bool release_task = false;
    int stop_callers_ready = 0;
    std::atomic<int> stop_callers_returned{0};

    const auto submit_status = pool.submit([&]() {
        std::unique_lock<std::mutex> lock(mutex);
        task_started = true;
        condition.notify_all();
        condition.wait(lock, [&]() { return release_task; });
    });
    ASSERT_TRUE(submit_status.isOk()) << submit_status.message();

    auto stop_pool = [&]() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++stop_callers_ready;
        }
        condition.notify_all();
        pool.stop();
        stop_callers_returned.fetch_add(1);
        condition.notify_all();
    };

    std::thread first_stop(stop_pool);
    std::thread second_stop(stop_pool);

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, 1s, [&]() { return stop_callers_ready == 2; }));
    }

    std::this_thread::sleep_for(20ms);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_task = true;
    }
    condition.notify_all();

    first_stop.join();
    second_stop.join();

    EXPECT_EQ(stop_callers_returned.load(), 2);
    EXPECT_FALSE(pool.started());

    const auto restart_status = pool.start();
    ASSERT_TRUE(restart_status.isOk()) << restart_status.message();
    pool.stop();
}
```

旧实现运行这个测试时超时，说明两个外部 stop 进入了不安全的 join 路径：

```bash
timeout 5s ./build/tests/liteim_tests --gtest_filter=ThreadPoolTest.ConcurrentStopCallsAreSerialized
```

结果：

```text
exit code 124
```

这个失败不是普通断言失败，而是并发生命周期错误暴露出的卡住行为。

## 最终修复设计

最终采用三点组合：

1. 用单一 `running_` 代替 `started_ + stopping_`。
2. 用 worker-local 标记识别当前线程是否属于这个 `ThreadPool`，避免 self-stop 路径读取 `std::thread` 对象。
3. 用 `stop_mutex_` 串行化外部 owner 的 join/cleanup 阶段，避免多个线程同时 join 同一个 worker。

### 状态设计

```cpp
std::atomic_bool running_{false};
```

状态变化：

```text
初始:
    running_ = false
    workers_.empty() = true

start 成功:
    running_ = true
    workers_.empty() = false

stop 请求发出:
    running_ = false
    workers_.empty() 可能仍为 false

owner 清理完成:
    running_ = false
    workers_.empty() = true

下一次 start:
    running_ = true
```

`running_` 表达是否运行和是否接收任务；`workers_.empty()` 表达上一轮线程资源是否已经清理干净。

### self-stop 识别

为了避免 stop 路径在外部 join 时并发读取 `std::thread` 对象，实现中使用 thread-local 标记：

```cpp
namespace {

thread_local ThreadPool* current_thread_pool = nullptr;

class CurrentThreadPoolGuard {
public:
    explicit CurrentThreadPoolGuard(ThreadPool* pool) noexcept : previous_(current_thread_pool) {
        current_thread_pool = pool;
    }

    ~CurrentThreadPoolGuard() noexcept {
        current_thread_pool = previous_;
    }

private:
    ThreadPool* previous_{nullptr};
};

} // namespace
```

`workerLoop()` 开始时设置当前 worker 所属线程池：

```cpp
void ThreadPool::workerLoop() noexcept {
    CurrentThreadPoolGuard guard(this);
    ...
}
```

这样 `stop()` 可以通过：

```cpp
const bool called_from_worker = current_thread_pool == this;
```

判断 self-stop，而不需要遍历 `workers_`。

### stop 主流程

```cpp
void ThreadPool::stop() noexcept {
    const bool called_from_worker = current_thread_pool == this;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load() && workers_.empty()) {
            return;
        }
        running_.store(false);
    }

    condition_.notify_all();

    if (called_from_worker) {
        return;
    }

    std::lock_guard<std::mutex> stop_lock(stop_mutex_);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (workers_.empty()) {
            return;
        }
    }

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        workers_.clear();
    }
}
```

这个流程的关键边界：

- worker self-stop 只设置 `running_ = false` 并唤醒 worker，不 join、不 detach、不清理。
- 外部 stop 才进入 `stop_mutex_` 保护的 join/cleanup。
- `stop_mutex_` 不包住 worker self-stop，避免外部线程正在 join worker 时，worker 又卡在 `stop_mutex_` 上造成死锁。
- 进入 cleanup 后再次检查 `workers_.empty()`，避免第二个外部 stop 在第一个外部 stop 清理完成后重复 join。
- `start()` 如果在线程创建过程中失败，也使用同一把 `stop_mutex_` 回收已经创建的 worker，避免异常清理路径和并发 `stop()` 抢同一个 `std::thread`。

### workerLoop

```cpp
void ThreadPool::workerLoop() noexcept {
    CurrentThreadPoolGuard guard(this);
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() { return !running_.load() || !tasks_.empty(); });
            if (tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }

        try {
            task();
        } catch (...) {
        }
    }
}
```

stop 后，`running_ = false` 会唤醒 worker；如果队列里还有任务，worker 继续取出执行；队列 drain 完后退出。

## 为什么不直接禁止并发 stop

可以文档约定 `stop()` 不能并发调用，但 LiteIM 的 `ThreadPool` 是业务池基础组件，后续可能被服务端 shutdown、业务服务析构、测试代码或异常路径间接触发。让组件内部防住并发 join 更稳，也更适合作为项目质量案例。

## 为什么不用 stopped_

`stopped_` 听起来像“线程已经完全停止”，但在 stop 一开始就需要通知 worker 退出。此时 worker 可能还在 drain 队列，不能说已经 stopped。

`running_` 更准确：

```text
running_ == true  -> 可以接收任务，worker 等待新任务
running_ == false -> 不再接收任务，worker drain 后退出
```

它和 muduo 风格一致，少一个状态变量，也减少 `started_` 和 `stopping_` 不一致的可能。

## 为什么不能 detach

`detach()` 只解除 `std::thread` 对底层线程的管理，不会让线程停止。

worker 持续访问的是 `ThreadPool` 对象内部状态：

- `mutex_`
- `condition_`
- `tasks_`
- `running_`

如果 worker 被 detach 后继续运行，owner 线程可能认为线程池已经停止，甚至可能析构对象。这样会把问题升级成 use-after-lifetime 风险。

## 验证流程

RED 观察：

```bash
timeout 5s ./build/tests/liteim_tests --gtest_filter=ThreadPoolTest.ConcurrentStopCallsAreSerialized
```

旧实现超时退出：

```text
exit code 124
```

修复后定向验证：

```bash
./build/tests/liteim_tests --gtest_filter=ThreadPoolTest.ConcurrentStopCallsAreSerialized
ctest --test-dir build --output-on-failure -R "ThreadPoolTest|ConcurrencyInterfaceTest.ThreadPool"
```

结果：

```text
ThreadPoolTest.ConcurrentStopCallsAreSerialized passed
100% tests passed, 0 tests failed out of 13
```

这里的正则也匹配了历史 `EventLoopThreadPool` 测试；本次 Step 17 相关测试是 9 个。

全量验证：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

预期结果：

```text
LiteIM server scaffold is running on 0.0.0.0:9000
100% tests passed, 0 tests failed out of 151
git diff --check 无输出
.gitkeep 检查无输出
旧路线残留检查无输出
```

## 面试时可以怎么讲

简短版本：

> 我在业务线程池里修过一个 stop 并发问题。最初代码用 `started_ + stopping_` 两个状态，并且 worker 内部调用 `stop()` 时为了避免 join 自己用了 `detach()`。这会导致 worker 脱离线程池管理，甚至可能在 `stopping_` 被复位后重新 wait。我先写了 self-stop 回归测试，证明 owner 清理前不能 restart。随后又发现多个外部线程并发 `stop()` 会同时 join 同一个 `std::thread`，这是 UB，于是补了并发 stop 测试。最终我把状态收敛成单一 `running_`，worker self-stop 只设置 `running_ = false` 并唤醒线程，不 join、不 detach；外部 owner stop 用 `stop_mutex_` 串行化 join/cleanup。最后用 ThreadPool 定向测试和全量 CTest 验证。

展开版本：

> 这个问题的关键是线程所有权和关闭状态机。`detach()` 不能作为线程池关闭策略，因为 worker 仍然访问线程池内部的 mutex、condition variable 和任务队列。并且 `std::thread::join()` 不能被多个线程同时调用，所以外部并发 stop 也必须防住。我把“是否运行”收敛为 `running_`，它既控制 submit 是否接收任务，也控制 worker 是否继续等待新任务；而线程资源是否清理干净用 `workers_.empty()` 判断。为了避免 self-stop 路径和外部 join 并发访问同一个 `std::thread` 对象，我用 thread-local 标记当前 worker 所属的 ThreadPool。这样 self-stop 不碰 `workers_`，只发出停止请求；真正的 join 和 cleanup 由外部 owner 串行完成。

## 面试追问

**为什么不用 `started_ + stopping_`？**

这两个变量表达的是同一个生命周期的两面，容易出现一条路径复位、另一条路径不复位的语义分裂。`running_` 更直接：true 表示运行并接收任务，false 表示不再接收任务，worker drain 完后退出。

**为什么不用 `stopped_`？**

stop 一开始就要唤醒 worker，但这时 worker 可能还在执行已入队任务，线程池不能算已经 stopped。`running_ == false` 更准确表达“不再运行/不再接收新任务”。

**为什么 `start()` 还要检查 `workers_.empty()`？**

`running_ == false` 只能说明线程池不接收任务，不能说明上一轮 worker 线程资源已经 join/cleanup。worker self-stop 后 `running_` 已经是 false，但 `workers_` 仍保留 join 句柄，所以 owner 清理前不能重启。

**为什么需要 `stop_mutex_`？**

两个外部线程同时调用 `stop()` 时，如果都进入 join 循环，就可能同时 join 同一个 `std::thread`，这是未定义行为。`stop_mutex_` 只保护外部 owner 的 join/cleanup 阶段。

**为什么 self-stop 路径不能拿 `stop_mutex_`？**

如果外部线程已经拿着 `stop_mutex_` 并在 join 当前 worker，而当前 worker 的任务里又调用 `stop()` 并等待 `stop_mutex_`，就会形成外部线程等 worker 结束、worker 等 `stop_mutex_` 的死锁。所以 self-stop 只发停止请求并返回。

**为什么用 thread-local 判断 self-stop？**

如果 self-stop 通过遍历 `workers_` 判断当前线程是否是 worker，可能和外部线程正在 join 同一个 `std::thread` 对象并发访问。thread-local 标记让 self-stop 不碰 `workers_`，避免这个数据竞争。

**这个修复有没有改变普通 stop 语义？**

没有。外部 owner 调用 `stop()` 时仍然会停止接收新任务、唤醒 worker、等待已入队任务执行完、join worker 并清理状态。变化只是在 self-stop 和并发外部 stop 边界上更安全。

## 复盘结论

关键经验：

- `detach()` 不是线程池关闭策略。
- self-stop 场景下，停止请求和资源回收要分离。
- `running_` 比 `started_ + stopping_` 更适合当前线程池状态机。
- `running_` 只表示是否运行，不表示 worker 资源是否清理完成；重启还要看 `workers_.empty()`。
- 多线程并发 `stop()` 必须避免同时 join 同一个 `std::thread`。
- 并发 bug 的测试要覆盖具体时序，而不是只测 API 表面返回值。
