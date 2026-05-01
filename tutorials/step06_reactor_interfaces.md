# Step 6：定义 Epoller / Channel / EventLoop 头文件接口

本步骤目标：先定义 LiteIM 网络层 Reactor 的核心接口，为后续 `epoll`、事件循环、连接接入做准备。

这一步只写头文件，不写复杂实现。

## 1. 这一步要解决什么问题

Step 5 已经封装了 `SocketUtil`，后续我们可以创建非阻塞 socket。

但是一个服务端不能只会创建 socket，还需要持续做这些事情：

- 等待哪些 fd 可读、可写、异常或关闭。
- fd 可读时执行读回调。
- fd 可写时执行写回调。
- 连接关闭或出错时执行清理回调。
- 新连接、普通客户端连接、定时器 fd、信号 fd 都能统一放进事件循环。

这就是 Reactor 模型要解决的问题。

第一版 LiteIM 的 Reactor 拆成三个核心类：

- `Epoller`：封装 Linux `epoll`。
- `Channel`：封装一个 fd 的事件和回调。
- `EventLoop`：事件循环，负责等待事件并分发事件。

这一步先把接口定下来。后续 Step 7、Step 8、Step 9 再分别补实现。

## 2. 为什么先写接口，不直接实现

`Epoller`、`Channel`、`EventLoop` 之间有循环协作关系：

```text
EventLoop 持有 Epoller
Epoller 需要操作 Channel
Channel 需要通知 EventLoop 更新关注事件
```

如果一上来就把所有实现写完，很容易出现两个问题：

- 头文件互相 include，导致循环依赖。
- 类的职责混在一起，后续接入 `Acceptor` 和 `Session` 时很难改。

所以 Step 6 只做“定边界”：

- `Epoller` 只声明 epoll 操作接口。
- `Channel` 只声明 fd 事件和回调接口。
- `EventLoop` 只声明事件循环接口。

这也是面试时可以讲的工程思路：先稳定模块边界，再逐步填充实现。

## 3. 本步骤新增文件

新增文件：

```text
server/net/Epoller.hpp
server/net/Channel.hpp
server/net/EventLoop.hpp
tests/test_reactor_interfaces.cpp
tutorials/step06_reactor_interfaces.md
```

修改文件：

```text
tests/CMakeLists.txt
tests/test_main.cpp
docs/architecture.md
docs/interview_notes.md
tutorials/README.md
task_plan.md
findings.md
progress.md
```

## 4. Epoller.hpp 讲解

文件：

```text
server/net/Epoller.hpp
```

`Epoller` 是 Linux `epoll` 的 C++ 封装层。

它未来会负责：

- 创建 `epoll` fd。
- 把普通 fd 加入 epoll。
- 修改 fd 关注事件。
- 从 epoll 删除 fd。
- 调用 `epoll_wait()` 等待活跃事件。

它不负责：

- 不处理业务消息。
- 不解析协议。
- 不保存用户登录状态。
- 不决定 fd 可读后该怎么读。

### ActiveEvent

```cpp
struct ActiveEvent {
    Channel* channel = nullptr;
    std::uint32_t events = 0;
};
```

作用：

- 表示一次 `epoll_wait()` 返回的活跃事件。
- `channel` 指向发生事件的 `Channel`。
- `events` 保存内核返回的事件标志，例如 `EPOLLIN`、`EPOLLOUT`。

为什么不只返回 fd：

- fd 本身只能说明“哪个文件描述符有事件”。
- `Channel` 里面保存了这个 fd 对应的回调函数。
- 返回 `Channel*` 后，`EventLoop` 可以把事件交给对应 `Channel` 分发。

边界：

- `ActiveEvent` 不拥有 `Channel`。
- 它只是一次 poll 结果里的临时状态。

### Epoller()

```cpp
Epoller();
```

作用：

- 构造 `Epoller` 对象。
- 后续 Step 7 会在这里调用 `epoll_create1()`。

当前 Step 6 只声明，不实现。

### ~Epoller()

```cpp
~Epoller();
```

作用：

- 析构 `Epoller`。
- 后续 Step 7 会在这里关闭 `epoll_fd_`。

为什么需要析构函数：

- `epoll_create1()` 返回的是系统 fd。
- fd 是操作系统资源，必须在对象销毁时释放。
- 这体现 RAII 思路：资源获取和释放绑定到对象生命周期。

### 禁止拷贝

```cpp
Epoller(const Epoller&) = delete;
Epoller& operator=(const Epoller&) = delete;
```

作用：

