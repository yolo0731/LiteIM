# Step 11：实现 Session

本步骤目标：实现单连接管理对象 `Session`。

前面 Step 10 的 `Acceptor` 已经可以接收新连接，并把 accepted fd 通过 callback 交给上层。但一个连接真正建立后，还需要对象负责后续读、写、解包和关闭，这就是 `Session`。

## 1. 这一步要解决什么问题

一个客户端连接在服务端不是一个裸 fd 就够了。它至少需要这些状态：

- 这个连接对应的 fd。
- 这个 fd 在 `EventLoop` 中注册的 `Channel`。
- TCP 字节流解包器 `FrameDecoder`。
- 还没有写完的输出缓冲区。
- 收到完整 `Packet` 后通知上层的 callback。
- 连接关闭后通知上层清理的 callback。

如果这些逻辑都写在 `TcpServer` 里，`TcpServer` 会同时负责监听、连接读写、协议拆包、业务分发和连接容器管理，职责太重。

所以 Step 11 把“单个连接”的生命周期独立成 `Session`。

## 2. 职责边界

`Session` 负责：

- 拥有一个 connected fd。
- 把 fd 包装成 `Channel`。
- 注册读写回调。
- 非阻塞读到 `EAGAIN`。
- 把读到的字节交给 `FrameDecoder`。
- 解析出完整 `Packet` 后调用 message callback。
- 用输出 `Buffer` 保存待发送数据。
- 非阻塞写出输出缓冲区。
- 关闭连接并通知 close callback。

`Session` 不负责：

- 不创建 listen socket。
- 不 `accept()` 新连接。
- 不维护所有在线连接。
- 不根据 `msg_type` 做业务路由。
- 不处理登录、私聊、群聊、存储。

这些会在后续 `TcpServer`、`MessageRouter` 和业务 service 中实现。

## 3. 本步骤新增文件

新增文件：

```text
include/liteim/net/Session.hpp
src/net/Session.cpp
tests/test_session.cpp
tutorials/step11_session.md
```

修改文件：

```text
src/CMakeLists.txt
tests/CMakeLists.txt
tests/test_main.cpp
README.md
docs/architecture.md
docs/interview_notes.md
docs/project_layout.md
tutorials/README.md
task_plan.md
findings.md
progress.md
```

## 4. Session.hpp 讲解

文件：

```text
include/liteim/net/Session.hpp
```

### MessageCallback

```cpp
using MessageCallback = std::function<void(Session&, const protocol::Packet&)>;
```

作用：

- 定义收到完整 `Packet` 后的回调类型。

输入：

- `Session&`：当前连接对象。
- `const Packet&`：刚解析出的完整协议包。

使用场景：

- 后续 `TcpServer` 会把这个 callback 接到 `MessageRouter`。
- `MessageRouter` 根据 `Packet.header.msg_type` 分发业务。

### CloseCallback

```cpp
using CloseCallback = std::function<void(int)>;
```

作用：

- 定义连接关闭后的回调类型。

输入：

- 已关闭的 fd 值。

为什么只传 fd：

- 后续 `TcpServer` 可以用 fd 从 session map 里删除对应连接。
- Step 11 暂时不引入 `shared_ptr<Session>` 或复杂所有权模型。

### Session()

```cpp
Session(EventLoop* loop, int fd);
```

作用：

- 创建一个连接对象。
- 接管 fd 所有权。
- 把 fd 设置为 nonblocking。
- 创建对应 `Channel`。
- 给 `Channel` 设置 read/write/close/error 回调。

输入：

- `loop`：所属事件循环，不能为 `nullptr`。
- `fd`：已连接客户端 fd，必须有效。

失败场景：

- `loop == nullptr` 抛出 `std::invalid_argument`。
- `fd < 0` 抛出 `std::invalid_argument`。
- 设置 nonblocking 失败时关闭 fd 并抛出异常。

### ~Session()

```cpp
~Session();
```

作用：

- 清理连接资源。
- 如果连接还没关闭，析构时会从 `EventLoop` 移除 `Channel` 并关闭 fd。

边界：

- 析构清理不会调用 close callback。
- 显式 `close()` 才会通知上层。

### start()

```cpp
void start();
```

作用：

- 注册读事件。
- 让这个连接正式进入事件循环。

为什么需要单独 `start()`：

- 构造 `Session` 后，上层通常还要设置 message callback 和 close callback。
- callback 设置好后再 `start()`，可以避免数据事件先到但回调还没准备好。

边界：

- 多次调用是幂等的。
- 已关闭后调用会抛出 `std::runtime_error`。

### sendPacket()

```cpp
void sendPacket(const protocol::Packet& packet);
```

作用：

- 把 `Packet` 编码成二进制字节。
- 追加到输出缓冲区。
- 开启写事件。

副作用：

- 修改 `output_buffer_`。
- 调用 `Channel::enableWriting()`，通过 `EventLoop` 更新 epoll。

边界：

