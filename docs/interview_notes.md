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

## 项目目录结构

面试时可以这样说：

> 我把项目目录调整成了更常见的 C++ 工程结构：`include/liteim/...` 放头文件，`src/...` 放库实现，`server/main.cpp` 只作为服务端入口。这样测试、服务端和未来 Qt 客户端都通过统一的 `liteim/...` include 路径依赖模块接口，接口和实现的边界更清晰。

为什么 `.hpp` 和 `.cpp` 分开：

- `.hpp` 表达模块对外接口或跨 target 可见的类型。
- `.cpp` 表达实现细节，只参与对应库目标编译。
- 分开后 CMake 可以把 `include/` 作为公共 include 目录暴露给依赖方。

为什么 include 路径带 `liteim/` 前缀：

- 避免和系统库或第三方库的 `net/`、`protocol/` 等通用目录名冲突。
- 让 include 语义更明确，例如 `liteim/net/Buffer.hpp` 一看就是本项目网络层。
- 未来项目拆库或安装时更接近真实工程习惯。

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

## Reactor 核心接口

面试时可以这样说：

> 我没有把 `epoll_wait()`、fd 状态和业务回调都堆在一个 server 类里，而是先拆成 `EventLoop`、`Epoller`、`Channel` 三层。`Epoller` 只负责封装 Linux `epoll`，`Channel` 负责描述一个 fd 关注什么事件以及事件发生后执行什么回调，`EventLoop` 负责循环等待事件并分发给对应的 `Channel`。这样网络层职责更清楚，后续实现 `Acceptor` 和 `Session` 时也能复用同一个事件分发机制。

为什么 Step 6 只定义接口：

- Reactor 这几个类之间天然有依赖关系。
- `EventLoop` 持有 `Epoller`。
- `Epoller` 需要操作 `Channel`。
- `Channel` 又需要通过 `EventLoop` 更新 epoll 关注事件。
- 先定义接口能把类之间的依赖方向固定下来，避免后续边写边改导致循环 include 或职责混乱。

为什么要有 `Channel`：

- epoll 返回的是 fd 上发生了什么事件。
- C++ 服务端真正需要的是“这个 fd 可读时调用哪个函数、可写时调用哪个函数、关闭时怎么清理”。
- `Channel` 就是 fd 到回调的桥梁。

为什么 `Channel` 不拥有 fd：

- fd 生命周期后续由 `Acceptor` 或 `Session` 管理。
- `Channel` 只是事件代理，不负责关闭连接。
- 这样能避免多个对象同时认为自己拥有同一个 fd，减少重复 close 的风险。

为什么 `Epoller` 不直接处理业务：

- `Epoller` 是系统调用封装层，只知道 fd、events、`epoll_ctl()`、`epoll_wait()`。
- 登录、私聊、群聊这些业务应该在 `MessageRouter` / service 层处理。
- 网络事件和业务语义分开，代码更容易测试和替换。

常见追问：

- `EventLoop`、`Epoller`、`Channel` 分别负责什么？
- 为什么不用一个类直接写完所有 epoll 逻辑？
- `Channel` 为什么只保存 fd 和回调，而不保存用户信息？
- 为什么第一版用 LT 模式，不一开始用 ET？
- `events` 和 `revents` 有什么区别？
- `updateChannel()` 应该由谁调用，为什么不是业务层直接调用 `epoll_ctl()`？

## Epoller epoll 封装

面试时可以这样说：

> 我把 Linux `epoll` 封装成了 `Epoller`。它只负责 `epoll_create1()`、`epoll_ctl()` 和 `epoll_wait()`，不处理业务消息，也不拥有普通 socket fd。每个 fd 的事件状态由 `Channel` 表示，注册 epoll 时把 `Channel*` 放进 `epoll_event.data.ptr`，这样事件返回后可以直接交给对应 `Channel`。第一版使用 LT 模式，先保证读写循环和连接生命周期正确，再考虑 ET 优化。

为什么 `Epoller` 用 RAII：

- `epoll_create1()` 返回的是操作系统 fd。
- fd 必须关闭，否则会造成资源泄漏。
- 把 `epoll_fd_` 绑定到 `Epoller` 对象生命周期后，构造成功就拥有资源，析构时自动释放。

为什么 `Epoller` 不关闭普通 socket fd：

- `Epoller` 只拥有 epoll fd。
- 客户端连接 fd 以后由 `Session` 管理。
- listen fd 以后由 `Acceptor` 管理。
- 明确所有权可以避免重复 close 或提前关闭。

为什么 `event.data.ptr` 保存 `Channel*`：

- epoll 本身只关心 fd 和事件位。
- C++ 事件分发需要找到 fd 对应的回调。
- `Channel` 正好保存 fd、关注事件、实际事件和回调。
- 保存 `Channel*` 后，`EventLoop` 不需要额外维护 fd 到 Channel 的查找表。

为什么第一版用 LT 模式：

- LT 是状态触发，只要 fd 仍然可读或可写，就会继续报告。
- 教学项目先保证行为容易理解、测试稳定。
- ET 要求每次读写都循环到 `EAGAIN`，否则可能漏事件。
- 等 `Session` 的非阻塞读写和输出缓冲区稳定后，再讨论 ET 更合适。

常见追问：

- `epoll_create1(EPOLL_CLOEXEC)` 的作用是什么？
- `EPOLL_CTL_ADD` 和 `EPOLL_CTL_MOD` 怎么区分？
- 为什么要有 `registered_fds_`？
- `EINTR` 发生时 `poll()` 应该怎么办？
- `Epoller` 返回 `Channel*` 会不会有悬空指针风险？怎么避免？