- 禁止复制 `Epoller`。

原因：

- `Epoller` 未来会拥有一个真实 `epoll_fd_`。
- 如果允许复制，两个对象可能持有同一个 fd。
- 两个对象析构时都去 close 同一个 fd，会造成资源管理错误。

### poll()

```cpp
std::vector<ActiveEvent> poll(int timeout_ms);
```

作用：

- 等待 epoll 事件。
- 返回本轮活跃事件列表。

输入：

- `timeout_ms`：`epoll_wait()` 的超时时间，单位毫秒。

输出：

- `std::vector<ActiveEvent>`：本轮发生事件的 `Channel` 和事件标志。

后续实现思路：

- 调用 `epoll_wait(epoll_fd_, ...)`。
- 把每个 `epoll_event` 里的 `data.ptr` 转回 `Channel*`。
- 把返回的 `events` 保存进 `ActiveEvent`。

### updateChannel()

```cpp
void updateChannel(Channel* channel);
```

作用：

- 新增或修改某个 `Channel` 在 epoll 中关注的事件。

输入：

- `channel`：要注册或修改的 fd 事件代理。

后续实现思路：

- 如果这个 fd 还没注册过，就调用 `epoll_ctl(EPOLL_CTL_ADD)`。
- 如果已经注册过，就调用 `epoll_ctl(EPOLL_CTL_MOD)`。

边界：

- `Epoller` 不拥有 `channel`。
- `channel` 的生命周期由更上层对象管理，后续通常是 `Acceptor` 或 `Session`。

### removeChannel()

```cpp
void removeChannel(Channel* channel);
```

作用：

- 从 epoll 中移除某个 `Channel` 对应的 fd。

后续使用场景：

- 客户端断开连接。
- `Session` 准备关闭 fd。
- `Acceptor` 或其他事件源销毁前取消监听。

## 5. Channel.hpp 讲解

文件：

```text
server/net/Channel.hpp
```

`Channel` 是一个 fd 的事件代理。

它未来会负责：

- 保存 fd。
- 保存当前关注的事件 `events_`。
- 保存本轮实际发生的事件 `revents_`。
- 保存读、写、关闭、错误回调。
- 根据 `revents_` 调用对应回调。

它不负责：

- 不拥有 fd。
- 不关闭 fd。
- 不直接调用 `epoll_ctl()`。
- 不处理业务协议。

### EventCallback

```cpp
using EventCallback = std::function<void()>;
```

作用：

- 定义事件回调类型。
- 读、写、关闭、错误事件都可以用这个类型保存回调。

为什么用 `std::function<void()>`：

- 可以保存普通函数。
- 可以保存 lambda。
- 可以保存绑定了对象的成员函数。

后续 `Session` 可以把自己的 `handleRead()`、`handleWrite()` 等方法绑定给 `Channel`。

### 事件常量

```cpp
static constexpr std::uint32_t kNoneEvent = 0;
static constexpr std::uint32_t kReadEvent = EPOLLIN | EPOLLPRI;
static constexpr std::uint32_t kWriteEvent = EPOLLOUT;
```

作用：

- `kNoneEvent` 表示当前不关注任何事件。
- `kReadEvent` 表示关注可读事件和紧急数据事件。
- `kWriteEvent` 表示关注可写事件。

为什么封装成常量：

- 避免业务代码到处直接写 `EPOLLIN | EPOLLPRI`。
- 后续如果需要调整读事件关注范围，只改 `Channel` 即可。

### Channel()

```cpp
Channel(EventLoop* loop, int fd);
```

作用：

- 创建一个 fd 对应的 `Channel`。

输入：

- `loop`：所属事件循环。
- `fd`：被代理的文件描述符。

边界：

- `Channel` 不拥有 `fd`。
- `fd` 的关闭由 `Session`、`Acceptor` 或其他拥有者负责。

### ~Channel()

```cpp
~Channel();
```

作用：

- 析构 `Channel`。

注意：

- 后续实现里，析构函数不应该随便 close fd。
- 因为 `Channel` 只是事件代理，不是 fd owner。

### 禁止拷贝

```cpp
Channel(const Channel&) = delete;
Channel& operator=(const Channel&) = delete;
```

作用：

- 禁止复制 `Channel`。

原因：

- 一个 fd 的事件代理应该只有一个明确对象。
- 如果复制 `Channel`，两个对象可能都认为自己能更新同一个 fd 的 epoll 事件。
- 这会让事件状态和回调归属变得混乱。

### fd()

```cpp
int fd() const;
```

作用：

