# Step 9：实现 Channel 并联通 EventLoop

本步骤目标：补齐 Reactor 中 `Channel` 这一层，让 fd 的事件关注变化可以自动同步到 `EventLoop` / `Epoller`。

Step 7 已经实现了 `Epoller`，Step 8 已经实现了 `EventLoop` 和 `Channel::handleEvent()` 的基础分发。但 Step 8 的测试还需要这样写：

```cpp
channel.enableReading();
loop.updateChannel(&channel);
```

这说明 `Channel` 还没有真正“联通” `EventLoop`。Step 9 要把这件事补上：调用 `enableReading()`、`enableWriting()`、`disableWriting()`、`disableAll()` 后，`Channel` 自己通知 `EventLoop` 更新 epoll。

## 1. 这一步要解决什么问题

`Channel` 是 Reactor 里的 fd 事件代理。一个 `Channel` 绑定一个 fd，并保存：

- 当前希望监听什么事件。
- 本轮 epoll 实际返回什么事件。
- 事件发生后应该调用哪个 C++ 回调。

没有 Step 9 时，上层代码需要记住两件事：

```cpp
channel.enableReading();
loop.updateChannel(&channel);
```

这容易出错。后续 `Session` 如果忘记第二行，fd 明明设置了读事件，却没有真正注册到 epoll。

Step 9 后，上层只需要表达语义：

```cpp
channel.enableReading();
```

`Channel` 内部会调用私有 `update()`，再通过 `EventLoop` 更新 epoll。

## 2. 职责边界

`Channel` 负责：

- 保存 fd。
- 保存关注事件 `events_`。
- 保存实际返回事件 `revents_`。
- 保存 read/write/close/error 回调。
- 根据 `revents_` 分发回调。
- 当关注事件变化时通知 `EventLoop`。

`Channel` 不负责：

- 不拥有 fd。
- 不关闭 fd。
- 不创建 socket。
- 不调用 `accept()`。
- 不读写协议包。
- 不保存用户登录状态。

这些后续会由 `Acceptor`、`Session` 和业务层完成。

## 3. 本步骤修改文件

修改文件：

```text
src/net/Channel.cpp
tests/CMakeLists.txt
tests/test_main.cpp
README.md
docs/architecture.md
docs/interview_notes.md
docs/project_layout.md
tutorials/README.md
task_plan.md
findings.md
progress.md
```

新增文件：

```text
tests/test_channel.cpp
tutorials/step09_channel.md
```

## 4. Channel.hpp 讲解

文件：

```text
include/liteim/net/Channel.hpp
```

Step 9 没有新增公开接口，而是把 Step 6 已经声明好的接口真正联通。

### Channel(EventLoop* loop, int fd)

```cpp
Channel(EventLoop* loop, int fd);
```

作用：

- 创建一个 fd 对应的事件代理。
- 保存所属 `EventLoop*`。
- 保存 fd。

输入：

- `loop`：这个 `Channel` 所属的事件循环。
- `fd`：这个 `Channel` 代理的文件描述符。

输出：

- 构造成功后，`Channel` 可以设置回调和事件关注。

边界：

- `Channel` 不拥有 `loop`。
- `Channel` 不拥有 `fd`。
- 后续 `Acceptor` / `Session` 关闭 fd 前，需要先从 `EventLoop` 移除对应 `Channel`。

### fd()

```cpp
int fd() const;
```

作用：

- 返回当前 `Channel` 绑定的 fd。

输入：

- 没有输入参数。

输出：

- 返回 `int` 类型 fd。

使用场景：

- `Epoller::updateChannel()` 通过它拿到 fd，然后调用 `epoll_ctl()`。

### events()

```cpp
std::uint32_t events() const;
```

作用：

- 返回当前希望 epoll 关注的事件。

输出：

- 返回 `events_`。

例子：

- 调用 `enableReading()` 后，`events()` 包含 `EPOLLIN | EPOLLPRI`。
- 调用 `enableWriting()` 后，`events()` 包含 `EPOLLOUT`。

### revents()

```cpp
std::uint32_t revents() const;
```

作用：

- 返回本轮 epoll 实际返回的事件。

输出：

- 返回 `revents_`。

为什么需要它：

- `events_` 是“我想监听什么”。
- `revents_` 是“这一次实际发生了什么”。
- `handleEvent()` 根据 `revents_` 决定调用哪个回调。

### setRevents()

```cpp
void setRevents(std::uint32_t revents);
```

作用：

- 把 `epoll_wait()` 返回的事件写入 `Channel`。

