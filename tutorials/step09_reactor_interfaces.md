# Step 9：定义 Reactor 核心接口

本 Step 进入 Reactor 的接口层，但还不实现真实 `epoll` 行为。

目标是先把 `Channel`、`Epoller`、`EventLoop` 三个核心角色的边界定下来，避免后续 `Epoller.cpp`、`Channel.cpp`、`EventLoop.cpp` 互相包含、互相依赖，导致循环 include 或职责混乱。

```text
EventLoop
    owns Epoller
    manages Channel

Channel
    represents one fd and interested events

Epoller
    wraps epoll wait/update/remove boundary
```

## 1. 为什么先定义接口

Reactor 模型里有三个层次：

- `Channel`：fd 事件抽象层。
- `Epoller`：Linux `epoll` 系统调用封装层。
- `EventLoop`：事件调度层。

如果一开始就直接写 `.cpp`，很容易把三件事混在一起：`Channel` 里调用 `epoll_ctl()`，`Epoller` 里处理业务回调，`EventLoop` 里直接操作 fd。这样后续扩展 `Acceptor`、`Session`、`EventLoopThreadPool` 时边界会变乱。

所以本 Step 只定义头文件接口：

- 让后续 Step 10 可以专注实现 `Epoller`。
- 让 Step 11 可以专注实现 `Channel` 的事件开关和回调分发。
- 让 Step 12 可以专注实现 `EventLoop` 的事件循环、`eventfd` 唤醒和任务投递。

## 2. 本 Step 新增文件

```text
include/liteim/net/Channel.hpp
include/liteim/net/Epoller.hpp
include/liteim/net/EventLoop.hpp
tests/net/channel_header_test.cpp
tests/net/epoller_header_test.cpp
tests/net/event_loop_header_test.cpp
```

同时更新：

```text
tests/CMakeLists.txt
```

本 Step 没有新增：

```text
src/net/Channel.cpp
src/net/Epoller.cpp
src/net/EventLoop.cpp
```

因为当前只验证接口和头文件依赖关系，不实现运行时行为。

## 3. Channel 接口

`Channel` 表示一个 fd 在 Reactor 里的事件代理。

公开接口分几类：

```cpp
using EventCallback = std::function<void()>;

int fd() const noexcept;
std::uint32_t events() const noexcept;
std::uint32_t revents() const noexcept;
void setRevents(std::uint32_t revents) noexcept;
```

含义：

- `fd()`：返回这个 `Channel` 代表的 fd。
- `events()`：当前希望 epoll 监听的事件。
- `revents()`：本轮 `epoll_wait()` 实际返回的事件。
- `setRevents()`：由 `Epoller` 在 poll 返回后写入实际事件。

事件开关接口：

```cpp
void enableReading();
void disableReading();
void enableWriting();
void disableWriting();
void disableAll();
```

这些函数后续会改变 `events_`，再通过私有 `update()` 通知所属 `EventLoop`，最后由 `EventLoop` 交给 `Epoller` 更新 epoll 关注事件。

回调接口：

```cpp
void setReadCallback(EventCallback callback);
void setWriteCallback(EventCallback callback);
void setCloseCallback(EventCallback callback);
void setErrorCallback(EventCallback callback);
void handleEvent();
```

后续 `handleEvent()` 会根据 `revents_` 调用对应回调。本 Step 只声明接口，不写分发逻辑。

## 4. Channel 不拥有 fd

`Channel` 有一个很重要的边界：

```text
Channel represents fd, but does not own fd.
```

也就是说，`Channel` 不负责关闭 fd。fd 的生命周期后续会由 `Acceptor`、`Session` 或其他 RAII owner 管理。

这样做的原因是：

- 一个连接对象通常需要同时管理 fd、输入输出 buffer、协议解码器和回调。
- 如果 `Channel` 自己关闭 fd，连接对象析构时也关闭 fd，就可能重复关闭。
- `Channel` 只做事件代理，连接生命周期留给更上层对象。

## 5. Epoller 接口

`Epoller` 是 Linux epoll 的薄封装边界。

公开接口：

```cpp
using ChannelList = std::vector<Channel*>;

Status poll(int timeout_ms, ChannelList& active_channels);
Status updateChannel(Channel* channel);
Status removeChannel(Channel* channel);
```

含义：

- `poll()`：后续会调用 `epoll_wait()`，通过输出参数返回本轮活跃的 `Channel*` 列表，并用 `Status` 表达系统调用结果。
- `updateChannel()`：后续会根据 `Channel::events()` 做 `EPOLL_CTL_ADD` 或 `EPOLL_CTL_MOD`，并用 `Status` 返回无效参数或系统调用错误。
- `removeChannel()`：后续会把 fd 从 epoll 中删除，并用 `Status` 返回结果。

本 Step 的 `Epoller.hpp` 预留了私有状态：

```cpp
int epoll_fd_;
std::vector<epoll_event> events_;
std::unordered_map<int, Channel*> channels_;
```

但真实的 `epoll_create1()`、`epoll_ctl()`、`epoll_wait()` 会放到 Step 10。

## 6. EventLoop 接口

`EventLoop` 是 Reactor 的调度层。

公开接口：

