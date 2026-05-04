# LiteIM 面试讲解笔记

本文档用于沉淀 LiteIM 的面试表达。每完成一个核心模块后，都要把“为什么这样设计”和“能怎么讲”补充到这里。

后续重点补充：

- `epoll` 的基本使用方式。
- LT / ET 模式区别，以及为什么第一版先用 LT。
- TCP 是字节流，为什么会有粘包和半包。
- 定长 Header + JSON Body 如何解决消息边界问题。
- `Session` 生命周期：创建、读、写、关闭、清理。
- `TcpServer` 如何组合 `Acceptor` 和 `Session` 管理连接表。
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

## EventLoop 事件循环

面试时可以这样说：

> 我把 Reactor 的调度层封装成了 `EventLoop`。它内部持有 `Epoller`，在 `loop()` 中不断调用 `epoll_wait()`，拿到活跃 `Channel` 后调用 `Channel::handleEvent()`。`EventLoop` 不直接读写 socket，也不解析业务协议，它只负责事件循环主流程。后续 `Acceptor`、`Session`、`timerfd`、`signalfd` 都可以作为 Channel 注册进同一个 EventLoop。

为什么 `EventLoop` 不直接处理业务：

- `EventLoop` 只关心 fd 事件调度。
- 业务消息属于 `MessageRouter` / service 层。
- TCP 连接读写属于 `Session`。
- 这样网络调度、连接生命周期和业务逻辑不会混在一起。

为什么 `quit()` 只设置标记：

- 事件循环应该在当前回调执行完后有序退出。
- 直接中断当前回调会让资源状态难以保证。
- 退出标记由 `loop()` 在下一轮 while 条件检查时观察。

为什么 `quit_` 用 `std::atomic_bool`：

- 测试和未来关闭路径可能从回调或其他线程调用 `quit()`。
- 普通 bool 跨线程读写会有数据竞争。
- atomic stop flag 是一个低成本的安全边界。

当前没有 wakeup fd 的限制：

- `quit()` 不会主动唤醒阻塞中的 `epoll_wait()`。
- 如果没有新事件，循环会等到 poll 超时后退出。
- 后续可以加 `eventfd` 作为跨线程唤醒机制。
- Step 12 会用 `signalfd` 把 Ctrl+C / SIGTERM 也纳入 epoll。

常见追问：

- `EventLoop` 为什么通常和线程绑定？
- `quit()` 会不会打断正在执行的回调？
- 为什么 EventLoop 不拥有 Channel？
- 如果 Channel 被销毁前没有 remove，会发生什么？
- 没有 wakeup fd 时，跨线程停止 EventLoop 有什么延迟？
- Step 8 和 Step 9 的边界是什么？

## Channel 事件代理

面试时可以这样说：

> 我把每个 fd 的事件状态和回调封装成 `Channel`。`events_` 表示当前希望 epoll 关注的事件，`revents_` 表示本轮 epoll 实际返回的事件。业务对象后续只需要给 Channel 设置 read/write/close/error 回调，并调用 `enableReading()`、`enableWriting()` 这类语义接口；Channel 内部会通过所属 `EventLoop` 更新 epoll 关注事件。

为什么 `Channel` 要通过 `EventLoop` 更新 epoll：

- `Channel` 不拥有 `Epoller`，也不应该直接知道底层系统调用对象。
- `EventLoop` 是 Reactor 的调度入口，统一管理注册、修改和移除。
- 上层模块只表达“我要关注读/写”，不用直接调用 `epoll_ctl()`。

为什么 `events_` 和 `revents_` 要分开：

- `events_` 是用户态期望，表示“我想监听什么”。
- `revents_` 是内核返回结果，表示“这一次实际发生了什么”。
- 两者分开后，注册兴趣和事件分发不会混在一起。

为什么 `disableAll()` 走 remove：

- 当一个 fd 没有任何关注事件时，继续留在 epoll 里没有意义。
- 从 epoll 移除可以避免后续继续分发旧事件。
- 后续如果再次 `enableReading()`，`Epoller` 会重新 `ADD` 这个 fd。

常见追问：

