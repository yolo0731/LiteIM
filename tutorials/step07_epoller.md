# Step 7：实现 Epoller

本步骤目标：把 Linux `epoll` 的核心系统调用封装进 `Epoller`，让后续 `EventLoop` 可以通过一个 C++ 类等待 fd 事件。

Step 6 已经定义了 `Epoller`、`Channel`、`EventLoop` 的接口。Step 7 只落地 `Epoller` 这一层，不实现完整事件循环，也不做业务连接管理。

## 1. 这一步要解决什么问题

IM 服务端后续会同时管理很多 fd：

- listen fd：等待新连接。
- client fd：等待客户端读写。
- timerfd：后续用于心跳超时。
- signalfd：后续用于优雅关闭。

如果每个 fd 都用一个线程阻塞等待，线程数量和上下文切换都会很重。LiteIM 采用 Reactor 模型：一个线程通过 `epoll_wait()` 等待多个 fd，哪个 fd 有事件，就把事件交给对应的 `Channel`。

`Epoller` 的职责就是封装这几个 Linux 调用：

- `epoll_create1()`：创建 epoll 实例。
- `epoll_ctl()`：新增、修改、删除关注的 fd。
- `epoll_wait()`：等待活跃事件。

第一版只使用 LT，也就是 level-triggered 模式，不启用 `EPOLLET`。

## 2. 职责边界

`Epoller` 负责：

- 拥有并关闭 `epoll_fd_`。
- 把 `Channel` 注册到 epoll。
- 修改 `Channel` 的关注事件。
- 从 epoll 移除 `Channel`。
- 等待事件并返回活跃 `Channel`。

`Epoller` 不负责：

- 不拥有普通 socket fd。
- 不拥有 `Channel`。
- 不调用业务回调。
- 不解析 `Packet`。
- 不管理连接生命周期。

这一步额外补了少量 `Channel` 状态方法，因为 `Epoller` 测试需要创建真实 `Channel`，并读取它的 fd 和事件掩码。但 `Channel::handleEvent()` 和通过 `EventLoop` 自动更新 epoll 的逻辑仍然留到后续 Step。

## 3. 本步骤新增文件

新增文件：

```text
src/net/Epoller.cpp
src/net/Channel.cpp
tests/test_epoller.cpp
tutorials/step07_epoller.md
```

修改文件：

```text
src/CMakeLists.txt
tests/CMakeLists.txt
tests/test_main.cpp
docs/architecture.md
docs/interview_notes.md
docs/project_layout.md
tutorials/README.md
tutorials/00_roadmap.md
README.md
task_plan.md
findings.md
progress.md
```

## 4. Epoller.hpp 讲解

文件：

```text
include/liteim/net/Epoller.hpp
```

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
- `events` 保存内核返回的事件位，例如 `EPOLLIN`、`EPOLLOUT`、`EPOLLERR`。

输入和输出：

- 它本身只是返回值结构，没有输入参数。
- `poll()` 会返回一组 `ActiveEvent`。

副作用：

- 没有副作用。
- 不拥有 `Channel`，只是临时保存指针。

边界：

- 如果上层在 `Channel` 注册后提前销毁 `Channel`，`Epoller` 中保存的指针会悬空。后续 `Session` 和 `Acceptor` 必须保证 remove 后再销毁对应 `Channel`。

### Epoller()

```cpp
Epoller();
```

作用：

- 创建一个 epoll 实例。
- 初始化内部事件数组。

实现中调用：

```cpp
epoll_create1(EPOLL_CLOEXEC)
```

为什么使用 `EPOLL_CLOEXEC`：

- 如果未来进程调用 `exec()` 启动其他程序，这个 epoll fd 不会泄漏到子进程。
- 这是 Linux fd 资源管理里的常见安全习惯。

输出：

- 构造成功后，对象内部持有一个有效 `epoll_fd_`。

失败场景：

