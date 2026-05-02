# Step 8：实现 EventLoop 基础骨架

本步骤目标：实现 Reactor 的事件循环层 `EventLoop`，把 Step 7 的 `Epoller` 和 `Channel` 串起来。

Step 7 已经能把 fd 注册进 epoll，也能从 `epoll_wait()` 拿到活跃 `Channel`。但那还只是系统调用封装。Step 8 要让程序有一个真正的“循环”：不断等待事件、遍历活跃事件、把事件交给 `Channel` 处理。

## 1. 这一步要解决什么问题

服务端运行起来后，不是处理一次请求就退出，而是长期循环：

```text
等待 fd 事件
  ↓
拿到活跃 Channel
  ↓
调用 Channel::handleEvent()
  ↓
继续等待下一批事件
```

这个循环就是 `EventLoop`。

如果没有 `EventLoop`，上层模块就得自己反复调用 `epoll_wait()`，自己遍历事件，自己决定什么时候退出。这样后续 `Acceptor`、`Session`、`timerfd`、`signalfd` 都会变得混乱。

所以 Step 8 先实现事件循环骨架：

- `EventLoop` 持有一个 `Epoller`。
- `loop()` 持续调用 `Epoller::poll()`。
- 对每个活跃 `Channel` 调用 `Channel::handleEvent()`。
- `quit()` 请求事件循环退出。
- `updateChannel()` / `removeChannel()` 把注册和移除操作转发给 `Epoller`。

## 2. 职责边界

`EventLoop` 负责：

- 拥有 `Epoller`。
- 驱动 `epoll_wait()` 循环。
- 调度活跃 `Channel`。
- 提供退出事件循环的状态。
- 提供 `updateChannel()` 和 `removeChannel()` 入口。

`EventLoop` 不负责：

- 不拥有 socket fd。
- 不拥有 `Channel`。
- 不直接调用 `read()` / `write()`。
- 不解析协议包。
- 不处理登录、私聊、群聊业务。

当前 Step 8 还没有做的事：

- `Channel::enableReading()` 不会自动调用 `EventLoop::updateChannel()`。
- `Channel::disableWriting()` 不会自动更新 epoll。
- 这些“Channel 自己改事件后通知 EventLoop”的联通逻辑留到 Step 9。

因此本步骤测试中会显式调用：

```cpp
channel.enableReading();
loop.updateChannel(&channel);
```

这能让 Step 8 的边界保持清楚：先把事件循环跑起来，再让 `Channel` 主动联通 `EventLoop`。

## 3. 本步骤新增文件

新增文件：

```text
src/net/EventLoop.cpp
tests/test_event_loop.cpp
tutorials/step08_event_loop.md
```

修改文件：

```text
include/liteim/net/EventLoop.hpp
src/net/Channel.cpp
src/CMakeLists.txt
tests/CMakeLists.txt
tests/test_main.cpp
docs/architecture.md
docs/interview_notes.md
docs/project_layout.md
tutorials/README.md
README.md
task_plan.md
findings.md
progress.md
```

## 4. EventLoop.hpp 讲解

文件：

```text
include/liteim/net/EventLoop.hpp
```

### EventLoop()

```cpp
EventLoop();
```

作用：

- 构造事件循环对象。
- 内部创建一个 `Epoller`。

输入：

- 没有输入参数。

输出：

- 构造成功后，`EventLoop` 可以注册 `Channel` 并进入 `loop()`。

失败场景：

- 如果 `Epoller` 构造失败，底层 `epoll_create1()` 会抛出 `std::system_error`。

### ~EventLoop()

```cpp
~EventLoop();
```

作用：

- 销毁事件循环对象。
- 自动销毁内部 `Epoller`，释放 epoll fd。

边界：

- 不关闭普通 socket fd。
- 不销毁 `Channel`。
- `Channel` 和普通 fd 后续由 `Acceptor` / `Session` 管理。

### loop()

```cpp
void loop();
```

作用：

