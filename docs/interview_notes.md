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
