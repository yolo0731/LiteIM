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

## Step 4：Buffer 网络缓冲区

Step 4 已经实现 `server/net/Buffer`。

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

Step 5 已经实现 `server/net/SocketUtil`。

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

当前还没有实现 `.cpp`，因此不会真正创建 `epoll` fd，也不会调用 `epoll_wait()`。具体实现会放到后续步骤：

- Step 7：实现 `Epoller`。
- Step 8：实现 `EventLoop`。
- Step 9：实现 `Channel` 并打通事件分发。
