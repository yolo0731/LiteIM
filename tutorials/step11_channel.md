# Step 11：实现 Channel

`Channel` 是 Reactor 中的 fd 事件分发器。Step 10 的 `Epoller` 已经能等待 Linux epoll 事件，并把本轮实际发生的事件写回 `Channel::setRevents()`；Step 11 要补上下一段链路：根据 `revents` 调用 read/write/close/error 回调。

```text
epoll_wait()
    -> Epoller::poll()
    -> Channel::setRevents()
    -> Channel::handleEvent()
    -> readCallback / writeCallback / closeCallback / errorCallback
```

## 1. 为什么需要 Channel

如果 `Epoller` 直接处理业务回调，它就会同时负责系统调用和连接逻辑，边界会变乱。`Channel` 把一个 fd 的事件状态和回调入口集中起来：

- `events_`：我想监听什么事件。
- `revents_`：本轮 epoll 实际返回了什么事件。
- callback：事件到来后调用谁。

这样 `Epoller` 只负责 epoll 系统调用，`Channel` 只负责事件分发，后续 `Session` 只需要把自己的读写函数绑定给 `Channel`。

## 2. 本 Step 新增和修改的文件

```text
src/net/Channel.cpp
src/net/EventLoop.cpp
tests/net/channel_test.cpp
tutorials/step11_channel.md
```

`Channel.cpp` 从 Step 10 的状态函数扩展为真正的事件分发器。

`EventLoop.cpp` 在本 Step 只实现最小桥接：`Channel` 调用 `owner_loop_->updateChannel(this)`，`EventLoop` 再交给 `Epoller`。它还不实现 `loop()` 阻塞循环，也不实现 `eventfd` 任务投递，这些属于 Step 12。

`channel_test.cpp` 使用 GoogleTest 验证回调分发和关注事件开关。

## 3. Channel 的职责

`Channel` 不拥有 fd。它只保存 fd 值和事件状态：

```cpp
EventLoop* owner_loop_;
const int fd_;
std::uint32_t events_;
std::uint32_t revents_;
```

fd 的关闭后续由 `Acceptor`、`Session` 或其他 RAII owner 负责。这个边界很重要，因为同一个 fd 不能被多个对象重复关闭。

`Channel` 的事件常量是：

```cpp
kNoneEvent = 0
kReadEvent = EPOLLIN | EPOLLPRI
kWriteEvent = EPOLLOUT
```

`events_` 和 `revents_` 要区分开：

| 字段 | 含义 | 谁写 |
| --- | --- | --- |
| `events_` | 希望 epoll 监听的事件 | `enableReading()` / `enableWriting()` 等函数 |
| `revents_` | epoll 本轮实际返回的事件 | `Epoller::poll()` |

## 4. enable / disable 做了什么

以 `enableReading()` 为例：

```cpp
void Channel::enableReading() {
    events_ |= kReadEvent;
    update();
}
```

第一行把读事件加入关注集合。第二行通知所属 `EventLoop`：

```cpp
void Channel::update() {
    if (owner_loop_ != nullptr) {
        owner_loop_->updateChannel(this);
    }
}
```

最终链路是：

```text
Channel::enableReading()
    -> Channel::update()
    -> EventLoop::updateChannel()
    -> Epoller::updateChannel()
    -> epoll_ctl(ADD/MOD/DEL)
```

这里 `owner_loop_ == nullptr` 时不会更新 epoll。这是为了让纯单元测试和 `EpollerTest` 可以直接构造一个不绑定 `EventLoop` 的 `Channel`，再手动调用 `Epoller::updateChannel()`。

## 5. handleEvent 的分发规则

`handleEvent()` 根据 `revents_` 调用不同回调：

| revents | 回调 |
| --- | --- |
| `EPOLLHUP` 且没有 `EPOLLIN` | `closeCallback` |
| `EPOLLERR` | `errorCallback` |
| `EPOLLIN` / `EPOLLPRI` / `EPOLLRDHUP` | `readCallback` |
| `EPOLLOUT` | `writeCallback` |