- `epoll_create1()` 失败时抛出 `std::system_error`。
- 构造函数不能返回错误码，所以这里用异常更合适。

### ~Epoller()

```cpp
~Epoller();
```

作用：

- 对象销毁时关闭 `epoll_fd_`。

副作用：

- 释放操作系统 fd 资源。

边界：

- `Epoller` 只关闭自己创建的 epoll fd。
- 它不会关闭被注册进来的 socket fd，因为那些 fd 由 `Acceptor` 或 `Session` 管理。

### poll()

```cpp
std::vector<ActiveEvent> poll(int timeout_ms);
```

作用：

- 调用 `epoll_wait()` 等待活跃事件。
- 把内核返回的 `epoll_event` 转换为 `ActiveEvent`。

输入：

- `timeout_ms`：等待超时时间，单位毫秒。
- `0` 表示立即返回。
- 正数表示最多等待指定毫秒数。
- 后续可以传 `-1` 表示一直等待。

输出：

- 返回本轮活跃事件列表。
- 每个元素包含发生事件的 `Channel*` 和事件位。

副作用：

- 会把内核返回的事件同步到 `Channel::setRevents()`。
- 如果当前事件数组刚好被填满，会扩大内部 `events_` 容量，避免高并发时频繁截断活跃事件。

失败场景：

- 如果 `epoll_wait()` 被信号打断并返回 `EINTR`，当前实现返回空列表。
- 其他系统调用错误会抛出 `std::system_error`。

为什么 `EINTR` 返回空列表：

- Linux 系统调用可能被信号中断。
- 对事件循环来说，这不是业务错误。
- 返回空列表后，未来 `EventLoop` 下一轮继续 poll 即可。

### updateChannel()

```cpp
void updateChannel(Channel* channel);
```

作用：

- 把 `Channel` 添加到 epoll。
- 或者修改已经注册过的 `Channel` 关注事件。

输入：

- `channel`：要注册或修改的事件代理。

实现思路：

- 如果 `channel->fd()` 还不在 `registered_fds_`，调用 `epoll_ctl(EPOLL_CTL_ADD)`。
- 如果 fd 已经注册过，调用 `epoll_ctl(EPOLL_CTL_MOD)`。
- `epoll_event.data.ptr` 保存 `Channel*`，这样 `poll()` 返回时可以直接找到对应 `Channel`。

副作用：

- 修改内核 epoll 关注集合。
- 第一次添加成功后，把 fd 记录进 `registered_fds_`。

失败场景：

- `channel == nullptr` 时抛出 `std::invalid_argument`。
- `channel->fd() < 0` 时抛出 `std::invalid_argument`。
- `epoll_ctl()` 失败时抛出 `std::system_error`。

为什么不直接返回 bool：

- `updateChannel()` 是 Reactor 内部关键操作。
- 如果 add/mod 失败，后续事件循环状态已经不可靠。
- 抛异常可以让测试和上层更早发现错误，而不是静默丢事件。

### removeChannel()

```cpp
void removeChannel(Channel* channel);
```

作用：

- 从 epoll 中删除某个 `Channel` 对应的 fd。

输入：

- `channel`：要移除的事件代理。

副作用：

- 调用 `epoll_ctl(EPOLL_CTL_DEL)`。
- 从 `registered_fds_` 中删除 fd。

边界：

- `channel == nullptr` 时直接返回。
- fd 无效时直接返回。
- fd 没注册过时直接返回。
- 对已注册 fd 调用删除失败时抛出 `std::system_error`。

为什么未知 fd 删除做 no-op：

- 连接清理路径可能被重复触发。
- 删除未知 fd 没有必要让程序崩溃。
- 真正的系统调用失败仍然会暴露出来。

## 5. Epoller.cpp 讲解

文件：

```text
src/net/Epoller.cpp
```

### makeSystemError()

这是 `.cpp` 文件里的私有辅助函数。

作用：

- 捕获当前 `errno`。
- 生成带系统错误码和操作名称的 `std::system_error`。