- 已关闭的 `Session` 调用会抛出 `std::runtime_error`。
- 如果 `Packet` body 超过协议上限，`encodePacket()` 会抛出异常。
- 它不保证立刻写完；真正写出在 `handleWrite()` 中完成。

### close()

```cpp
void close();
```

作用：

- 主动关闭连接。
- 从 `EventLoop` 移除 `Channel`。
- 关闭 fd。
- 调用 close callback。

边界：

- 多次调用只会第一次生效。
- close callback 是显式 close 路径才调用；析构兜底清理不会通知。

### fd()

```cpp
int fd() const;
```

作用：

- 返回当前连接 fd。

边界：

- 关闭后返回 `-1`。

### started()

```cpp
bool started() const;
```

作用：

- 返回 `Session` 是否已经调用过 `start()`。

### closed()

```cpp
bool closed() const;
```

作用：

- 返回连接是否已经关闭。

### pendingOutputBytes()

```cpp
std::size_t pendingOutputBytes() const;
```

作用：

- 返回输出缓冲区里还没写出的字节数。

使用场景：

- 测试可以验证 `sendPacket()` 是否把数据放进输出缓冲区。
- 后续也可以用于调试 backpressure。

### setMessageCallback()

```cpp
void setMessageCallback(MessageCallback callback);
```

作用：

- 设置收到完整 `Packet` 后的回调。

### setCloseCallback()

```cpp
void setCloseCallback(CloseCallback callback);
```

作用：

- 设置连接关闭后的回调。

## 5. Session.cpp 讲解

文件：

```text
src/net/Session.cpp
```

### prepareSessionFd()

作用：

- 校验 fd。
- 把 fd 设置为 nonblocking。

为什么 `Session` 里还要设置 nonblocking：

- `Acceptor` 已经用 `accept4(SOCK_NONBLOCK)` 创建 accepted fd。
- 但 `Session` 自己再设置一次可以让类更健壮。
- 测试或未来其他入口传入 fd 时，也能保证 `Session` 的读写循环不会阻塞整个事件循环。

### 构造函数实现

构造函数会把 `Channel` 的几个 callback 绑定到成员函数：

```cpp
channel_.setReadCallback([this]() { handleRead(); });
channel_.setWriteCallback([this]() { handleWrite(); });
channel_.setCloseCallback([this]() { handleClose(); });
channel_.setErrorCallback([this]() { handleError(); });
```

含义：

- fd 可读时进入 `handleRead()`。
- fd 可写时进入 `handleWrite()`。
- 关闭或错误事件会走关闭逻辑。

### handleRead()

作用：

- 从 fd 读取数据。
- 循环读到 `EAGAIN` / `EWOULDBLOCK`。
- 把字节交给 `FrameDecoder`。
- 把完整 `Packet` 交给 message callback。

核心流程：

```text
read()
  ├── n > 0：feed 到 FrameDecoder
  ├── n == 0：对端关闭，close()
  ├── EAGAIN：本轮读完，返回
  ├── EINTR：继续读
  └── 其他错误：close()
```

为什么 read 0 表示关闭：

- TCP 对端正常关闭写方向后，本端 `read()` 会返回 0。
- 这表示不能再从这个连接读到新数据，应关闭 Session。

为什么 `FrameDecoder` 出错要关闭连接：

- 错误 magic、错误 version、body 长度超限都表示协议流已经不可信。
- 继续读可能导致状态错乱。
- 关闭连接是第一版最简单可靠的处理。

### handleWrite()

作用：

- 把输出缓冲区中的数据尽量写到 fd。
- 写空后关闭写事件。

核心流程：

```text
output_buffer_ 有数据
  ↓
send(MSG_NOSIGNAL)
  ├── n > 0：retrieve(n)
  ├── EAGAIN：等待下次可写
  ├── EINTR：重试
  └── 其他错误：close()
```

为什么用输出缓冲区：

- 非阻塞 fd 不能保证一次写完。
- 写不完的数据必须保存。
- 等 fd 再次可写时继续写。

为什么写空后要 `disableWriting()`：

- socket 通常经常可写。
- 如果没有待发送数据还关注 `EPOLLOUT`，事件循环会被频繁唤醒。

为什么使用 `MSG_NOSIGNAL`：

- 如果对端已经关闭，直接写 socket 可能触发 `SIGPIPE`。
- `MSG_NOSIGNAL` 避免进程被信号杀掉，错误会以返回值和 `errno` 表示。

### sendPacket()

作用：

- 调用 `encodePacket()`。
- 把编码结果 append 到 `output_buffer_`。
- 开启写关注。

为什么不直接阻塞写完：

- 服务端事件循环不能因为一个连接写慢而卡住。
- 非阻塞写加输出缓冲区是更稳的做法。

### closeConnection()

作用：

- 统一关闭逻辑。
- 防止重复关闭。
- 先从 `EventLoop` 移除 `Channel`。
- 再关闭 fd。
- 需要时调用 close callback。

为什么先 remove 再 close：