- 返回当前 `Channel` 绑定的 fd。

后续使用：

- `Epoller::updateChannel()` 会通过它拿到 fd，然后调用 `epoll_ctl()`。

### events()

```cpp
std::uint32_t events() const;
```

作用：

- 返回当前希望 epoll 关注的事件集合。

例子：

- 调用 `enableReading()` 后，`events()` 应该包含 `EPOLLIN`。
- 调用 `enableWriting()` 后，`events()` 应该包含 `EPOLLOUT`。

### revents()

```cpp
std::uint32_t revents() const;
```

作用：

- 返回本轮 epoll 实际发生的事件集合。

区别：

- `events_` 是“我想关注什么”。
- `revents_` 是“这次实际发生了什么”。

这个区别非常适合面试展开。

### setRevents()

```cpp
void setRevents(std::uint32_t revents);
```

作用：

- 由事件循环或 epoll 封装层设置本轮实际发生的事件。

后续流程大概是：

```text
Epoller::poll()
  -> 返回 ActiveEvent
EventLoop::loop()
  -> channel->setRevents(active.events)
  -> channel->handleEvent()
```

### isNoneEvent()

```cpp
bool isNoneEvent() const;
```

作用：

- 判断当前是否没有关注任何事件。

后续场景：

- 如果一个 `Channel` 不再关注任何事件，`EventLoop` 可以决定是否从 epoll 中删除它。

### isWriting()

```cpp
bool isWriting() const;
```

作用：

- 判断当前是否正在关注可写事件。

为什么需要：

- 非阻塞写经常会遇到“没写完”的情况。
- 只有输出缓冲区里还有数据时，才需要关注 `EPOLLOUT`。
- 写完后应取消可写关注，避免 epoll 一直通知可写造成空转。

### enableReading()

```cpp
void enableReading();
```

作用：

- 让当前 `Channel` 关注读事件。

后续使用：

- `Acceptor` 会对监听 fd 调用它。
- `Session` 会对客户端连接 fd 调用它。

副作用：

- 修改 `events_`。
- 调用私有 `update()` 通知 `EventLoop` 更新 epoll。

### enableWriting()

```cpp
void enableWriting();
```

作用：

- 让当前 `Channel` 关注写事件。

后续使用：

- `Session::sendPacket()` 遇到数据需要异步写出时，会调用它。

### disableWriting()

```cpp
void disableWriting();
```

作用：

- 取消关注写事件。

后续使用：

- 输出缓冲区数据全部写完后，取消 `EPOLLOUT`。

为什么重要：

- 大部分 TCP socket 在正常情况下经常是可写的。
- 如果一直关注 `EPOLLOUT`，事件循环会频繁被唤醒，浪费 CPU。

### disableAll()

```cpp
void disableAll();
```

作用：

- 取消所有关注事件。

后续使用：

- 连接关闭前。
- 从 epoll 中移除前。

### setReadCallback()

```cpp
void setReadCallback(EventCallback callback);
```

作用：

- 设置读事件回调。

后续使用：

- `Acceptor` 的读回调是 accept 新连接。
- `Session` 的读回调是读取客户端数据并交给 `FrameDecoder`。

### setWriteCallback()

```cpp
void setWriteCallback(EventCallback callback);
```

作用：

- 设置写事件回调。

后续使用：

- `Session` 的写回调会把输出缓冲区里的数据写到 socket。

### setCloseCallback()

```cpp
void setCloseCallback(EventCallback callback);
```

作用：

- 设置关闭事件回调。

后续使用：

- 客户端关闭连接时，通知 `Session` 做清理。

### setErrorCallback()

```cpp
void setErrorCallback(EventCallback callback);
```

作用：

- 设置错误事件回调。

后续使用：

- fd 出错时读取 socket error，并关闭连接或记录日志。

### handleEvent()

```cpp
void handleEvent();
```

作用：

- 根据 `revents_` 判断本轮发生了哪些事件。
- 调用对应的 read/write/close/error callback。

后续实现思路：

- 如果发生 `EPOLLERR`，调用错误回调。
- 如果发生关闭相关事件，调用关闭回调。
- 如果发生读事件，调用读回调。
- 如果发生写事件，调用写回调。

### update()

```cpp
void update();
```

作用：

- 私有辅助函数。
- 当 `events_` 改变后，通过 `EventLoop` 更新 epoll 关注事件。

为什么是私有：

- 外部模块不应该直接控制 `Channel` 怎么更新 epoll。
- 外部只需要调用 `enableReading()`、`enableWriting()`、`disableWriting()` 这些语义更清楚的接口。