输入：

- `revents`：内核返回的事件位。

副作用：

- 修改内部 `revents_`。

使用场景：

- `Epoller::poll()` 从 `epoll_event.events` 拿到事件后调用它。

### isNoneEvent()

```cpp
bool isNoneEvent() const;
```

作用：

- 判断当前是否没有关注任何事件。

输出：

- 如果 `events_ == kNoneEvent`，返回 true。
- 否则返回 false。

使用场景：

- Step 9 的 `update()` 用它判断应该更新 epoll，还是从 epoll 移除。

### isWriting()

```cpp
bool isWriting() const;
```

作用：

- 判断当前是否正在关注写事件。

输出：

- 如果 `events_` 包含 `EPOLLOUT`，返回 true。
- 否则返回 false。

使用场景：

- 后续 `Session` 发送数据时，如果输出缓冲区还有待写数据，就开启写关注。
- 写完后如果 `isWriting()` 为 true，可以调用 `disableWriting()`。

### enableReading()

```cpp
void enableReading();
```

作用：

- 让当前 fd 关注读事件。
- Step 9 后会自动通知 `EventLoop` 更新 epoll。

副作用：

- 修改 `events_`。
- 如果 `loop_` 不为空，会间接调用 `epoll_ctl(ADD)` 或 `epoll_ctl(MOD)`。

边界：

- 如果 fd 无效，底层 `Epoller` 会抛出异常。
- 如果 `loop_ == nullptr`，只修改本地 `events_`。这是为了底层 `Epoller` 单元测试可以直接构造 `Channel`，正式网络对象应传入真实 `EventLoop`。

### enableWriting()

```cpp
void enableWriting();
```

作用：

- 让当前 fd 关注写事件。
- Step 9 后会自动通知 `EventLoop` 更新 epoll。

使用场景：

- 后续 `Session` 的输出缓冲区还有数据没写完时，开启写事件。
- fd 可写后，`handleWrite()` 继续发送剩余数据。

副作用：

- 修改 `events_`。
- 通过 `EventLoop` 更新 epoll 关注事件。

### disableWriting()

```cpp
void disableWriting();
```

作用：

- 取消写事件关注。

使用场景：

- 后续 `Session` 把输出缓冲区写空后，关闭 `EPOLLOUT`，避免 epoll 一直报告“可写”。

副作用：

- 从 `events_` 中清除 `EPOLLOUT`。
- 如果还关注读事件，更新 epoll。
- 如果没有任何关注事件，从 epoll 移除这个 fd。

### disableAll()

```cpp
void disableAll();
```

作用：

- 清空所有关注事件。

使用场景：

- 连接关闭前，不再关注这个 fd 的任何事件。

副作用：

- 把 `events_` 设为 `kNoneEvent`。
- 通过 `EventLoop::removeChannel(this)` 从 epoll 移除。

### setReadCallback()

```cpp
void setReadCallback(EventCallback callback);
```

作用：

- 保存读事件回调。

输入：

- `callback`：一个无参数、无返回值的可调用对象。

使用场景：

- 后续 `Session` 会把自己的 `handleRead()` 绑定进来。

### setWriteCallback()

```cpp
void setWriteCallback(EventCallback callback);
```

作用：

- 保存写事件回调。

使用场景：

- 后续 `Session` 会把自己的 `handleWrite()` 绑定进来。

### setCloseCallback()

```cpp
void setCloseCallback(EventCallback callback);
```

作用：

- 保存关闭事件回调。

使用场景：

- 对端关闭连接或出现 hang up 时，后续 `Session` 可以清理连接。

### setErrorCallback()

```cpp
void setErrorCallback(EventCallback callback);
```

作用：

- 保存错误事件回调。

使用场景：

- fd 出现 `EPOLLERR` 时，后续可以读取 socket error 并关闭连接。

### handleEvent()

```cpp
void handleEvent();
```

作用：

- 根据 `revents_` 分发对应回调。

输入：

- 没有函数参数。
- 它读取内部 `revents_`。

副作用：

- 可能执行 read/write/close/error 回调。
- 回调内部后续可能读写 fd、关闭连接、调用 `quit()`。

边界：

- 没有设置某类回调时直接跳过。
- `Channel` 自己不关闭 fd。
- `Channel` 自己不解析协议。

## 5. Channel.cpp 讲解

文件：

```text
src/net/Channel.cpp
```

### enableReading() 实现

```cpp
void Channel::enableReading() {
    events_ |= kReadEvent;
    update();
}
```

含义：

