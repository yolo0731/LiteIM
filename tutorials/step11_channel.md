# Step 11：实现 Channel

## 0. 本 Step 结论

- 目标：Channel 是 Reactor 中的 fd 事件分发器。
- 前置依赖：依赖 Step 0-10 已建立的工程、协议或运行时基础。
- 主要交付：`实现 Channel` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

`Channel` 是 Reactor 中的 fd 事件分发器。Step 10 的 `Epoller` 已经能等待 Linux epoll 事件，并把本轮实际发生的事件写回 `Channel::setRevents()`；Step 11 要补上下一段链路：根据 `revents` 调用 read/write/close/error 回调。

```text
epoll_wait()
    -> Epoller::poll()
    -> Channel::setRevents()
    -> Channel::handleEvent()
    -> readCallback / writeCallback / closeCallback / errorCallback
```

### 为什么需要 Channel

如果 `Epoller` 直接处理业务回调，它就会同时负责系统调用和连接逻辑，边界会变乱。`Channel` 把一个 fd 的事件状态和回调入口集中起来：

- `events_`：我想监听什么事件。
- `revents_`：本轮 epoll 实际返回了什么事件。
- callback：事件到来后调用谁。

这样 `Epoller` 只负责 epoll 系统调用，`Channel` 只负责事件分发，后续 `Session` 只需要把自己的读写函数绑定给 `Channel`。

### close 和 error 为什么优先处理

如果只有 `EPOLLHUP`，说明 fd 已经挂起，而且没有可读数据需要处理。此时直接调用 close callback，然后返回：

```cpp
if ((active_events & EPOLLHUP) != 0 && (active_events & EPOLLIN) == 0) {
    if (close_callback) {
        close_callback();
    }
    return;
}
```

如果发生 `EPOLLERR`，说明 fd 存在错误。Step 17 后的 review hardening 把这里改成：先调用 error callback，但不直接 `return`。如果本轮同时带着 `EPOLLIN` / `EPOLLPRI` / `EPOLLRDHUP`，后面的 read callback 仍会继续执行。原因是错误事件和可读事件可能同时出现，socket 缓冲里仍有数据需要由 `Session::handleRead()` 读完后再关闭。

`EPOLLRDHUP` 归到 read callback，是因为它表示对端关闭了写方向。后续 `Session::handleRead()` 会读到 `0` 或读到剩余数据后判断连接关闭，这比在 `Channel` 里直接关闭更符合职责边界。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `实现 Channel` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `src/net/Channel.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/EventLoop.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/channel_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tutorials/step11_channel.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `Channel.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `EventLoop.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `channel_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

`Channel.hpp` 定义 fd 事件状态和回调分发。

常量和类型：

- `EventCallback = std::function<void()>`。
- `kNoneEvent = 0`，表示不关注任何事件。
- `kReadEvent = EPOLLIN | EPOLLPRI`，表示普通读和高优先级读。
- `kWriteEvent = EPOLLOUT`，表示可写。

构造和所有权：

- `Channel(EventLoop* owner_loop, int fd)` 只保存 owner loop 和 fd 值。
- `Channel` 不拥有 fd，不在析构中 close。
- copy 被禁用，避免一个 fd 出现多个事件代理副本。

事件状态接口：

- `events()` 是希望 epoll 监听的事件。
- `revents()` 是本轮 epoll 实际返回的事件。
- `setRevents()` 只应由 `Epoller::poll()` 写入。
- `isNoneEvent()`、`isReading()`、`isWriting()` 用于判断当前关注集合。

事件开关：

- `enableReading()` / `disableReading()` 修改读关注。
- `enableWriting()` / `disableWriting()` 修改写关注。
- `disableAll()` 清空关注集合。
- 这些函数都会调用 private `update()`，再交给 `owner_loop_->updateChannel(this)`。

回调和生命周期：

- `setReadCallback()`、`setWriteCallback()`、`setCloseCallback()`、`setErrorCallback()` 安装回调。
- `tie(owner)` 保存 owner 的 weak_ptr。事件分发时 lock 成局部 `shared_ptr`，保证 owner 活到 `handleEvent()` 返回。
- `handleEvent()` 根据 `revents_` 直接调用回调。

关键 private 成员：