## 6. EventLoop.hpp 讲解

文件：

```text
server/net/EventLoop.hpp
```

`EventLoop` 是 Reactor 的事件循环入口。

它未来会负责：

- 持有 `Epoller`。
- 在 `loop()` 中调用 `Epoller::poll()`。
- 遍历活跃事件。
- 调用 `Channel::handleEvent()`。
- 支持 `quit()` 退出事件循环。

### EventLoop()

```cpp
EventLoop();
```

作用：

- 创建事件循环对象。
- 后续 Step 8 会在构造函数里创建 `Epoller`。

### ~EventLoop()

```cpp
~EventLoop();
```

作用：

- 销毁事件循环。
- 释放它拥有的 `Epoller`。

为什么这里可以用 `std::unique_ptr<Epoller>`：

- `EventLoop` 拥有唯一的 `Epoller`。
- 不需要多个 `EventLoop` 共享同一个 `Epoller`。
- `unique_ptr` 表达了明确的所有权。

### 禁止拷贝

```cpp
EventLoop(const EventLoop&) = delete;
EventLoop& operator=(const EventLoop&) = delete;
```

作用：

- 禁止复制事件循环。

原因：

- 一个事件循环拥有自己的 epoll 实例和 fd 事件集合。
- 复制事件循环会导致所有权和事件注册关系混乱。

### loop()

```cpp
void loop();
```

作用：

- 启动事件循环。

后续实现流程：

```text
while (!quit_) {
    activeEvents = epoller_->poll(timeout);
    for each active event:
        channel->setRevents(...)
        channel->handleEvent()
}
```

### quit()

```cpp
void quit();
```

作用：

- 请求退出事件循环。

后续使用：

- Step 12 会接入 `signalfd`，收到 `SIGINT` 或 `SIGTERM` 后调用 `quit()`。

### updateChannel()

```cpp
void updateChannel(Channel* channel);
```

作用：

- 让 `EventLoop` 更新某个 `Channel` 在 epoll 中关注的事件。

为什么不让 `Channel` 直接调用 `Epoller`：

- `Channel` 不拥有 `Epoller`。
- `EventLoop` 是网络事件的调度中心。
- 通过 `EventLoop` 更新，依赖方向更清楚。

### removeChannel()

```cpp
void removeChannel(Channel* channel);
```

作用：

- 从事件循环中移除某个 `Channel`。

后续使用：

- `Session` 关闭连接时调用。
- `Acceptor` 关闭监听 fd 时调用。

## 7. 本步骤测试讲解

新增测试文件：

```text
tests/test_reactor_interfaces.cpp
```

这一步没有 `.cpp` 实现，所以测试重点不是“epoll 是否真的运行”，而是验证接口边界是否正确。

测试分三类。

第一类是编译期检查：

- `Epoller`、`Channel`、`EventLoop` 必须是类。
- 三个类都不能被拷贝。
- `Channel` 必须能用 `EventLoop*` 和 fd 构造。
- 关键函数的返回类型符合预期。

这些检查使用 `static_assert` 完成。如果接口写错，编译阶段就会失败。

第二类是事件常量检查：

- `Channel::kNoneEvent` 必须是 0。
- `Channel::kReadEvent` 必须包含 `EPOLLIN`。
- `Channel::kReadEvent` 必须包含 `EPOLLPRI`。
- `Channel::kWriteEvent` 必须等于 `EPOLLOUT`。

这些测试保证后续 `enableReading()`、`enableWriting()` 有明确语义。

第三类是轻量运行期检查：

- `Epoller::ActiveEvent` 能保存 `Channel*` 和事件 flags。
- `Channel::EventCallback` 能保存并执行 lambda。

注意：测试没有构造 `Epoller`、`Channel`、`EventLoop` 对象。

原因是 Step 6 只声明接口，没有实现构造函数、析构函数和成员函数。如果测试里真的构造对象，就会在链接阶段报 undefined reference。当前测试只验证“头文件和接口设计”，不提前要求实现。

## 8. 如何编译和测试