- `enableReading()` 里面为什么不直接调用 `epoll_ctl()`？
- `Channel` 为什么不负责关闭 fd？
- `Channel` 销毁前忘记 remove 会有什么风险？
- `EPOLLHUP` 和 `EPOLLERR` 分别怎么处理？
- 为什么低层 `Epoller` 测试里可以构造没有 `EventLoop` 的 `Channel`？

## Acceptor 非阻塞监听器

面试时可以这样说：

> 我把服务端监听 socket 封装成 `Acceptor`。它负责创建非阻塞 listen fd，设置端口复用选项，完成 `bind()` / `listen()`，然后把 listen fd 注册成一个 `Channel`。当 listen fd 可读时，说明有新连接到来，`Acceptor` 会循环 `accept4()` 直到 `EAGAIN`，并通过 callback 把 accepted fd 交给上层。

为什么 listen fd 也用 `Channel`：

- listen socket 和普通连接 socket 都是 fd。
- 新连接到来时，listen fd 会变成可读。
- 把它纳入 `EventLoop` 后，监听、连接读写、timerfd、signalfd 后续都能统一调度。

为什么 accept 要循环到 `EAGAIN`：

- 一个可读事件可能对应多个排队连接。
- 如果只 accept 一个，剩余连接还留在内核队列里。
- 循环到 `EAGAIN` 表示本轮已经把可接受连接取干净。

为什么用 `accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)`：

- accepted fd 必须是非阻塞，后续 `Session` 才能配合 epoll 工作。
- `SOCK_CLOEXEC` 避免未来 `exec()` 子进程继承连接 fd。
- 一次系统调用完成 accept 和 fd 属性设置，减少中间状态。

为什么 `Acceptor` 不创建 `Session`：

- `Acceptor` 只负责“接新连接”。
- `Session` 负责“管理单个连接的读写和生命周期”。
- `TcpServer` 后续负责组合两者，维护 session 容器。

常见追问：

- `bind()`、`listen()`、`accept()` 分别做什么？
- 为什么 listen fd 可读表示有新连接？
- 为什么 accepted fd 也必须非阻塞？
- `SO_REUSEADDR` 和 `SO_REUSEPORT` 的作用是什么？
- callback 收到 fd 后，所有权应该由谁负责？

## Session 单连接生命周期

面试时可以这样说：

> 我把每个已连接客户端 fd 封装成 `Session`。它拥有这个 fd，并持有一个 `Channel` 接入 `EventLoop`。读方向上，`Session` 循环 `read()` 到 `EAGAIN`，把字节交给 `FrameDecoder`，解析出完整 `Packet` 后通过 message callback 通知上层。写方向上，`sendPacket()` 先把 Packet 编码进输出缓冲区，再开启写事件；`handleWrite()` 负责把缓冲区里的数据尽量写出，写空后关闭写关注。

为什么 `Session` 要有输出缓冲区：

- 非阻塞 fd 不能保证一次 `write()` 写完所有数据。
- 未写完的数据必须保存下来。
- 等下一次 fd 可写时继续写，写空后再关闭 `EPOLLOUT`。

为什么读要循环到 `EAGAIN`：

- 使用 LT 模式时，fd 可读表示当前内核缓冲区里有数据。
- 一次回调里尽量读干净，可以减少重复事件唤醒。
- 读到 `EAGAIN` 表示当前没有更多数据可读。

为什么 `Session` 不直接处理业务：

- `Session` 是连接生命周期和网络读写层。
- 登录、私聊、心跳属于业务层或 `MessageRouter`。
- 网络层只产出 `Packet`，上层根据 `msg_type` 分发。

为什么 `start()` 单独存在：

- 构造 `Session` 后，`TcpServer` 需要先设置 message/close callback。
- callback 配好后再 `start()` 注册读事件。
- 这样连接一旦有数据到来，处理路径已经完整。

常见追问：

- `Session`、`Acceptor`、`TcpServer` 怎么分工？
- 为什么 `sendPacket()` 不直接阻塞写完？
- `FrameDecoder` 出错时为什么关闭连接？
- close callback 收到 fd 后应该做什么？
- 为什么输出缓冲区写空后要 `disableWriting()`？

## TcpServer 服务端主体

面试时可以这样说：