- 进入事件循环。
- 不断调用 `Epoller::poll()`。
- 遍历活跃事件并调用 `Channel::handleEvent()`。

输入：

- 没有输入参数。

输出：

- 没有返回值。
- 调用 `quit()` 后，循环结束，函数返回。

副作用：

- 会阻塞等待 fd 事件。
- 会执行 `Channel` 中保存的回调。

边界：

- 当前没有 `eventfd` wakeup 机制。
- 如果其他线程调用 `quit()`，但没有新 fd 事件到来，循环最迟要等到本轮 `poll()` 超时后才退出。
- 当前 `quit_` 使用 `std::atomic_bool`，避免测试和未来跨线程退出路径出现数据竞争。

### quit()

```cpp
void quit();
```

作用：

- 请求事件循环退出。

副作用：

- 把内部 `quit_` 标记设为 true。

使用场景：

- 测试中，读回调收到事件后调用 `loop.quit()`。
- 后续 Step 12 中，`signalfd` 收到 Ctrl+C / SIGTERM 后可以调用 `quit()`。

边界：

- `quit()` 只是修改状态，不负责唤醒正在阻塞的 `epoll_wait()`。
- 后续如果需要跨线程立即唤醒，可以补 `eventfd`。

### updateChannel()

```cpp
void updateChannel(Channel* channel);
```

作用：

- 把 `Channel` 的注册或修改请求转发给 `Epoller`。

输入：

- `channel`：要添加或修改关注事件的 `Channel`。

副作用：

- 间接调用 `epoll_ctl(ADD)` 或 `epoll_ctl(MOD)`。

失败场景：

- 如果 `channel == nullptr` 或 fd 无效，底层 `Epoller` 会抛出 `std::invalid_argument`。
- 如果 `epoll_ctl()` 失败，底层会抛出 `std::system_error`。

### removeChannel()

```cpp
void removeChannel(Channel* channel);
```

作用：

- 把 `Channel` 的移除请求转发给 `Epoller`。

副作用：

- 间接调用 `epoll_ctl(DEL)`。
- 从 `Epoller` 的注册集合中删除该 fd。

边界：

- 不关闭 fd。
- 不销毁 `Channel`。
- 移除后，fd 生命周期仍由上层对象负责。

## 5. EventLoop.cpp 讲解

文件：

```text
src/net/EventLoop.cpp
```

### kPollTimeoutMs

```cpp
constexpr int kPollTimeoutMs = 10000;
```

作用：

- 作为 `Epoller::poll()` 的超时时间。
- 单位是毫秒。

为什么不是 `-1` 永久阻塞：

- Step 8 还没有 wakeup fd。
- 使用有限超时可以避免某些退出路径永久卡住。
- 后续加入 `eventfd` 或 `signalfd` 后，可以再优化阻塞策略。

### 构造函数实现

```cpp
EventLoop::EventLoop() : epoller_(std::make_unique<Epoller>()) {}
```

含义：

- 每个 `EventLoop` 拥有一个自己的 `Epoller`。
- `Epoller` 内部拥有一个 epoll fd。

后续单线程 Reactor 中，一个线程通常对应一个 `EventLoop`。

### loop() 实现

核心逻辑：

```cpp
while (!quit_.load()) {
    const auto active_events = epoller_->poll(kPollTimeoutMs);
    for (const auto& active_event : active_events) {
        if (active_event.channel == nullptr) {
            continue;
        }
        active_event.channel->handleEvent();
    }
}
```

执行过程：

1. 检查 `quit_` 是否已经请求退出。
2. 调用 `Epoller::poll()` 等待事件。
3. 遍历本轮活跃事件。
4. 跳过空 `Channel*`。
5. 调用 `Channel::handleEvent()`。

为什么 `loop()` 不直接调用业务函数：

- `EventLoop` 只知道“哪个 Channel 活跃了”。
- 读、写、关闭、错误分别怎么处理，是 `Channel` 回调和后续 `Session` 的职责。
- 这样 `EventLoop` 不需要知道登录、私聊、心跳等业务语义。