为什么要封装：

- `epoll_create1()`、`epoll_ctl()`、`epoll_wait()` 都可能失败。
- 错误抛出时保留操作名称，排查时能知道是哪个系统调用失败。

### 构造函数实现

构造函数里做两件事：

- 创建 epoll fd。
- 初始化 `events_` 数组，初始容量为 16。

`events_` 是传给 `epoll_wait()` 的缓冲区。它不是业务数据，只是承接内核返回事件的临时数组。

### poll() 实现

核心调用：

```cpp
epoll_wait(epoll_fd_, events_.data(), events_.size(), timeout_ms)
```

返回值含义：

- 大于 0：有对应数量的活跃事件。
- 等于 0：超时，没有事件。
- 小于 0：系统调用失败。

转换过程：

1. 取出每个 `epoll_event`。
2. 从 `event.data.ptr` 转回 `Channel*`。
3. 把 `event.events` 写入 `Channel::setRevents()`。
4. 生成 `ActiveEvent` 返回给上层。

为什么 `Epoller` 返回 `Channel*` 而不是 fd：

- fd 只说明“哪个数字有事件”。
- `Channel` 才知道这个 fd 对应哪些回调。
- 后续 `EventLoop` 可以直接把事件交给 `Channel::handleEvent()`。

### updateChannel() 实现

`registered_fds_` 用来判断 fd 是第一次添加还是已经存在。

第一次添加：

```cpp
EPOLL_CTL_ADD
```

已经存在：

```cpp
EPOLL_CTL_MOD
```

这样 `EventLoop` 后续只需要调用同一个 `updateChannel()`，不用关心底层应该 add 还是 mod。

### removeChannel() 实现

删除时先检查 fd 是否在 `registered_fds_` 里。

如果不在，直接返回。这样可以让清理路径更容易写，避免重复 remove 造成不必要错误。

如果在，调用：

```cpp
EPOLL_CTL_DEL
```

删除成功后再从 `registered_fds_` 里 erase。

## 6. Channel.cpp 本步骤补充的内容

文件：

```text
src/net/Channel.cpp
```

Step 7 不是完整实现 `Channel`，只补 Epoller 需要的状态方法。

### Channel()

```cpp
Channel(EventLoop* loop, int fd);
```

作用：

- 保存所属 `EventLoop*`。
- 保存 fd。

边界：

- `Channel` 不拥有 fd。
- 析构时不关闭 fd。
- 当前 Step 不使用 `loop_` 自动更新 epoll，后续 Step 会补上。

### fd()

```cpp
int fd() const;
```

作用：

- 返回这个 `Channel` 绑定的 fd。

使用场景：

- `Epoller::updateChannel()` 和 `removeChannel()` 需要这个 fd 调用 `epoll_ctl()`。

### events()

```cpp
std::uint32_t events() const;
```

作用：

- 返回当前想关注的事件。

例如：

- 调用 `enableReading()` 后，`events()` 包含 `EPOLLIN | EPOLLPRI`。
- 调用 `enableWriting()` 后，`events()` 包含 `EPOLLOUT`。

### revents()

```cpp
std::uint32_t revents() const;
```

作用：

- 返回本轮实际发生的事件。

区别：

- `events` 是“我想监听什么”。
- `revents` 是“内核告诉我实际发生了什么”。

### setRevents()

```cpp
void setRevents(std::uint32_t revents);
```

作用：

- 由 `Epoller::poll()` 调用，把内核返回事件写回 `Channel`。

副作用：

- 修改 `revents_`。

### isNoneEvent()

```cpp
bool isNoneEvent() const;
```

作用：

- 判断当前没有关注任何事件。

后续用途：

- `EventLoop` 或 `Channel` 清理时可以根据它判断是否需要从 epoll 中移除。

### isWriting()

```cpp
bool isWriting() const;
```

作用：

- 判断当前是否关注写事件。

后续用途：

