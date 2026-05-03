# LiteIM 架构说明

本文档用于记录 LiteIM 的整体架构。后续实现 Reactor、协议层、业务层、存储层和定时器模块时，会逐步补充详细说明。

规划模块：

- `net`：网络层，包含 `EventLoop`、`Epoller`、`Channel`、`Acceptor`、`Session`、`Buffer`。
- `protocol`：协议层，包含 `Packet`、`MessageType`、`FrameDecoder`。
- `service`：业务层，包含 `MessageRouter`、`AuthService`、`ChatService`、`GroupService`、`BotService`。
- `storage`：存储层，包含 `IStorage`、`SQLiteStorage`、`ICache`、`NullCache`。
- `timer`：定时器和心跳超时清理，后续会接入 `timerfd`。

文档目标：

- 说明每一层负责什么。
- 说明模块之间如何依赖。
- 说明为什么网络层、协议层、业务层和存储层要解耦。

## 当前工程目录分层

LiteIM 当前采用 `include/` 和 `src/` 分离的 C++ 项目结构：

```text
include/liteim/
  ├── net/
  └── protocol/

src/
  ├── net/
  └── protocol/

server/
  └── main.cpp
```

目录职责：

- `include/liteim/...`：头文件和模块接口。
- `src/...`：库实现文件。
- `server/`：服务端可执行程序入口。
- `tests/`：单元测试和接口测试。
- `docs/`：架构、协议、数据库、面试讲解等中文文档。
- `tutorials/`：每个 Step 的教学文档。

项目内 include 统一使用：

```cpp
#include "liteim/net/Buffer.hpp"
#include "liteim/protocol/Packet.hpp"
```

不要再使用旧的 `net/Buffer.hpp` 或 `protocol/Packet.hpp` 写法。

详细目录约定见 `docs/project_layout.md`。

## Step 4：Buffer 网络缓冲区

Step 4 已经实现 `Buffer`：

- 头文件：`include/liteim/net/Buffer.hpp`
- 实现文件：`src/net/Buffer.cpp`

`Buffer` 属于网络层基础组件，后续会给 `Session` 的输入缓冲区和输出缓冲区使用。

它的职责：

- 保存已经读到但还没处理完的数据。
- 保存准备写出但还没完全写完的数据。
- 支持追加字节。
- 支持查看当前可读数据。
- 支持消费部分数据。
- 支持一次性取出所有可读数据。

它不负责：

- 不直接调用 `read()`。
- 不直接调用 `write()`.
- 不理解 `Packet`。
- 不做协议拆包。
- 不做业务分发。

设计上，`Buffer` 使用 `std::string buffer_` 保存数据，并用 `read_index_` 记录已经消费到哪里。

这样做的原因是：如果每次 `retrieve()` 都直接 `erase(0, len)`，会频繁移动内存。当前实现只有在已消费空间达到一定阈值，并且已消费空间占比较大时，才进行压缩。

后续 `Session` 会组合：

```text
socket fd
Channel
FrameDecoder
input Buffer
output Buffer
```

其中：

- input Buffer 用于承接 socket 读入的数据。
- output Buffer 用于处理非阻塞写中的短写问题。

## Step 5：SocketUtil socket 工具函数

Step 5 已经实现 `SocketUtil`：

- 头文件：`include/liteim/net/SocketUtil.hpp`
- 实现文件：`src/net/SocketUtil.cpp`

`SocketUtil` 属于网络层底部工具模块，负责封装 Linux socket 常用系统调用。

它的职责：

- 创建非阻塞 TCP socket。
- 把已有 fd 设置为非阻塞。
- 设置 `SO_REUSEADDR`。
- 设置 `SO_REUSEPORT`。
- 关闭 fd。
- 读取 socket pending error。

它不负责：

- 不负责 `bind()`。
- 不负责 `listen()`。
- 不负责 `accept()`。
- 不负责注册 epoll。
- 不负责管理连接生命周期。

后续关系：

```text
SocketUtil
  ↓
Acceptor 使用它创建 listen socket
Session 使用它关闭连接 fd
TcpServer 组合 Acceptor 和 Session
```

这一步先把底层系统调用封装起来，后续 `Acceptor` 和 `Session` 就不用到处直接写 `socket()`、`fcntl()`、`setsockopt()`、`close()`。

## Step 6：Reactor 核心接口

Step 6 已经定义网络层 Reactor 的三个核心接口：

- `Epoller`
- `Channel`
- `EventLoop`

这一步只定义头文件，不实现具体逻辑。这样做是为了先把网络层职责边界拆清楚，避免一开始就把 `epoll_wait()`、fd 状态、回调分发和事件循环全部写在一个类里。

三者的关系：

```text
EventLoop
  ├── owns Epoller
  ├── loop() 调用 Epoller::poll()
  └── 遍历活跃事件，交给 Channel::handleEvent()

Epoller
  ├── owns epoll fd
  ├── updateChannel() / removeChannel()
  └── poll() 返回活跃事件列表

Channel
  ├── 绑定一个 fd
  ├── 保存关注事件 events
  ├── 保存实际发生事件 revents
  └── 根据 revents 调用 read/write/close/error callback
```

职责边界：

- `EventLoop` 是事件循环入口，负责“等事件”和“分发事件”的主流程。
- `Epoller` 是 `epoll` 系统调用的封装层，负责和 Linux 内核交互。
- `Channel` 是 fd 的事件代理，负责把内核事件转成 C++ 回调。