### quit() 实现

```cpp
quit_.store(true);
```

含义：

- 把退出标记设为 true。
- 下一次 `while` 条件检查时，`loop()` 会结束。

为什么 `quit_` 用 atomic：

- 测试或未来关闭逻辑可能从回调或其他线程调用 `quit()`。
- 原始 `bool` 在跨线程读写时会有数据竞争。
- `std::atomic_bool` 能保证这个停止标记的读写是安全的。

### updateChannel() / removeChannel() 实现

这两个函数都是转发：

```cpp
epoller_->updateChannel(channel);
epoller_->removeChannel(channel);
```

为什么需要这一层：

- 上层模块不应该直接拿到 `Epoller`。
- `EventLoop` 是 Reactor 的统一入口。
- 后续 `Channel::update()` 会通过 `loop_->updateChannel(this)` 更新 epoll。

## 6. Channel.cpp 本步骤补充的内容

文件：

```text
src/net/Channel.cpp
```

Step 8 给 `Channel::handleEvent()` 补了基础分发逻辑。

### handleEvent()

```cpp
void Channel::handleEvent();
```

作用：

- 根据 `revents_` 判断本轮发生了什么事件。
- 调用对应回调。

输入：

- 没有函数参数。
- 它读取对象内部的 `revents_`。

输出：

- 没有返回值。

副作用：

- 可能执行 read/write/close/error 回调。
- 回调里可能读取 fd、写 fd、关闭连接，或者调用 `EventLoop::quit()`。

当前分发规则：

- `EPOLLHUP` 且没有 `EPOLLIN` 时，调用 close callback。
- `EPOLLERR` 时，调用 error callback。
- `EPOLLIN` / `EPOLLPRI` / `EPOLLRDHUP` 时，调用 read callback。
- `EPOLLOUT` 时，调用 write callback。

边界：

- 如果某类 callback 没设置，就跳过。
- 当前不负责关闭 fd。
- 当前不负责从 epoll 移除自己。

为什么 `EPOLLHUP` 要看是否同时有 `EPOLLIN`：

- 有些关闭场景下，fd 仍可能有剩余数据可读。
- 如果还有 `EPOLLIN`，通常应该先让 read callback 处理剩余数据。

## 7. 本步骤测试

新增测试文件：

```text
tests/test_event_loop.cpp
```

接入位置：

```text
tests/CMakeLists.txt
tests/test_main.cpp
```

测试继续使用 `pipe()` 创建真实 fd。这样能验证 `EventLoop` 是否真的通过 `Epoller` 收到内核事件，而不是只测普通函数调用。

### event loop dispatches read callback and quits

验证：

- pipe 写端写入一个字节。
- 读端 `Channel` 注册读事件。
- `loop.loop()` 能收到 `EPOLLIN`。
- `Channel::handleEvent()` 能调用 read callback。
- read callback 读出字节后调用 `loop.quit()`。

为什么要测：

- 这是 Step 8 最核心路径：`EventLoop -> Epoller -> Channel -> callback -> quit`。

### event loop dispatches write callback and quits

验证：

- pipe 写端注册写事件。
- `loop.loop()` 能收到 `EPOLLOUT`。
- write callback 被调用，并让事件循环退出。

为什么要测：

- 后续 `Session` 的输出缓冲区依赖写事件。
- 如果输出数据没写完，就要关注 `EPOLLOUT`，可写后继续发送。

### event loop remove channel stops dispatch

验证：

- 读 `Channel` 注册后再移除。
- 即使 pipe 中有可读数据，被移除的读 `Channel` 也不会被分发。
- 另一个写 `Channel` 用来触发并退出事件循环。

为什么要测：

- 连接关闭时必须从 epoll 移除 fd。
- 移除后的 fd 不能继续触发旧回调，否则容易访问已销毁对象。

### event loop quit before loop returns immediately

验证：

- 先调用 `quit()`。
- 再调用 `loop()` 会立即返回。