- `owner_loop_` 决定 Channel 归属哪个 EventLoop。
- `fd_` 是被代理的 fd。
- `events_` / `revents_` 是 Reactor 事件流的核心状态。
- `tied_` / `tie_` 支撑连接对象生命周期保护。
- 四个 callback 是上层对象绑定的事件入口。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

`Channel` 是“一个 fd 在一个 EventLoop 里的事件代理”。`Acceptor` 用它监听 listen fd，`Session` 用它监听连接 fd，`EventLoop` 用它监听 eventfd，`TimerManager` 和 `SignalWatcher` 分别用它接入 timerfd / signalfd。

### 2. 上下层调用连接

```text
fd owner
    -> Channel(owner_loop, fd)
    -> setReadCallback / setWriteCallback / tie
    -> enableReading / enableWriting
    -> EventLoop::updateChannel()
    -> Epoller::poll() writes revents_
    -> Channel::handleEvent()
    -> fd owner callback
```

上游是 fd owner，下游是 `EventLoop` / `Epoller`。`Channel` 不拥有 fd，只保存事件和回调。

### 3. 整体运行链路

1. fd owner 构造 Channel 并传入 owner loop 和 fd。
2. fd owner 设置读、写、关闭、错误回调。
3. 如果回调期间 owner 可能被释放，调用 [tie()](../src/net/Channel.cpp) 绑定弱生命周期保护。
4. fd owner 调用 [enableReading()](../src/net/Channel.cpp) 或 [enableWriting()](../src/net/Channel.cpp)。
5. Channel 更新 `events_` 后调用 [update()](../src/net/Channel.cpp)。
6. EventLoop / Epoller 把事件注册到内核。
7. epoll 返回后，Epoller 调用 `setRevents()` 写入本轮实际事件。
8. EventLoop 调用 [handleEvent()](../src/net/Channel.cpp) 分发回调。

### 4. 自身内部运行流程

整体可以看成 3 步：修改关注事件、接收实际事件、分发回调。

核心成员职责：

- `fd_` 是被代理的 fd，不由 Channel 关闭。
- `events_` 是当前想关注的事件集合。
- `revents_` 是本轮 epoll 返回的实际事件集合。
- `owner_loop_` 是更新和移除所在的 EventLoop。
- `tie_` / `tied_` 保护 owner 生命周期。
- read/write/close/error callback 是 fd owner 安装的行为。

核心函数流程：

- `enableReading()` / `disableReading()`：增删读事件位，然后 `update()`。
- `enableWriting()` / `disableWriting()`：增删写事件位，然后 `update()`。
- `disableAll()`：清空关注事件，让 Epoller 走删除路径。
- `tie()`：保存弱引用，事件分发前尝试提升为强引用。
- `handleEvent()`：按 HUP、ERR、READ、WRITE 顺序检查 `revents_` 并调用回调。

`handleEvent()` 可以理解成“先保护生命周期，再按事件类型分发”：

```text
EventLoop 调用 Channel::handleEvent()
    ↓
tie_ 尝试提升为 shared_ptr，保护 owner 存活
    ↓
保存本轮 revents_ 快照
    ↓
纯关闭事件优先交给 close callback
    ↓
错误事件交给 error callback
    ↓
可读 / urgent / peer half-close 交给 read callback
    ↓
可写事件交给 write callback
```

当前实现允许 `EPOLLERR | EPOLLIN` 先触发 error callback，再继续触发 read callback，这样不会因为错误标志吞掉 socket 缓冲里仍可读取的数据。

### 5. 该项目代码在实际应用中的具体数据例子

Bob 的连接 `session_id=42` 绑定 fd=57。fd=57 同一轮事件带着 `EPOLLIN | EPOLLERR` 返回时，Channel 会先触发 error callback 记录 socket 错误，再继续处理 read callback，避免吞掉已经到达的完整 Packet。`Channel` 本身不 close fd，fd 生命周期仍由 `Session` 的 `UniqueFd` 负责。

## 6. 关键实现点

### Channel 的职责

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

### enable / disable 做了什么

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

### handleEvent 的分发规则

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

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `实现 Channel` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

本 Step 新增：