1. 把读事件加入 `events_`。
2. 调用私有 `update()`。

为什么用 `|=`：

- 一个 fd 可能同时关注读和写。
- 开启读事件时不应该把已有写事件清掉。

### enableWriting() 实现

```cpp
void Channel::enableWriting() {
    events_ |= kWriteEvent;
    update();
}
```

含义：

1. 把写事件加入 `events_`。
2. 通知 `EventLoop` 更新 epoll。

为什么写事件不能一直开：

- 大多数 socket 在正常情况下经常可写。
- 如果一直关注 `EPOLLOUT`，事件循环会频繁收到写事件。
- 后续 `Session` 只在输出缓冲区有未写完数据时开启写关注。

### disableWriting() 实现

```cpp
void Channel::disableWriting() {
    events_ &= ~kWriteEvent;
    update();
}
```

含义：

1. 从 `events_` 中移除写事件。
2. 通知 `EventLoop` 更新 epoll。

如果这个 fd 还关注读事件，`update()` 会修改 epoll 关注事件；如果已经没有任何事件，`update()` 会移除它。

### disableAll() 实现

```cpp
void Channel::disableAll() {
    events_ = kNoneEvent;
    update();
}
```

含义：

1. 清空所有关注事件。
2. 通知 `EventLoop` 从 epoll 移除这个 fd。

为什么不注册一个空事件：

- fd 没有关注事件时，留在 epoll 里没有实际意义。
- 移除后，后续如果再次开启读写事件，`Epoller` 会重新走 `EPOLL_CTL_ADD`。

### handleEvent() 实现

当前分发规则：

```cpp
if ((revents_ & EPOLLHUP) != 0 && (revents_ & EPOLLIN) == 0) {
    close_callback_();
}

if ((revents_ & EPOLLERR) != 0) {
    error_callback_();
}

if ((revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) != 0) {
    read_callback_();
}

if ((revents_ & EPOLLOUT) != 0) {
    write_callback_();
}
```

为什么 `EPOLLHUP` 要判断没有 `EPOLLIN`：

- 某些关闭场景下，fd 可能还有剩余数据可读。
- 如果同时有 `EPOLLIN`，通常先让读回调处理剩余数据。

为什么 `EPOLLRDHUP` 归到读回调：

- 半关闭通常意味着读方向需要处理 EOF 或连接关闭。
- 后续 `Session::handleRead()` 可以在 read 返回 0 时关闭连接。

### update() 实现

```cpp
void Channel::update() {
    if (loop_ == nullptr) {
        return;
    }

    if (isNoneEvent()) {
        loop_->removeChannel(this);
        return;
    }

    loop_->updateChannel(this);
}
```

作用：

- 这是 `Channel` 到 `EventLoop` 的私有桥梁。

输入：

- 没有函数参数。
- 它使用当前对象的 `loop_` 和 `events_`。

副作用：

- 如果还有关注事件，注册或修改 epoll。
- 如果没有关注事件，从 epoll 移除。

为什么它是 private：

- 外部不应该直接关心“怎么更新 epoll”。
- 外部只应该表达“关注读”“关注写”“取消写”“全部取消”。

为什么 `loop_ == nullptr` 时直接返回：

- 低层 `Epoller` 单元测试需要直接构造 `Channel(nullptr, fd)`，只验证 `Epoller` 的 add/mod/del 行为。
- 真实的 `Acceptor` 和 `Session` 后续会传入有效 `EventLoop*`。

## 6. 本步骤测试

新增测试文件：

```text
tests/test_channel.cpp
```

接入位置：

```text
tests/CMakeLists.txt
tests/test_main.cpp
```

测试继续使用 `pipe()` 创建真实 fd。这样可以验证 `Channel -> EventLoop -> Epoller -> epoll_wait()` 的完整路径，而不是只测普通成员变量。

### channel enableReading registers with event loop

验证：

- 创建 pipe。
- 创建 `EventLoop`。
- 用 pipe 读端创建 `Channel`。
- 设置 read callback。
- 只调用 `channel.enableReading()`，不手动调用 `loop.updateChannel(&channel)`。
- 写端写入一个字节。
- `loop.loop()` 能触发 read callback 并退出。

为什么要测：

- 这是 Step 9 的核心验收。
- 通过它证明 `enableReading()` 已经自动联通 `EventLoop`。

### channel disableWriting removes write interest

验证：

- 用 pipe 写端创建 write channel。
- 开启写事件后立即 `disableWriting()`。
- 再用读 channel 触发事件循环退出。
- write callback 不应该被调用。