实现上只把 `revents_` 保存到局部变量，callback 直接从成员调用：

```cpp
const auto active_events = revents_;
```

Step 13 review hardening 后，`Channel::tie()` 已经用 `weak_ptr` 管理 owner 生命周期：`handleEvent()` 事件分发前先 lock owner，回调执行期间用局部 `shared_ptr` 保持 owner 存活。guard 和事件分发 body 在同一个栈帧里，因此可以直接看出 owner 会活到 `handleEvent()` 返回。同时 `handleEvent()` 不再每次事件都复制四个 `std::function`，避免高频 `EPOLLIN` 路径上产生不必要的回调对象复制和潜在堆分配。

后续 `Session` / `TcpConnection` 仍然要把 owner `shared_ptr` 传给 `tie()`，这样回调里关闭连接或释放外部引用时，事件分发期间对象仍然是稳定的。

这个优化带来一个明确契约：`Channel` 不会用局部 `std::function` 副本保护未 `tie()` 的回调。未 `tie()` 的轻量 `Channel` 只能用于 `Acceptor::listen_channel_`、`EventLoop::wakeup_channel_` 这类 owner 生命周期明确的场景；callback 内部不得直接或间接析构当前 `Channel`，也不得重置正在执行的 callback。只要 callback 可能释放 owner，就必须先调用 `tie()`。

## 6. close 和 error 为什么优先处理

如果只有 `EPOLLHUP`，说明 fd 已经挂起，而且没有可读数据需要处理。此时直接调用 close callback，然后返回：

```cpp
if ((active_events & EPOLLHUP) != 0 && (active_events & EPOLLIN) == 0) {
    if (close_callback) {
        close_callback();
    }
    return;
}
```

如果发生 `EPOLLERR`，说明 fd 存在错误。当前实现调用 error callback 后返回，避免在错误状态下继续触发普通读写回调。

`EPOLLRDHUP` 归到 read callback，是因为它表示对端关闭了写方向。后续 `Session::handleRead()` 会读到 `0` 或读到剩余数据后判断连接关闭，这比在 `Channel` 里直接关闭更符合职责边界。

## 7. EventLoop.cpp 在本 Step 的边界

本 Step 新增了 `src/net/EventLoop.cpp`，但它不是完整 EventLoop。

当前只实现：

- 构造 `Epoller`。
- `quit()` 设置退出标记。
- `updateChannel()` 调用 `Epoller::updateChannel()`。
- `removeChannel()` 调用 `Epoller::removeChannel()`。
- `isInLoopThread()` 检查当前线程是否是 loop 所属线程。
- `assertInLoopThread()` 在跨线程误用时抛出 `std::logic_error`。

当前不实现：

- `EventLoop::loop()` 阻塞循环。
- `eventfd` 唤醒。
- `runInLoop()` / `queueInLoop()`。
- 跨线程任务队列。

这些会在 Step 12 完整展开。

## 8. 测试设计

本 Step 新增：

```cpp
TEST(ChannelTest, EnableAndDisableEventsUpdateInterestMask)
TEST(ChannelTest, ReadableEventInvokesReadCallback)
TEST(ChannelTest, WritableEventInvokesWriteCallback)
TEST(ChannelTest, ReadWriteEventInvokesCallbacksInStableOrder)
TEST(ChannelTest, HangupWithoutReadableEventInvokesCloseOnly)
TEST(ChannelTest, ErrorEventInvokesErrorCallback)
TEST(ChannelTest, HandleEventToleratesMissingCallbacks)
TEST(ChannelTest, TiedExpiredOwnerSkipsCallbacks)
TEST(ChannelTest, TiedOwnerStaysAliveDuringCallback)
TEST(ChannelTest, HandleEventDoesNotCopyStoredCallbacks)
```

这些测试分别验证：