```cpp
void loop();
void quit() noexcept;

void updateChannel(Channel* channel);
void removeChannel(Channel* channel);

bool isInLoopThread() const noexcept;
void assertInLoopThread() const;
```

含义：

- `loop()`：后续会进入事件循环，反复调用 `Epoller::poll()`。
- `quit()`：后续用于请求退出事件循环。
- `updateChannel()` / `removeChannel()`：给 `Channel` 和上层连接对象提供统一入口，不让它们直接操作 `Epoller`。
- `isInLoopThread()` / `assertInLoopThread()`：为 one-loop-per-thread 模型准备线程归属检查。

`EventLoop` 通过：

```cpp
std::unique_ptr<Epoller> epoller_;
```

表达“一个 loop 拥有一个 epoller”。真实构造、析构、阻塞循环、任务队列和 `eventfd` 唤醒都在后续 Step 实现。

## 7. 为什么用前向声明

三个头文件之间有天然依赖：

- `Channel` 需要知道自己属于哪个 `EventLoop`。
- `Epoller` 需要返回活跃的 `Channel*`。
- `EventLoop` 需要持有 `Epoller` 并管理 `Channel`。

如果互相 `#include`，很容易形成循环 include。

本 Step 使用前向声明：

```cpp
class Channel;
class Epoller;
class EventLoop;
```

只在头文件里保存指针或声明函数参数，不需要完整类型定义。这样三个头文件可以独立 include。

## 8. 本 Step 不做什么

本 Step 不实现：

- `src/net/Epoller.cpp`
- `src/net/Channel.cpp`
- `src/net/EventLoop.cpp`
- `epoll_create1()`
- `epoll_ctl()`
- `epoll_wait()`
- `Channel::handleEvent()` 回调分发
- `EventLoop::loop()` 阻塞循环
- `eventfd` 跨线程唤醒
- `Acceptor`
- `Session`
- `TcpServer`

这些分别属于后续 Step。

## 9. 测试清单

新增测试文件：

```text
tests/net/channel_header_test.cpp
tests/net/epoller_header_test.cpp
tests/net/event_loop_header_test.cpp
```

测试用例：

```cpp
TEST(ReactorInterfaceTest, ChannelHeaderIsSelfContained)
TEST(ReactorInterfaceTest, EpollerHeaderIsSelfContained)
TEST(ReactorInterfaceTest, EventLoopHeaderIsSelfContained)
```

这些测试覆盖：

- 三个头文件都可以独立 include。
- `Channel`、`Epoller`、`EventLoop` 都禁止拷贝。
- `Channel` 暴露读写事件常量和回调 setter。
- `Epoller` 暴露 `ChannelList`、`poll()`、`updateChannel()`、`removeChannel()`。
- `EventLoop` 暴露 `loop()`、`quit()`、`updateChannel()`、`removeChannel()` 和线程归属检查。

本 Step 的 TDD RED 是先新增测试并构建，构建失败于：

```text
fatal error: liteim/net/Channel.hpp: No such file or directory
```

然后补齐三个头文件，构建和新增测试通过。

## 10. 验证命令

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

当前 Step 9 预期 CTest 通过 76 个测试，其中 3 个是新增的 `ReactorInterfaceTest`。

## 11. 面试讲法

可以这样讲：

> 我把 Reactor 核心拆成三层：`Channel` 表示 fd 和关注事件，`Epoller` 只封装 Linux epoll 的注册、删除和等待边界，`EventLoop` 负责持有 `Epoller` 并调度活跃 `Channel`。Step 9 只定义接口和头文件依赖关系，不实现系统调用，这样后续可以分别实现 epoll wrapper、事件回调分发和 eventfd 任务唤醒，避免一开始就把职责耦合在一起。

重点边界：

- `Channel` 不拥有 fd。
- `events_` 是想监听的事件，`revents_` 是 epoll 实际返回的事件。
- `Epoller` 是系统调用层，不处理业务回调。
- `EventLoop` 是调度层，不直接承载业务逻辑。

## 12. 面试常见追问

**为什么 `Channel` 不直接调用 epoll？**

因为 `Channel` 是 fd 事件代理。如果它直接调用 `epoll_ctl()`，就会知道太多系统调用细节，也会和 `Epoller` 职责重叠。当前设计是 `Channel` 改变关注事件后通知 `EventLoop`，再由 `EventLoop` 交给 `Epoller` 更新。

**`events` 和 `revents` 有什么区别？**

`events` 是程序希望监听的事件，比如读事件或写事件。`revents` 是 `epoll_wait()` 本轮实际返回的事件，比如这个 fd 真的可读、可写、关闭或出错。

**为什么 Step 9 只有头文件，没有 `.cpp`？**

因为本 Step 的目标是定接口和依赖边界。真实行为会拆到后续步骤：Step 10 实现 `Epoller`，Step 11 实现 `Channel`，Step 12 实现 `EventLoop` 和 `eventfd` 任务投递。

**为什么需要 `isInLoopThread()`？**

one-loop-per-thread 模型要求一个 `EventLoop` 固定在自己的线程里处理 fd 事件。线程归属检查可以帮助后续发现跨线程误操作，跨线程任务需要通过 `queueInLoop()` 或 `runInLoop()` 回到正确 I/O 线程。

## 13. 提交信息

```text
feat(net): define reactor core interfaces
```