为什么要测：

- 写 fd 通常很容易处于可写状态。
- 如果写事件没有正确关闭，后续 `Session` 会被频繁唤醒。

### channel disableAll can re-enable reading

验证：

- 先开启读事件。
- 调用 `disableAll()` 清空关注事件并移除。
- 再次调用 `enableReading()`。
- 写入数据后，读回调仍然能触发。

为什么要测：

- 关闭关注事件后，对象还能重新注册。
- 这验证了 `remove` 后再 `add` 的路径。

### channel handleEvent dispatches callbacks by returned events

验证：

- 手动设置 `EPOLLHUP`，触发 close callback。
- 手动设置 `EPOLLERR`，触发 error callback。
- 手动设置 `EPOLLIN | EPOLLOUT`，触发 read/write callback。

为什么要测：

- `Channel` 的另一个职责是事件分发。
- 即使 Step 8 已经有 EventLoop 集成测试，Step 9 仍需要直接验证 `handleEvent()` 的分发规则。

运行测试：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
```

测试通过说明：

- `Channel` 能自动通过 `EventLoop` 更新 epoll。
- `disableWriting()` 和 `disableAll()` 能让 fd 停止被 epoll 分发。
- `disableAll()` 后可以重新启用事件。
- `handleEvent()` 能根据 `revents_` 调用正确回调。

## 7. 面试时怎么讲

可以这样说：

> 我把每个 fd 的事件状态和回调封装成 `Channel`。`events_` 表示当前希望 epoll 关注的事件，`revents_` 表示 epoll 本轮实际返回的事件。`enableReading()`、`enableWriting()` 这些接口只表达语义，内部会通过所属 `EventLoop` 更新 epoll。这样后续 `Session` 不需要直接调用 `epoll_ctl()`，只需要管理自己的读写状态和回调。

为什么 `Channel` 不直接拥有 fd：

- fd 生命周期属于 `Acceptor` 或 `Session`。
- `Channel` 只是事件代理。
- 如果 `Channel` 自己关闭 fd，连接对象的生命周期会变得混乱。

为什么通过 `EventLoop` 更新：

- `EventLoop` 是 Reactor 的调度入口。
- `Epoller` 是底层系统调用封装，不应该暴露给业务对象。
- `Channel` 通过 `EventLoop` 更新，依赖方向更清楚。

为什么要区分 `events` 和 `revents`：

- `events` 是用户态想监听的事件。
- `revents` 是内核实际返回的事件。
- 一个表示“愿望”，一个表示“结果”，分开后逻辑更清楚。

为什么 `disableWriting()` 很重要：

- socket 通常经常可写。
- 如果输出缓冲区已经写空，还继续关注 `EPOLLOUT`，事件循环会被无意义唤醒。
- 所以后续 `Session` 写完数据后要关闭写关注。

## 8. 面试常见追问

**Q：`enableReading()` 里面为什么不直接调用 `epoll_ctl()`？**

A：因为 `Channel` 不拥有 epoll fd，也不应该直接依赖系统调用细节。它通过 `EventLoop` 更新，让事件调度入口统一，后续也更容易管理线程和生命周期。

**Q：`Channel` 为什么不关闭 fd？**

A：`Channel` 只是事件代理，fd 的所有权属于 `Acceptor` 或 `Session`。谁拥有资源，谁负责关闭；这样生命周期边界更清楚。

**Q：`events_` 和 `revents_` 的区别是什么？**

A：`events_` 是当前希望监听的事件，比如读或写；`revents_` 是本轮 `epoll_wait()` 实际返回的事件。`Epoller` 根据 `events_` 注册，`Channel::handleEvent()` 根据 `revents_` 分发。

**Q：为什么 `disableAll()` 选择 remove，而不是 update 成空事件？**

A：没有任何关注事件时，把 fd 留在 epoll 里没有意义。remove 后状态更明确，之后如果重新 `enableReading()`，`Epoller` 会重新 add。

**Q：如果 `Channel` 销毁前没有从 `EventLoop` 移除，会怎样？**

A：`Epoller` 里保存的是 `Channel*`。如果对象已经销毁但 epoll 之后还返回这个指针，就会形成悬空指针，可能导致崩溃或未定义行为。

**Q：为什么测试里还有 `Channel(nullptr, fd)`？**

A：那是为了单独测试 `Epoller` 的 add/mod/del 行为。正式网络对象会传入有效 `EventLoop*`，这样 `Channel` 的事件变化才能自动更新 epoll。