在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
```

你会看到 `reactor interface ...` 相关测试通过。

测试通过说明：

- 三个 Reactor 头文件可以被同一个翻译单元同时包含。
- 前向声明没有造成 include 循环。
- 不可拷贝约束生效。
- 事件常量符合 epoll 语义。
- 回调类型能保存普通 callable。

它不说明：

- `epoll_create1()` 已经可用。
- `epoll_ctl()` 已经实现。
- `epoll_wait()` 已经能返回事件。
- `Channel::handleEvent()` 已经会分发回调。

这些是后续 Step 7、Step 8、Step 9 的验收内容。

## 9. 面试时怎么讲

这一 Step 可以这样讲：

> 我在实现 Reactor 之前，先把网络层核心接口拆成了 `EventLoop`、`Epoller`、`Channel`。`Epoller` 只封装 Linux epoll 系统调用，`Channel` 表示一个 fd 的事件和回调，`EventLoop` 负责事件循环和分发。这样做的好处是职责清楚：系统调用、fd 事件状态、事件调度不会混在一个类里。后续 `Acceptor`、`Session`、`timerfd`、`signalfd` 都可以抽象成 `Channel` 接入同一个 `EventLoop`。

你还可以补充：

> 这一版先只定义接口，不实现复杂逻辑，是为了先解决依赖关系。`EventLoop` 持有 `Epoller`，`Epoller` 操作 `Channel`，`Channel` 又通过 `EventLoop` 更新事件。通过前向声明和清晰接口，可以避免循环 include，也方便后续每一步单独实现、编译和测试。

如果面试官问为什么要分三层：

- `Epoller` 是系统调用层。
- `Channel` 是 fd 事件抽象层。
- `EventLoop` 是调度层。

如果全部放在一个类里，短期能跑，但后续接入 `Acceptor`、`Session`、心跳、优雅关闭时会变得难维护。

如果面试官问为什么 `Channel` 不拥有 fd：

- 因为 fd 的生命周期应该由真正拥有连接的对象管理。
- 监听 fd 后续由 `Acceptor` 管。
- 客户端 fd 后续由 `Session` 管。
- `Channel` 只负责“这个 fd 发生事件后调用什么回调”。

如果面试官问 `events` 和 `revents` 的区别：

- `events` 是用户希望关注的事件。
- `revents` 是 epoll 本轮实际返回的事件。
- 例如一个连接平时关注 `EPOLLIN`，当输出缓冲区有数据时临时加上 `EPOLLOUT`，这属于 `events`。
- `epoll_wait()` 返回这次真的可读或可写，这属于 `revents`。

如果面试官问这一 Step 为什么也要测试：

- 虽然没有实现 epoll，但接口本身也可能出错。
- 比如头文件循环依赖、类可拷贝、事件常量写错、返回类型设计不合理。
- 用编译期测试先锁住接口，后续实现时可以减少返工。

## 10. 面试常见追问

### Q1：什么是 Reactor？

Reactor 是一种事件驱动网络模型。程序用一个事件循环等待多个 fd 的 I/O 事件，事件发生后再分发给对应处理函数。LiteIM 后续会用 `epoll` 作为事件通知机制，用 `Channel` 保存回调，用 `EventLoop` 驱动整体循环。

### Q2：为什么不直接在 `TcpServer` 里写 epoll？

直接写也能跑，但职责会混乱。`TcpServer` 应该负责管理 server 生命周期和 sessions；`Epoller` 负责系统调用；`Channel` 负责 fd 事件；`EventLoop` 负责调度。拆开后更容易测试、复用和讲清楚。

### Q3：`Channel` 和 fd 是什么关系？

一个 `Channel` 绑定一个 fd，但不拥有 fd。它保存这个 fd 关注哪些事件，以及事件发生后调用哪些回调。fd 的关闭由 `Session` 或 `Acceptor` 负责。

### Q4：为什么 `Epoller` 要禁止拷贝？

因为它未来会拥有 `epoll_fd_`。如果允许拷贝，两个对象可能管理同一个 epoll fd，析构时重复 close，或者一个对象修改事件集合影响另一个对象。

### Q5：为什么 `EventLoop` 持有 `std::unique_ptr<Epoller>`？

因为一个事件循环应该独占自己的 epoll 实例。`unique_ptr` 明确表达唯一所有权，也符合 RAII。

### Q6：为什么读事件包含 `EPOLLPRI`？

`EPOLLPRI` 表示有紧急数据等高优先级数据可读。第一版主要关注普通 `EPOLLIN`，但把 `EPOLLPRI` 放进读事件常量里，是常见的事件封装方式。

### Q7：为什么 Step 6 的测试不构造 `Epoller`？

因为 Step 6 只有头文件声明，没有 `.cpp` 定义。如果构造对象，链接器会找不到构造函数和析构函数实现。当前测试只验证接口设计；真正运行逻辑在后续 Step 测试。

## 11. 本步骤提交信息

```text
feat(net): define reactor core interfaces
```