- `Epoller` 里保存的是 `Channel*`。
- 关闭连接前先移除，可以避免事件循环后续返回已经失效的对象指针。

## 6. 本步骤测试

新增测试文件：

```text
tests/test_session.cpp
```

接入位置：

```text
tests/CMakeLists.txt
tests/test_main.cpp
```

测试使用 `socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)` 创建一对已连接 stream fd。它不是 TCP listen/accept，但它和 TCP 连接一样是流式 fd，可以验证 `Session` 的非阻塞读写、epoll 事件和协议解包行为，而不需要提前实现完整 `TcpServer`。

### session reads packet and invokes message callback

验证：

- peer 写入一个完整编码后的 `Packet`。
- `Session` 读到数据。
- `FrameDecoder` 解析出完整包。
- message callback 收到正确的 `msg_type`、`seq_id` 和 body。

为什么要测：

- 这是 `Session` 的主读路径。

### session decodes sticky packets

验证：

- peer 一次写入两个连续编码包。
- `Session` 能解析出两个 `Packet`。

为什么要测：

- TCP 是字节流，没有消息边界。
- 粘包必须由 `FrameDecoder` 和 `Session` 组合正确处理。

### session decodes large frame across multiple reads

验证：

- peer 写入一个大于 `Session` 单次 read buffer 的包。
- `Session` 多次 read 后仍能组出完整 `Packet`。

为什么要测：

- 这覆盖半包场景。
- 网络层不能假设一次 read 就拿到完整消息。

### session sendPacket writes to peer

验证：

- `sendPacket()` 把数据放进输出缓冲区。
- `EventLoop` 触发写事件。
- peer 读到完整编码包。
- 输出缓冲区最终清空。

为什么要测：

- 这是 `Session` 的主写路径。
- 它证明写事件、输出缓冲和 Packet 编码能配合工作。

### session close on peer eof

验证：

- peer 关闭。
- `Session` 读到 EOF。
- close callback 被调用。
- `Session` 标记为 closed，fd 变为 `-1`。

为什么要测：

- 连接关闭是最常见生命周期事件。
- 上层 `TcpServer` 后续要依赖 close callback 删除 session。

### session closes on invalid frame

验证：

- peer 写入非法协议头。
- `FrameDecoder` 进入 error 状态。
- `Session` 关闭连接。

为什么要测：

- 非法协议流不能继续处理。
- 这覆盖协议错误边界。

运行测试：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
```

测试通过说明：

- `Session` 能处理读方向 Packet 解码。
- 能处理粘包和半包。
- 能通过输出缓冲区发送 Packet。
- 能处理 peer close 和协议错误。
- 当前 Step 的单连接生命周期达到了验收标准。

## 7. 面试时怎么讲

可以这样说：

> 我把每个客户端连接封装成 `Session`。它拥有 connected fd，并用 `Channel` 接入 `EventLoop`。读方向上，`Session` 循环 read 到 `EAGAIN`，把字节交给 `FrameDecoder`，解析出完整 Packet 后通过 callback 交给上层。写方向上，`sendPacket()` 先编码并追加到输出缓冲区，再开启写事件；`handleWrite()` 尽量写出缓冲区，写空后关闭写关注。这样连接生命周期、网络读写和协议拆包都集中在 Session，业务分发留给后续 `MessageRouter`。

为什么 `Session` 要拥有 fd：

- 一个客户端连接的生命周期应该有明确 owner。
- `Session` 负责 close fd，避免 fd 泄漏。
- `Epoller` 和 `EventLoop` 只调度，不拥有普通连接 fd。

为什么 `Session` 不处理业务：

- 网络层只负责把字节流变成 `Packet`。
- 业务层负责根据 `msg_type` 处理登录、聊天、心跳。
- 这样协议 I/O 和业务逻辑不会混在一起。

为什么需要 close callback：

- `Session` 关闭后，上层 `TcpServer` 需要从 session map 删除它。
- Step 11 只传 fd，避免提前引入复杂所有权模型。

## 8. 面试常见追问

**Q：`Session` 和 `Channel` 是什么关系？**

A：`Session` 拥有连接 fd 和对应的 `Channel`。`Channel` 只负责事件代理，真正的读写逻辑在 `Session::handleRead()` 和 `Session::handleWrite()`。

**Q：为什么 read/write 都要处理 `EAGAIN`？**

A：非阻塞 fd 在没有更多数据可读或暂时不能继续写时会返回 `EAGAIN`。这不是致命错误，而是告诉事件循环等下一次事件。

**Q：为什么输出缓冲区写空后要关闭写事件？**

A：如果没有数据还关注 `EPOLLOUT`，可写事件会频繁触发，造成空转。

**Q：为什么非法协议包直接关闭连接？**

A：协议头非法说明当前字节流已经不可信。第一版直接关闭连接，简单且安全，避免解码状态继续错乱。

**Q：`Session` 为什么不自己回复心跳？**

A：心跳是业务协议语义，应该由后续 `MessageRouter` 处理。`Session` 只负责传入和传出 `Packet`。
