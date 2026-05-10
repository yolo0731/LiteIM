# Step 10：实现 Epoller

本 Step 在 Step 9 的接口基础上实现真正的 Linux `epoll` 封装。

`Epoller` 是 Reactor 里的系统调用层。它不处理业务，不解析协议，也不负责关闭连接 fd。它只做一件事：把 `Channel` 注册到 epoll，等待 fd 事件，再把活跃事件写回对应的 `Channel`。

```text
EventLoop
    -> Epoller
        -> epoll_create1 / epoll_ctl / epoll_wait
    -> Channel
        -> fd + events + revents
```

## 1. 为什么需要 Epoller

Linux 原生 `epoll` API 是偏底层的：

- `epoll_create1()` 创建 epoll 实例。
- `epoll_ctl()` 负责 add / mod / del fd。
- `epoll_wait()` 返回活跃事件数组。
- 每个 `epoll_event` 里需要自己保存能找回上层对象的数据。

如果这些系统调用散落在 `EventLoop`、`Session`、`Acceptor` 里，后续会很难维护。`Epoller` 把这些细节集中起来，让上层只面对 `Channel*`。

## 2. 本 Step 新增文件

```text
src/net/Epoller.cpp
src/net/Channel.cpp
tests/net/epoller_test.cpp
```

同时更新：

```text
include/liteim/net/Epoller.hpp
src/net/CMakeLists.txt
tests/CMakeLists.txt
```

`Channel.cpp` 在本 Step 只实现最小状态函数，用于支撑 `Epoller` 测试。它还不实现 `handleEvent()` 回调分发，也不实现自动 `update()`。

## Epoller.hpp 接口说明

`Epoller.hpp` 是 Reactor 系统调用层的契约。

public 类型和构造：

- `ChannelList = std::vector<Channel*>`，`poll()` 每轮返回的活跃 Channel 列表。
- `explicit Epoller(EventLoop* owner_loop)` 创建 epoll 实例，失败时抛 `std::runtime_error`，因为没有 epoll fd 的对象不可用。
- 析构函数关闭 `epoll_fd_`，但不关闭业务 fd。

public 函数：

- `poll(timeout_ms, active_channels)` 调用 `epoll_wait()`。成功时清空并填充 `active_channels`；`EINTR` 返回成功但无事件；其他系统调用错误返回 `IoError`。
- `updateChannel(channel)` 校验 Channel 和 owner loop，再根据 `channels_` 是否已有 fd 选择 add/mod/del。新增无关注事件的 Channel 会返回 `InvalidArgument`。
- `removeChannel(channel)` 执行 `EPOLL_CTL_DEL`，删除 `channels_` 中的 fd 映射，并清空 Channel 的 `revents`。

关键 private 成员：

- `owner_loop_` 用于校验 Channel 是否属于同一个 EventLoop。
- `epoll_fd_` 是当前对象拥有的 epoll 实例 fd。
- `events_` 是 `epoll_wait()` 的输出缓冲，事件满时会扩容。
- `channels_` 是 `fd -> Channel*` 映射，用来判断 add/mod/del 和防止同一 fd 被不同 Channel 重复注册。

关键 private helper：

- `validateChannelOwner()` 防止跨 loop 注册 Channel，这对 one-loop-per-thread 模型很重要。
- `.cpp` 里的 `makeEpollEvent()` 把 `Channel::events()` 和 `Channel*` 填进 `epoll_event`。

线程和所有权边界：

- `Epoller` 归属于一个 `EventLoop`，只应在 owner loop 线程使用。
- `Epoller` 拥有 epoll fd，不拥有 `Channel`，也不拥有业务 fd。
- `channels_` 中保存的是裸指针，调用方必须先 `removeChannel()` 再销毁 Channel。

## Epoller 的作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

`Epoller` 是 `EventLoop` 内部的 epoll 封装。`Channel` 开关读写事件时最终落到 `Epoller::updateChannel()`；`EventLoop::loop()` 每轮通过 `Epoller::poll()` 取回活跃 Channel。