- `enableReading()` / `enableWriting()` / `disableReading()` / `disableWriting()` / `disableAll()` 是否正确修改 `events_`。
- `EPOLLIN` 是否触发 read callback。
- `EPOLLOUT` 是否触发 write callback。
- 同时有读写事件时，先读后写。
- 只有 `EPOLLHUP` 时触发 close callback，不触发 read callback。
- `EPOLLERR` 触发 error callback。
- 没有设置 callback 时，`handleEvent()` 不应该崩溃。
- `tie()` 后 owner 已释放时跳过回调。
- 回调执行期间局部 `shared_ptr` 会让 tied owner 保持存活。
- 事件分发不复制已存储的 callback。

TDD RED 阶段，新增测试后构建失败在：

```text
undefined reference to `liteim::Channel::handleEvent()`
```

这说明测试确实覆盖了当前缺失行为。实现 `handleEvent()` 后，`ChannelTest` 通过。

## 9. 如何运行测试

运行全部验证：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

只运行本 Step 的测试：

```bash
ctest --test-dir build -R ChannelTest --output-on-failure
```

当前 Step 11 初次完成时 CTest 通过 88 个测试，其中 7 个是新增的 `ChannelTest`。Step 13 review hardening 后又补充了 `tie()` 和 callback no-copy 回归测试。

## 10. 面试时怎么讲

可以这样讲：

> 我把 Reactor 中的 fd 事件抽象成 `Channel`。`Channel` 不拥有 fd，只保存关注事件 `events_`、epoll 返回事件 `revents_` 和四类回调。`Epoller` 只负责 epoll 系统调用，poll 返回后把事件写回 `Channel`，再由 `Channel::handleEvent()` 分发到 read/write/close/error callback。关注事件变化时，`Channel` 不直接调用 `epoll_ctl()`，而是通过所属 `EventLoop` 交给 `Epoller` 更新，这样保持了 `Channel`、`EventLoop`、`Epoller` 三层职责清晰。

要强调三个边界：

- `Channel` 不关闭 fd。
- `Channel` 不解析业务协议。
- `Channel` 不直接调用 epoll 系统调用。

## 11. 面试常见追问

**为什么要区分 `events` 和 `revents`？**

`events` 是我想监听什么，`revents` 是 epoll 实际告诉我发生了什么。比如我监听读事件，但本轮可能返回错误事件；两者不能混用。

**为什么 `Channel` 不直接调用 `epoll_ctl()`？**

因为 `Channel` 是 fd 事件代理，不是系统调用封装层。直接调用 `epoll_ctl()` 会和 `Epoller` 职责重叠，也会绕过 `EventLoop` 的线程归属管理。

**为什么 `EPOLLRDHUP` 走 read callback？**

因为对端半关闭时，socket 里可能还有剩余数据。让 read callback 处理，可以由后续 `Session::handleRead()` 统一读到 `0` 或读完剩余数据后关闭连接。

**为什么现在不再拷贝 callback 到局部变量？**

当前实现只拷贝 `revents_`。owner 生命周期由 `Channel::tie()` 的 `weak_ptr` / `shared_ptr` 保护，callback 直接调用成员，避免每次 epoll 事件都复制四个 `std::function`。

**不拷贝 callback 会不会有风险？**

有边界要求。已 `tie()` 的连接类 owner 会在 `handleEvent()` 期间被局部 `shared_ptr` 保住；未 `tie()` 的 `Channel` 必须保证 callback 不会销毁当前 `Channel`，也不会重置正在执行的 callback。后续 `Session` / `TcpConnection` 必须使用 `tie()`。

**Step 11 为什么新增了 `EventLoop.cpp`，但不实现 `loop()`？**

因为 `Channel::update()` 已经需要通过 `EventLoop` 更新 epoll 关注事件，所以本 Step 需要最小桥接。但阻塞事件循环、活跃 `Channel` 遍历、跨线程任务投递和 `eventfd` 唤醒是 Step 12 的范围。
