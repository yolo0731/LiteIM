# LiteIM 面试讲解笔记

本文档用于沉淀 LiteIM 的面试表达。每完成一个核心模块后，都要把“为什么这样设计”和“能怎么讲”补充到这里。

后续重点补充：

- `epoll` 的基本使用方式。
- LT / ET 模式区别，以及为什么第一版先用 LT。
- TCP 是字节流，为什么会有粘包和半包。
- 定长 Header + JSON Body 如何解决消息边界问题。
- `Session` 生命周期：创建、读、写、关闭、清理。
- 输出缓冲区如何处理短写。
- `timerfd` 如何接入心跳超时检查。
- `signalfd` 如何接入 Ctrl+C / SIGTERM 优雅关闭。
- bot 虚拟用户为什么通过同一套 TLV 协议接入，而不是把 AI 逻辑写进 C++ 服务端。

写作要求：

- 用中文讲清楚思路。
- 关键类名、函数名、协议字段保留英文。
- 每个点都尽量配一个“面试时可以这样说”的简短表述。

## Buffer 网络缓冲区

面试时可以这样说：

> 我在网络层封装了一个通用 `Buffer`，它不直接操作 socket，也不理解业务协议，只负责保存、查看和消费字节数据。输入方向上，它可以保存已经从 socket 读到但还没处理完的数据；输出方向上，它可以保存非阻塞写没有一次写完的剩余数据。这样 `Session` 只需要组合 `Buffer`、`FrameDecoder` 和 `Channel`，网络读写和协议解析的边界比较清楚。

为什么需要输出缓冲区：

- 非阻塞 socket 的 `write()` 不保证一次写完所有数据。
- 如果只写出一部分，剩余数据必须保存下来。
- 后续等 fd 可写事件再次触发时继续写。

为什么 `Buffer` 不直接调用 `read()` / `write()`：

- `Buffer` 只是数据结构。
- socket 生命周期由 `Session` 管理。
- fd 事件由 `Channel` / `EventLoop` 管理。
- 这样职责更单一，也更容易单元测试。

当前 `Buffer` 的接口：

- `append()`
- `appendString()`
- `readableBytes()`
- `peek()`
- `retrieve()`
- `retrieveAllAsString()`

## SocketUtil socket 工具函数

面试时可以这样说：

> 我把 Linux socket 常用操作封装成了 `SocketUtil`，包括创建非阻塞 socket、设置 `O_NONBLOCK`、设置端口复用、关闭 fd 和读取 socket error。这样后续 `Acceptor` 和 `Session` 不需要分散调用底层系统调用，错误处理也更统一。服务端使用 epoll 时，fd 必须配合非阻塞 I/O，否则某个连接上的 read/write 可能阻塞整个事件循环。

为什么 socket 要非阻塞：

- epoll 只负责告诉你 fd 当前可能可读或可写。
- 如果 fd 是阻塞的，`read()` / `write()` 仍可能卡住当前线程。
- 单线程 Reactor 一旦被某个 fd 卡住，其他连接都无法处理。

为什么要 `SO_REUSEADDR`：

- 服务端重启时，端口可能还处于 `TIME_WAIT` 相关状态。
- 设置 `SO_REUSEADDR` 能提升本地开发和重启体验。

为什么要 `SO_REUSEPORT`：

- 它允许多个 socket 绑定同一个 ip:port。
- 第一版不做多进程负载均衡，但提前封装接口，后续可扩展。

常见追问：

- `SO_REUSEADDR` 和 `SO_REUSEPORT` 的区别是什么？
- 为什么 `accept()` 得到的新连接 fd 也要设置非阻塞？
- `SO_ERROR` 有什么用？
- 为什么不在业务代码里直接调用 `socket()` / `fcntl()`？