> 我把服务端主体封装成 `TcpServer`。它不直接读写 socket，也不解析业务消息，而是组合 `Acceptor` 和 `Session`：`Acceptor` 负责接收新连接，`TcpServer` 在新连接 callback 中创建 `Session` 并放入 session map，`Session` 负责单连接读写。连接关闭时，`TcpServer` 根据 fd 从 map 中移除对应 Session。这样监听、单连接 I/O 和连接集合管理分开，后续 `MessageRouter` 只需要处理业务消息。

为什么需要 `TcpServer`：

- `Acceptor` 只负责 listen fd，不应该维护在线连接表。
- `Session` 只负责一个 connected fd，不知道其他连接。
- `TcpServer` 负责把两者组合起来，管理所有 active sessions。

为什么用 `std::shared_ptr<Session>`：

- `EventLoop` / `Channel` 派发事件时，回调栈上可能正在执行 `Session` 成员函数。
- 关闭 callback 里如果立刻销毁当前 `Session`，会出现对象还在执行成员函数但已经被释放的生命周期问题。
- `TcpServer` 会先把关闭的 `Session` 从 active map 移到 retired 列表，等安全时再清理。

为什么 `sendToUser()` 只做基础绑定：

- Step 12 还没有登录和用户态。
- 当前只提供 `user_id -> session_fd` 的显式绑定能力。
- 真正什么时候绑定用户、什么时候解绑用户，后续由 `MessageRouter` / `AuthService` 根据登录结果决定。

为什么用 `signalfd` 做优雅关闭：

- `SIGINT` / `SIGTERM` 默认是异步信号，不适合在 signal handler 里做复杂关闭逻辑。
- `signalfd` 可以把信号变成一个普通 fd 事件。
- 这个 fd 可以像 socket 一样注册进 `EventLoop`，由 epoll 统一调度。
- 收到信号后，`TcpServer` 在正常回调上下文中关闭 sessions、关闭 listen socket，并调用 `EventLoop::quit()`。

常见追问：

- `TcpServer` 为什么不直接调用 `epoll_ctl()`？
- `Acceptor`、`Session`、`TcpServer` 各自拥有哪个 fd？
- 为什么 stop 时要先关闭 sessions，再关闭 listen socket？
- 为什么 signal fd 也可以放进 epoll？
- 当前 `sendToUser()` 和真正登录态绑定还差什么？

## MessageRouter 消息分发

面试时可以这样说：

> 我在网络层之上增加了 `MessageRouter`。`Session` 负责把 TCP 字节流解成 `Packet`，`TcpServer` 负责连接管理和 message callback，`MessageRouter` 负责根据 `Packet.header.msg_type` 做业务分发。第一版只支持 `HEARTBEAT_REQ -> HEARTBEAT_RESP`，未知消息返回 `ERROR_RESP`。router 不直接操作 fd，也不保存 Session，只通过 `Session::sendPacket()` 把响应交回网络层。

为什么要单独拆 `MessageRouter`：

- `Session` 不应该理解登录、聊天、心跳这些业务语义。
- `TcpServer` 不应该变成所有业务 switch 的集合。
- 业务分发单独成层后，后续可以自然接 `AuthService`、`ChatService`、`GroupService`。
- router 可以独立测试，不需要每个业务测试都重新搭完整服务端。

为什么 `MessageRouter` 只通过 `Session::sendPacket()` 回复：

- fd 的读写、输出缓冲区和关闭状态都由 `Session` 管理。
- 非阻塞写可能短写，必须走 `Session` 的输出缓冲。
- router 只表达“要回什么 Packet”，不关心底层什么时候可写。

为什么响应保留 `seq_id`：

- 客户端可能同时发多个请求。
- 保留请求 `seq_id` 后，客户端能把响应和请求对应起来。
- 这是二进制协议里常见的 request/response 关联方式。

为什么未知消息返回 `ERROR_RESP`：

- 静默丢弃会让客户端难以判断服务端是否收到请求。
- 明确错误响应方便客户端处理，也方便测试定位。
- 后续新业务没实现时，也能保持协议行为可预期。

常见追问：

- `MessageRouter` 和 `TcpServer` 的边界是什么？
- 为什么 router 不直接拿 fd 写？
- 什么时候应该把分发逻辑拆到具体 service？
- 心跳响应和心跳超时清理有什么区别？
- 未来登录成功后，用户和 Session 应该在哪里绑定？