- `Session` 处理输出缓冲区时，如果还有数据没写完，就打开写事件。
- 如果数据写完，就关闭写事件，避免 epoll 一直报告可写。

### enableReading()

```cpp
void enableReading();
```

作用：

- 在 `events_` 中加入读事件。

当前事件包含：

```cpp
EPOLLIN | EPOLLPRI
```

边界：

- 当前 Step 只修改内存里的事件掩码。
- 自动通知 `EventLoop` 更新 epoll 留到后续 Step。

### enableWriting()

```cpp
void enableWriting();
```

作用：

- 在 `events_` 中加入写事件 `EPOLLOUT`。

后续用途：

- 非阻塞写没有一次写完时，`Session` 会打开写事件，等待 fd 可写后继续发送剩余数据。

### disableWriting()

```cpp
void disableWriting();
```

作用：

- 从 `events_` 中移除写事件。

为什么需要：

- socket 通常大部分时间都是可写的。
- 如果一直关注写事件，事件循环会频繁被唤醒。
- 只有输出缓冲区有待发送数据时才应该关注写事件。

### disableAll()

```cpp
void disableAll();
```

作用：

- 清空所有关注事件。

使用场景：

- 连接准备关闭。
- 测试中验证 `Epoller::updateChannel()` 能把兴趣事件从读事件修改为其他事件。

### setReadCallback() / setWriteCallback() / setCloseCallback() / setErrorCallback()

作用：

- 保存对应事件回调。

当前 Step 的边界：

- 只保存回调，不触发回调。
- `handleEvent()` 的具体分发逻辑留到后续 `Channel` Step。

## 7. 本步骤测试

新增测试文件：

```text
tests/test_epoller.cpp
```

接入位置：

```text
tests/CMakeLists.txt
tests/test_main.cpp
```

测试使用 `pipe()` 创建一对真实 fd：

- 写端写入 1 字节。
- 读端会变成可读。
- 把读端 fd 包装成 `Channel`，注册给 `Epoller`。

这样测试的是 Linux 内核真实返回的 epoll 事件，不是只测内存状态。

### epoller poll timeout returns empty

验证：

- 没有注册 fd 时，`poll(0)` 返回空列表。

为什么要测：

- 事件循环空转时不能误报事件。

### epoller add readable fd

验证：

- `updateChannel()` 能用 `EPOLL_CTL_ADD` 注册读 fd。
- pipe 写入后，`poll()` 能返回对应 `Channel`。
- 返回事件包含 `EPOLLIN`。
- `Channel::revents()` 会同步更新。
- 关注事件里没有 `EPOLLET`。

为什么要测：

- 这是 Step 7 最核心路径：注册 fd，等待可读事件，拿回正确 `Channel`。

### epoller poll is level triggered

验证：

- 写入 pipe 后不读取数据。
- 连续两次 `poll()` 都能看到可读事件。

为什么要测：

- 这证明第一版使用的是 LT 模式。
- LT 模式下，只要 fd 状态仍然满足条件，事件会继续被报告。

### epoller modifies interest mask

验证：

- 先注册读事件。
- 再把关注事件修改为写事件。
- pipe 读端虽然有数据可读，但不再返回读事件。

为什么要测：

- 证明 `updateChannel()` 对已注册 fd 会走 `EPOLL_CTL_MOD`。
- 后续 `Session` 打开/关闭写事件会依赖这个能力。

### epoller remove channel stops events

验证：

- 注册后调用 `removeChannel()`。
- 再让 fd 可读，`poll()` 不再返回事件。
- 重复 remove 不报错。

为什么要测：

- 连接关闭时必须能从 epoll 中移除 fd。
- 清理路径重复触发时应该保持稳定。

### epoller update rejects invalid channel

验证：

- `updateChannel(nullptr)` 会抛出 `std::invalid_argument`。
- fd 为 -1 的 `Channel` 也会被拒绝。

为什么要测：