Step 6 刚完成时还没有实现 `.cpp`，因此当时不会真正创建 `epoll` fd，也不会调用 `epoll_wait()`。具体实现按下面步骤继续推进：

- Step 7：实现 `Epoller`。
- Step 8：实现 `EventLoop`。
- Step 9：实现 `Channel` 并打通事件分发。

## Step 7：Epoller epoll 封装

Step 7 已经实现 `Epoller`：

- 头文件：`include/liteim/net/Epoller.hpp`
- 实现文件：`src/net/Epoller.cpp`

`Epoller` 是 Linux `epoll` 的薄封装层。

它的职责：

- 在构造函数中调用 `epoll_create1(EPOLL_CLOEXEC)` 创建 epoll fd。
- 在析构函数中关闭自己拥有的 epoll fd。
- 用 `updateChannel()` 封装 `EPOLL_CTL_ADD` 和 `EPOLL_CTL_MOD`。
- 用 `removeChannel()` 封装 `EPOLL_CTL_DEL`。
- 用 `poll()` 封装 `epoll_wait()`，返回活跃 `Channel` 和事件位。

它不负责：

- 不拥有普通 socket fd。
- 不关闭客户端连接 fd。
- 不调用业务回调。
- 不解析协议包。
- 不管理 `Session` 生命周期。

`Epoller` 把 `Channel*` 保存到 `epoll_event.data.ptr`。这样内核返回事件后，网络层可以直接拿到对应的 `Channel`，而不是只拿到 fd 数字再额外查表。

当前实现使用 LT 模式，不设置 `EPOLLET`。原因是第一版更重视正确性和可解释性：LT 模式下，只要 fd 仍然可读，下一轮 `epoll_wait()` 还会继续报告，后续接入 `Session` 的读写循环时更容易避免漏读。

Step 7 也补充了 `Channel` 的基础状态方法：

- 保存 fd。
- 保存关注事件 `events_`。
- 保存实际返回事件 `revents_`。
- 支持打开/关闭读写关注事件。
- 支持保存事件回调。

Step 8 已经补充 `Channel::handleEvent()` 的基础回调分发；`Channel` 通过 `EventLoop` 自动更新 epoll 的逻辑还没有实现，会放到 Step 9。

## Step 8：EventLoop 事件循环骨架

Step 8 已经实现 `EventLoop`：

- 头文件：`include/liteim/net/EventLoop.hpp`
- 实现文件：`src/net/EventLoop.cpp`

`EventLoop` 是 Reactor 的调度层。

它的职责：

- 持有一个 `Epoller`。
- 在 `loop()` 中反复调用 `Epoller::poll()`。
- 遍历活跃事件。
- 调用对应 `Channel::handleEvent()`。
- 通过 `quit()` 请求退出循环。
- 通过 `updateChannel()` / `removeChannel()` 转发 epoll 注册和删除操作。

它不负责：

- 不拥有普通 socket fd。
- 不拥有 `Channel`。
- 不直接执行 `read()` 或 `write()`。
- 不解析协议。
- 不处理登录、私聊、群聊等业务。

当前 `quit_` 使用 `std::atomic_bool`。这样测试和未来跨线程关闭路径不会对普通 bool 产生数据竞争。

当前还没有 wakeup fd，因此如果其他线程调用 `quit()` 时 `epoll_wait()` 正在阻塞，循环会在下一次 fd 事件到来或 poll 超时后退出。后续可以通过 `eventfd` 或 `signalfd` 让退出事件也变成 epoll 管理的 fd 事件。

Step 8 也实现了 `Channel::handleEvent()` 的基础分发：

- `EPOLLHUP` 且没有 `EPOLLIN` 时调用关闭回调。
- `EPOLLERR` 时调用错误回调。
- `EPOLLIN` / `EPOLLPRI` / `EPOLLRDHUP` 时调用读回调。
- `EPOLLOUT` 时调用写回调。

## Step 9：Channel 事件代理和 EventLoop 联通

Step 9 已经补齐 `Channel` 和 `EventLoop` 的自动联通：

- 头文件：`include/liteim/net/Channel.hpp`
- 实现文件：`src/net/Channel.cpp`

`Channel` 是一个 fd 的事件代理。它保存两类事件状态：

- `events_`：当前希望 epoll 关注哪些事件。
- `revents_`：本轮 `epoll_wait()` 实际返回了哪些事件。

Step 9 后，调用者只需要使用语义化接口：

```cpp
channel.enableReading();
channel.enableWriting();
channel.disableWriting();
channel.disableAll();
```

这些接口会先修改 `events_`，然后通过私有 `update()` 通知所属 `EventLoop`。如果 `Channel` 还有关注事件，`update()` 调用 `EventLoop::updateChannel(this)`；如果没有任何关注事件，`update()` 调用 `EventLoop::removeChannel(this)`。

这样上层的 `Acceptor` 和 `Session` 后续不需要直接操作 `Epoller`，也不需要在每次事件状态变化后手动调用 `loop.updateChannel(&channel)`。它们只表达业务含义：需要读就 `enableReading()`，有待发送数据就 `enableWriting()`，写完了就 `disableWriting()`，关闭前就 `disableAll()` 并移除。

`Channel` 仍然不拥有 fd。fd 的关闭会放在后续 `Acceptor` 或 `Session` 中完成；销毁 `Channel` 或关闭 fd 之前，必须保证已经从 `EventLoop` / `Epoller` 中移除，避免 epoll 返回悬空指针。