```cpp
TEST(ChannelTest, EnableAndDisableEventsUpdateInterestMask)
TEST(ChannelTest, ReadableEventInvokesReadCallback)
TEST(ChannelTest, WritableEventInvokesWriteCallback)
TEST(ChannelTest, ReadWriteEventInvokesCallbacksInStableOrder)
TEST(ChannelTest, HangupWithoutReadableEventInvokesCloseOnly)
TEST(ChannelTest, ErrorEventInvokesErrorCallback)
TEST(ChannelTest, ErrorWithReadableEventInvokesErrorThenRead)
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
- `EPOLLERR` 触发 error callback；如果同时有可读事件，会先 error 再 read，避免吞掉剩余数据。
- 没有设置 callback 时，`handleEvent()` 不应该崩溃。
- `tie()` 后 owner 已释放时跳过回调。
- 回调执行期间局部 `shared_ptr` 会让 tied owner 保持存活。
- 事件分发不复制已存储的 callback。

TDD RED 阶段，新增测试后构建失败在：

```text
undefined reference to `liteim::Channel::handleEvent()`
```

这说明测试确实覆盖了当前缺失行为。实现 `handleEvent()` 后，`ChannelTest` 通过。

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

## 8. 验证命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R ChannelTest --output-on-failure
```

## 9. 面试表达

### 一句话

我把 Reactor 中的 fd 事件抽象成 Channel。

### 展开说

可以这样讲：

> 我把 Reactor 中的 fd 事件抽象成 `Channel`。`Channel` 不拥有 fd，只保存关注事件 `events_`、epoll 返回事件 `revents_` 和四类回调。`Epoller` 只负责 epoll 系统调用，poll 返回后把事件写回 `Channel`，再由 `Channel::handleEvent()` 分发到 read/write/close/error callback。关注事件变化时，`Channel` 不直接调用 `epoll_ctl()`，而是通过所属 `EventLoop` 交给 `Epoller` 更新，这样保持了 `Channel`、`EventLoop`、`Epoller` 三层职责清晰。

要强调三个边界：

- `Channel` 不关闭 fd。
- `Channel` 不解析业务协议。
- `Channel` 不直接调用 epoll 系统调用。

### 容易被追问

- 为什么要区分 `events` 和 `revents`？
- 为什么 `Channel` 不直接调用 `epoll_ctl()`？
- 为什么 `EPOLLRDHUP` 走 read callback？
- 为什么现在不再拷贝 callback 到局部变量？
- 不拷贝 callback 会不会有风险？
- Step 11 为什么新增了 `EventLoop.cpp`，但不实现 `loop()`？

## 10. 面试常见追问

### 为什么要区分 `events` 和 `revents`？

`events` 是我想监听什么，`revents` 是 epoll 实际告诉我发生了什么。比如我监听读事件，但本轮可能返回错误事件；两者不能混用。

### 为什么 `Channel` 不直接调用 `epoll_ctl()`？

因为 `Channel` 是 fd 事件代理，不是系统调用封装层。直接调用 `epoll_ctl()` 会和 `Epoller` 职责重叠，也会绕过 `EventLoop` 的线程归属管理。

### 为什么 `EPOLLRDHUP` 走 read callback？

因为对端半关闭时，socket 里可能还有剩余数据。让 read callback 处理，可以由后续 `Session::handleRead()` 统一读到 `0` 或读完剩余数据后关闭连接。

### 为什么现在不再拷贝 callback 到局部变量？

当前实现只拷贝 `revents_`。owner 生命周期由 `Channel::tie()` 的 `weak_ptr` / `shared_ptr` 保护，callback 直接调用成员，避免每次 epoll 事件都复制四个 `std::function`。

### 不拷贝 callback 会不会有风险？

有边界要求。已 `tie()` 的连接类 owner 会在 `handleEvent()` 期间被局部 `shared_ptr` 保住；未 `tie()` 的 `Channel` 必须保证 callback 不会销毁当前 `Channel`，也不会重置正在执行的 callback。后续 `Session` / `TcpConnection` 必须使用 `tie()`。

### Step 11 为什么新增了 `EventLoop.cpp`，但不实现 `loop()`？

因为 `Channel::update()` 已经需要通过 `EventLoop` 更新 epoll 关注事件，所以本 Step 需要最小桥接。但阻塞事件循环、活跃 `Channel` 遍历、跨线程任务投递和 `eventfd` 唤醒是 Step 12 的范围。