为什么要测：

- 退出标记应由 `loop()` 尊重。
- 这也是后续优雅关闭逻辑的基础。

### event loop update rejects invalid channel

验证：

- `loop.updateChannel(nullptr)` 会抛出 `std::invalid_argument`。

为什么要测：

- `EventLoop` 应该保留 `Epoller` 的错误边界。
- 无效 `Channel` 不能被静默注册。

运行测试：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
```

测试通过说明：

- `EventLoop` 能持有并驱动 `Epoller`。
- `loop()` 能分发读写事件到 `Channel` callback。
- `quit()` 能结束循环。
- `updateChannel()` 和 `removeChannel()` 能通过真实 fd 行为被验证。

## 8. 面试时怎么讲

可以这样说：

> 我把 Reactor 的调度层封装成 `EventLoop`。它内部持有 `Epoller`，循环调用 `epoll_wait()`，拿到活跃 `Channel` 后调用 `Channel::handleEvent()`。`EventLoop` 不处理业务，也不拥有 socket fd，它只负责事件循环的主流程。这样后续 `Acceptor`、`Session`、`timerfd`、`signalfd` 都能作为 Channel 注册进同一个循环里统一调度。

为什么 `EventLoop` 不直接读 socket：

- 读 socket 属于连接对象 `Session` 的职责。
- `EventLoop` 如果直接读，就会知道协议、缓冲区和连接状态。
- 这样会破坏网络层分工。

为什么 `quit()` 只是设置标记：

- 事件循环退出应该是可控状态，而不是直接中断当前回调。
- 当前回调执行完后，下一轮循环检查 `quit_`，再决定退出。
- 这比在任意位置强行跳出更容易保证资源清理顺序。

为什么还没有 wakeup fd：

- Step 8 只实现基础骨架。
- 当前测试通过真实 fd 事件唤醒循环。
- 后续如果要跨线程立即唤醒，可以加入 `eventfd`。
- Step 12 的 `signalfd` 也会把系统信号转成 fd 事件纳入 epoll。

为什么 `EventLoop` 不拥有 `Channel`：

- `Channel` 通常是 `Acceptor` 或 `Session` 的成员。
- `EventLoop` 只调度它，不决定它什么时候销毁。
- 销毁前必须先 `removeChannel()`，避免 epoll 返回悬空指针。

## 9. 面试常见追问

**Q：`EventLoop`、`Epoller`、`Channel` 的关系是什么？**

A：`EventLoop` 是循环调度层，`Epoller` 是 Linux epoll 系统调用封装层，`Channel` 是 fd 和回调的绑定层。`EventLoop` 调用 `Epoller::poll()`，然后让活跃 `Channel` 自己处理事件。

**Q：为什么 `EventLoop` 不直接调用 `read()`？**

A：因为 `EventLoop` 不应该知道连接协议和业务状态。具体 read/write 逻辑后续放在 `Session`，`EventLoop` 只负责事件分发。

**Q：`quit()` 会不会立即打断正在执行的 callback？**

A：不会。它只是设置退出标记。当前 callback 执行完后，`loop()` 回到 while 条件时才退出，这样更容易保证状态一致。

**Q：为什么 `quit_` 用 `std::atomic_bool`？**

A：退出标记可能从回调或未来其他线程设置。用 atomic 可以避免跨线程读写普通 bool 的数据竞争。

**Q：没有 wakeup fd 时，跨线程调用 `quit()` 有什么限制？**

A：它不会立即唤醒正在阻塞的 `epoll_wait()`。循环会在下一次事件到来或 poll 超时后退出。后续可以用 `eventfd` 做立即唤醒。

**Q：Step 8 和 Step 9 的边界是什么？**

A：Step 8 实现 `EventLoop` 的循环和基础事件分发，测试里手动调用 `loop.updateChannel(&channel)`。Step 9 再让 `Channel::enableReading()` 等接口自动通过 `EventLoop` 更新 epoll，完成 Channel 和 EventLoop 的联通。