- 注册无效 fd 会导致 epoll 内部状态错误。
- 早失败比静默忽略更容易排查。

运行测试：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
```

测试通过说明：

- `Epoller` 可以创建和释放 epoll fd。
- 可以 add、mod、del 真实 fd。
- 可以从 `epoll_wait()` 拿回正确的活跃事件。
- 当前实现保持 LT 模式。

## 8. 面试时怎么讲

可以这样说：

> 我在网络层封装了一个 `Epoller`，它只负责 Linux `epoll` 系统调用，不处理业务逻辑，也不拥有 socket fd。`Channel` 保存 fd 和关注事件，`Epoller` 把 `Channel*` 放进 `epoll_event.data.ptr`，这样 `epoll_wait()` 返回时可以直接找到对应的事件代理。第一版使用 LT 模式，不启用 `EPOLLET`，这样读写逻辑更容易保证正确性，后续等 `Session` 的读写循环稳定后再考虑 ET 优化。

为什么 `Epoller` 不直接保存 fd 到回调：

- fd 只是内核资源编号。
- 回调、关注事件、实际事件都属于 `Channel`。
- `Epoller` 保持系统调用封装层的职责，后续 `EventLoop` 和业务层会更清晰。

为什么第一版用 LT：

- LT 下，只要数据还没读完，epoll 会继续报告可读。
- 即使某一轮没有读到 `EAGAIN`，下一轮仍有机会继续处理。
- ET 性能潜力更高，但要求每次读写都循环到 `EAGAIN`，否则可能漏事件。
- 教学项目先用 LT 更容易验证正确性。

为什么 `Epoller` 不关闭普通 socket fd：

- 它只拥有 `epoll_fd_`。
- 普通连接 fd 的生命周期由 `Session` 管理。
- listen fd 的生命周期由 `Acceptor` 管理。
- 谁创建资源，谁负责释放，避免重复 close。

为什么 `poll()` 返回 `Channel*`：

- 后续 `EventLoop` 不需要再维护 fd 到对象的映射。
- `Channel` 里会保存读、写、关闭、错误回调。
- 事件分发可以写成：poll 得到活跃 Channel，然后让 Channel 自己处理事件。

## 9. 面试常见追问

**Q：`epoll_create1(EPOLL_CLOEXEC)` 比 `epoll_create()` 好在哪里？**

A：`EPOLL_CLOEXEC` 会让 epoll fd 在 `exec()` 后自动关闭，避免 fd 泄漏到子进程。它也比先 create 再 fcntl 设置 close-on-exec 更原子。

**Q：`epoll_ctl` 的 ADD、MOD、DEL 分别什么时候用？**

A：fd 第一次加入 epoll 用 ADD；fd 已经在 epoll 中，只是关注事件变化，用 MOD；连接关闭或事件源销毁前用 DEL。

**Q：为什么要记录 `registered_fds_`？**

A：`updateChannel()` 对上层只暴露一个接口，但底层要区分 ADD 和 MOD。记录已注册 fd 后，上层不用关心某个 fd 是第一次添加还是修改关注事件。

**Q：LT 和 ET 的核心区别是什么？**

A：LT 是状态触发，只要 fd 仍然可读或可写，就会持续报告；ET 是边沿触发，状态从不可读变为可读时报告一次。ET 通常要求非阻塞 fd 并循环读写到 `EAGAIN`。

**Q：为什么 `Epoller` 不处理 `read()`？**

A：`Epoller` 只告诉上层哪个 fd 有事件。具体读多少、怎么处理半包、怎么关闭连接，属于 `Session`、`Buffer`、`FrameDecoder` 的职责。

**Q：`event.data.ptr` 保存裸指针安全吗？**

A：裸指针本身不拥有对象，安全性取决于生命周期管理。后续设计中，`Session` 或 `Acceptor` 会在 `Channel` 销毁前先调用 `removeChannel()`，保证 epoll 不再返回悬空指针。