### 2. 上下层调用连接

```text
Channel::enableReading() / enableWriting()
    -> EventLoop::updateChannel()
    -> Epoller::updateChannel()
    -> epoll_ctl(ADD / MOD / DEL)

EventLoop::loop()
    -> Epoller::poll()
    -> epoll_wait()
    -> Channel::setRevents()
    -> EventLoop dispatches Channel::handleEvent()
```

上游是 `EventLoop` 和 `Channel`，下游是 Linux epoll fd。

### 3. 整体运行链路

1. `EventLoop` 构造时创建一个 `Epoller`。
2. 上层 Channel 修改 `events_` 后调用 `EventLoop::updateChannel()`。
3. [Epoller::updateChannel()](../src/net/Epoller.cpp#L96) 判断 fd 是否已经注册。
4. 新 fd 且有关注事件时执行 `EPOLL_CTL_ADD`。
5. 已注册 fd 且关注事件为空时执行 `EPOLL_CTL_DEL`。
6. 已注册 fd 且仍有关注事件时执行 `EPOLL_CTL_MOD`。
7. [Epoller::poll()](../src/net/Epoller.cpp#L64) 调用 `epoll_wait()`。
8. 每个返回事件通过 `data.ptr` 找回 `Channel*`，写入 `revents_` 后交给 EventLoop。

### 4. 自身内部运行流程

整体可以看成 3 步：维护注册表、等待事件、清理注册。

核心成员职责：

- `epoll_fd_` 是内核 epoll 实例。
- `events_` 是 `epoll_wait()` 输出数组，满了会扩容。
- `channels_` 保存 fd 到 `Channel*` 的映射，用来判断 add/mod/del 和防 fd 复用混乱。
- `owner_loop_` 用来校验 Channel 是否属于同一个 EventLoop。

核心函数流程：

- [validateChannelOwner()](../src/net/Epoller.cpp#L56)：拒绝把其他 EventLoop 的 Channel 注册进来。
- [poll()](../src/net/Epoller.cpp#L64)：处理 `EINTR`、填充 active_channels、必要时扩容事件数组。
- [updateChannel()](../src/net/Epoller.cpp#L96)：统一处理 add/mod/del。
- [removeChannel()](../src/net/Epoller.cpp#L145)：显式从 epoll 删除 Channel，并清空 `revents_`。

`updateChannel(channel)` 伪流程：

```text
validate channel and owner
fd = channel.fd()
if fd not in channels_:
    if channel has no events: return error
    epoll_ctl(ADD, fd, channel.events)
    channels_[fd] = channel
elif channels_[fd] != channel:
    return error
elif channel has no events:
    epoll_ctl(DEL, fd)
    erase fd from channels_
else:
    epoll_ctl(MOD, fd, channel.events)
```

### 5. 小例子和边界

小例子：`Session` 输出缓冲从空变非空时调用 `channel_->enableWriting()`，最终 `Epoller` 对连接 fd 做 `EPOLL_CTL_MOD`，让 epoll 开始关注 `EPOLLOUT`。

边界：`epoll_wait()` 被信号打断返回 `EINTR` 不算 fatal；事件数组填满会扩容；同一个 fd 已经被另一个 Channel 注册时返回错误，避免 fd 复用导致旧 Channel 被误触发。

## 3. Epoller 的公开接口

```cpp
using ChannelList = std::vector<Channel*>;

Status poll(int timeout_ms, ChannelList& active_channels);
Status updateChannel(Channel* channel);
Status removeChannel(Channel* channel);
```

接口含义：

- `poll()`：调用 `epoll_wait()`，把本轮活跃的 `Channel*` 写入 `active_channels`。
- `updateChannel()`：根据 `Channel::events()` 决定执行 `EPOLL_CTL_ADD` 或 `EPOLL_CTL_MOD`。
- `removeChannel()`：执行 `EPOLL_CTL_DEL`，把 fd 从 epoll 里移除。

这三个接口都返回 `Status`，因为系统调用可能失败，传入的 `Channel*` 也可能无效。底层网络模块不能直接退出进程。

## 4. epoll fd 的生命周期

`Epoller` 构造时调用：

```cpp
epoll_create1(EPOLL_CLOEXEC)
```

`EPOLL_CLOEXEC` 的作用是设置 close-on-exec，避免进程执行新程序时意外继承 epoll fd。

如果 `epoll_create1()` 返回失败，构造函数会直接抛出 `std::runtime_error`。原因是 `Epoller` 没有有效 epoll fd 时无法提供任何可用能力，让对象“半初始化”存在反而会把错误延迟到 `EventLoop` 注册 wakeup channel 时才暴露，还可能让已经创建的其他 fd 缺少 RAII 清理边界。

析构时关闭 epoll fd：

```cpp
if (epoll_fd_ >= 0) {
    close(epoll_fd_);
}
```

这里 `Epoller` 拥有的是 epoll 实例 fd，不拥有业务连接 fd。连接 fd 后续由 `Acceptor`、`Session` 等对象管理。

## 5. Channel 和 epoll_event 的关系

注册事件时，`Epoller` 会构造：

```cpp
epoll_event event{};
event.events = channel->events();
event.data.ptr = channel;
```

关键点是：

```text
epoll_event.data.ptr stores Channel*
```

这样 `epoll_wait()` 返回事件后，`Epoller` 可以把 `data.ptr` 转回 `Channel*`，再调用：

```cpp
channel->setRevents(events_[i].events);
```

`events_` 和 `revents_` 的区别：

- `events_`：我想监听什么事件。
- `revents_`：这次 epoll 实际告诉我发生了什么事件。

## 6. updateChannel 的 add / mod / del 规则

`Epoller` 内部维护：

```cpp
std::unordered_map<int, Channel*> channels_;
```

它用于记录 fd 是否已经注册过。

规则：

- fd 不在 `channels_` 中：执行 `EPOLL_CTL_ADD`。
- fd 已在 `channels_` 中，且还有关注事件：执行 `EPOLL_CTL_MOD`。
- fd 已在 `channels_` 中，但 `Channel::isNoneEvent()` 为 true：执行 `EPOLL_CTL_DEL` 并从 map 删除。

`removeChannel()` 是显式删除入口。它会检查 fd 是否真的注册过，未注册则返回错误。

Step 16 前代码清理后，`Epoller` 还会检查 `Channel::ownerLoop()`：

```text
Epoller owner_loop_ != nullptr
Channel ownerLoop() 必须等于 owner_loop_
```

这样可以防止一个 `Channel` 被错误注册到其他 `EventLoop` 的 epoll 实例里，维护 one-loop-per-thread 边界。

## 7. 为什么第一版使用 LT 模式

本项目当前使用 LT，也就是 level-triggered 模式。

LT 的特点是：

```text
只要 fd 状态仍然满足条件，epoll_wait 后续还会继续报告它。
```

例如 pipe 里还有数据没读完，那么下一次 `epoll_wait()` 仍会报告可读。

本 Step 暂时不使用 `EPOLLET`。ET 模式要求读写回调必须循环读写到 `EAGAIN` / `EWOULDBLOCK`，这个细节更适合在后续 `Session::handleRead()` / `handleWrite()` 里讨论，而不是放进 `Epoller::poll()`。

## 8. 错误处理

本 Step 的错误处理规则：

- `nullptr` channel 返回 `InvalidArgument`。
- 负 fd channel 返回 `InvalidArgument`。
- owner loop 不匹配返回 `InvalidArgument`。
- 没有关注事件的新 channel 不能 add，返回 `InvalidArgument`。
- 删除未注册 channel 返回 `InvalidArgument`。
- 系统调用失败返回 `IoError`。
- `epoll_wait()` 被信号中断时，返回空 active list 和 `Status::ok()`。

这里不在底层工具里 `exit()`，因为服务端后续需要让上层决定是记录日志、关闭连接、重试，还是停止服务。

## 9. Channel.cpp 在本 Step 做什么

`Channel.cpp` 当前只实现：

- 构造函数。
- `fd()` / `events()` / `revents()`。
- `setRevents()`。
- `isNoneEvent()` / `isReading()` / `isWriting()`。
- `enableReading()` / `disableReading()`。
- `enableWriting()` / `disableWriting()`。
- `disableAll()`。
- 回调 setter。

当前不实现：

- `Channel::handleEvent()`。
- `Channel::update()` 自动通知 `EventLoop`。

原因是本 Step 只验证 `Epoller` 能正确注册和等待 fd 事件。事件分发属于下一步。

## 10. 测试清单

新增测试文件：

```text
tests/net/epoller_test.cpp
```

测试用例：

```cpp
TEST(EpollerTest, AddChannelReceivesReadableEvent)
TEST(EpollerTest, ModifyChannelToWriteInterestTakesEffect)
TEST(EpollerTest, RemoveChannelStopsEvents)
TEST(EpollerTest, PollTimeoutReturnsEmptyActiveList)
TEST(EpollerTest, InvalidChannelOperationsReturnError)
TEST(EpollerTest, RejectsChannelOwnedByDifferentEventLoop)
```

这些测试使用真实 `pipe()` fd，而不是 mock。

覆盖点：

- 注册读端 fd 后，写端写入数据，`poll()` 能返回可读事件。
- 修改关注事件后，写端 fd 的可写事件能被返回。
- 删除 fd 后，即使 fd 后续变得可读，也不会再被 `poll()` 返回。
- 没有事件时，短 timeout 返回空列表。
- `nullptr` channel、负 fd channel、删除未注册 channel 等无效操作返回错误。

## 11. TDD 过程

本 Step 的 RED：

```text
EpollerHeaderIsSelfContained static_assert failed
```

原因是测试先要求 `Epoller` 接口改成返回 `Status`，而旧接口仍是 `ChannelList poll(int)` 和 `void update/remove`。

然后实现 `Epoller.cpp`、`Channel.cpp`，并更新 CMake，focused Epoller 测试通过。

## 12. 验证命令

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

当前 Step 10 预期 CTest 通过 81 个测试，其中 5 个是新增的 `EpollerTest`。

## 13. 面试讲法

可以这样讲：

> 我把 Linux epoll 封装成 `Epoller`，它负责创建 epoll fd、用 `epoll_ctl` 管理 fd 关注事件、用 `epoll_wait` 等待活跃事件，并通过 `epoll_event.data.ptr` 保存和取回 `Channel*`。`Epoller` 不处理业务回调，也不拥有连接 fd；它只是 Reactor 的系统调用层。第一版使用 LT 模式，错误通过 `Status` 返回，便于上层决定如何处理。

重点边界：

- `Epoller` 拥有 epoll fd，不拥有连接 fd。
- `Channel` 保存 fd 和事件状态，不做业务。
- `EventLoop` 后续负责调度活跃 `Channel`。
- `Session` 后续负责连接生命周期和读写 buffer。

## 14. 面试常见追问

**为什么 `epoll_event.data.ptr` 存 `Channel*`？**

因为 `epoll_wait()` 返回时只告诉我们哪些 fd 活跃。存 `Channel*` 后，可以直接回到上层 fd 事件对象，不需要再查一遍业务对象。

**为什么要维护 `fd -> Channel*` map？**

因为 `epoll_ctl` 需要区分 add、mod、del。map 能告诉我们 fd 是否已经注册，也能防止同一个 fd 被不同 `Channel` 混用。

**为什么不用 ET？**

第一版用 LT 更容易验证，也更适合教学推进。ET 需要回调里循环读写到 `EAGAIN`，这属于后续 `Session` 的真实 I/O 处理边界，不应该放进 `Epoller`。

**`epoll_wait()` 被信号打断怎么办？**

如果返回 `-1` 且 `errno == EINTR`，本实现把它当成非致命情况，返回空 active list 和 `Status::ok()`。上层下一轮可以继续 poll。

## 15. 提交信息

```text
feat(net): implement epoller wrapper
```
