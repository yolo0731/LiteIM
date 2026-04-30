# Step 4：实现 Buffer

本步骤目标：实现网络层基础缓冲区 `Buffer`，为后续 `Session` 的输入缓冲区和输出缓冲区做准备。

Step 2 和 Step 3 解决的是协议层问题：

```text
Packet 定义消息格式
FrameDecoder 从 TCP 字节流中切出 Packet
```

Step 4 开始进入网络层基础设施：

```text
Buffer 保存 socket 读入或准备写出的字节数据
```

## 1. 为什么需要 Buffer

网络程序不能假设一次读写就完整成功。

### 1.1 输入方向

服务端从 socket 读数据时，可能读到：

- 半个 Header。
- 一个 Header 加半个 Body。
- 多个完整 Packet。
- 一个完整 Packet 加下一个 Packet 的一部分。

这些数据需要先放到某个地方，再交给协议层处理。

### 1.2 输出方向

非阻塞 socket 写数据时，可能出现短写。

比如你想发送 1000 字节：

```text
send(fd, data, 1000, 0)
```

但内核发送缓冲区空间不够，实际只写出去 300 字节。

剩下 700 字节不能丢，必须保存起来，等下次 fd 可写时继续发送。

这就是输出缓冲区的意义。

## 2. Buffer 的职责边界

`Buffer` 是一个通用字节容器。

它负责：

- 追加数据。
- 查看当前可读数据。
- 消费部分数据。
- 一次性取出所有可读数据。

它不负责：

- 不调用 `read()`。
- 不调用 `write()`。
- 不管理 fd。
- 不知道什么是 `Packet`。
- 不做 JSON 解析。
- 不处理业务逻辑。

这样设计的好处是：

- `Buffer` 可以独立测试。
- 后续 `Session` 可以同时持有 input buffer 和 output buffer。
- 协议层和网络层职责清楚。

## 3. 本步骤新增文件

```text
server/net/Buffer.hpp
server/net/Buffer.cpp
tests/test_buffer.cpp
tutorials/step04_buffer.md
```

修改文件：

```text
server/CMakeLists.txt
tests/CMakeLists.txt
tests/test_main.cpp
docs/architecture.md
docs/interview_notes.md
tutorials/README.md
task_plan.md
findings.md
progress.md
```

其中 `task_plan.md`、`findings.md`、`progress.md` 是 `planning-with-files` skill 的项目工作记忆文件。

## 4. Buffer.hpp 讲解

文件：

```text
server/net/Buffer.hpp
```

接口：

```cpp
class Buffer {
public:
    void append(const char* data, std::size_t len);
    void appendString(const std::string& data);

    std::size_t readableBytes() const;
    const char* peek() const;

    void retrieve(std::size_t len);
    std::string retrieveAllAsString();
};
```

### append()

追加一段字节。

```cpp
void append(const char* data, std::size_t len);
```

规则：

- `len == 0` 时什么都不做。
- `data == nullptr && len > 0` 时抛出异常。

### appendString()

追加字符串，内部复用 `append()`。

```cpp
void appendString(const std::string& data);
```

### readableBytes()

返回当前还没被消费的字节数。

```cpp
std::size_t readableBytes() const;
```

### peek()

返回当前可读数据的起始地址。

```cpp
const char* peek() const;
```

注意：`peek()` 不消费数据，只是查看。

### retrieve()

消费指定长度的数据。

```cpp
void retrieve(std::size_t len);
```

如果 `len >= readableBytes()`，说明要消费的长度超过或等于当前可读数据，直接清空缓冲区。

### retrieveAllAsString()

取出当前所有可读数据，并清空缓冲区。

```cpp
std::string retrieveAllAsString();
```


### compactIfNeeded()

作用是当已消费空间较大时，压缩缓冲区，把剩余可读数据前移。

```cpp
void compactIfNeeded();
constexpr std::size_t kCompactThreshold = 1024;
```

压缩条件的意思是：

- 如果已消费数据还不到 1024 字节，就先不整理。
- 如果已消费数据小于总数据的一半，也先不整理。
- 因为 `erase(0, read_index_)` 会移动后面的数据，有成本，小数据没必要频繁整理。

## 5. 内部设计

当前实现使用：

```cpp
std::string buffer_;
std::size_t read_index_ = 0;
```

`buffer_` 保存全部数据。

`read_index_` 表示当前已经消费到哪里。该 index 之前的数据已经被消费了，index 及之后的数据是可读的。

例如：

```text
buffer_ = "abcdef"
read_index_ = 2
```

此时可读数据是：

```text
"cdef"
```

也就是：

```cpp
peek() == buffer_.data() + read_index_
readableBytes() == buffer_.size() - read_index_
```

## 6. 为什么不每次 retrieve 都 erase

如果每次消费数据都执行：

```cpp
buffer_.erase(0, len);
```

会导致剩余字节整体前移。

在网络程序里，频繁移动内存会带来额外开销。

所以当前实现是：

1. `retrieve(len)` 只增加 `read_index_`。
2. 当已消费空间比较大时，再调用 `compactIfNeeded()` 压缩。

当前压缩条件：

```cpp
constexpr std::size_t kCompactThreshold = 1024;
if (read_index_ >= kCompactThreshold && read_index_ * 2 >= buffer_.size()) {
    buffer_.erase(0, read_index_);
    read_index_ = 0;
}
```

这不是最终高性能 Buffer，只是第一版足够清晰、容易测试的实现。

## 7. 测试覆盖

文件：

```text
tests/test_buffer.cpp
```

这些测试的目的不是“凑覆盖率”，而是确认 `Buffer` 作为未来 `Session` 输入/输出缓冲区时，最基础的数据追加、查看、消费和边界处理都是可靠的。

测试分三类：

1. 正常读写路径：验证追加数据后能正确读取，部分消费后剩余数据正确。
2. 清空和复用路径：验证 `retrieveAllAsString()`、超长 `retrieve()` 和消费后继续追加都符合预期。
3. 边界和异常路径：验证空追加是 no-op，非法空指针追加会抛异常。

覆盖：

1. `append()` 后 `readableBytes()` 正确。
2. `peek()` 能查看可读数据。
3. `appendString()` 可以连续追加。
4. `retrieve()` 可以消费部分数据。
5. `retrieveAllAsString()` 返回所有可读数据并清空。
6. `retrieve()` 长度超过可读数据时清空。
7. 消费后继续 append，剩余数据顺序正确。
8. `append(nullptr, 1)` 抛异常。
9. `append(nullptr, 0)` 是 no-op。

如果这些测试通过，说明：

- `Buffer` 能正确维护“已消费数据”和“可读数据”的边界。
- 后续 `Session` 可以安全地把它作为 input buffer 和 output buffer 的基础。
- 当前实现对明显错误输入有防护，不会静默接受非法空指针。

## 8. 编译和测试

在 `LiteIM/` 根目录执行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/tests/liteim_tests
./build/server/liteim_server
```

预期测试包含：

```text
[PASS] buffer append and readable bytes
[PASS] buffer appendString
[PASS] buffer retrieve partial
[PASS] buffer retrieve all as string
[PASS] buffer retrieve more than readable clears buffer
[PASS] buffer append after retrieve
[PASS] buffer append null with non-zero length throws
[PASS] buffer append null with zero length is noop
```

## 9. 面试时怎么讲

可以这样说：

> 我在网络层封装了通用 Buffer，它不直接操作 socket，也不理解业务协议，只负责保存、查看和消费字节数据。输入方向上，Buffer 用来保存 socket 读到但还没处理完的数据；输出方向上，Buffer 用来保存非阻塞写没有一次写完的剩余数据。这样后续 Session 可以组合 Buffer、FrameDecoder 和 Channel，把网络读写、协议拆包和业务分发分开。

展开点：

- 非阻塞写可能短写，所以需要 output buffer。
- TCP 读入数据可能分批到达，所以需要 input buffer。
- Buffer 不依赖 Packet，因此网络层和协议层边界清楚。
- 当前实现用 `read_index_` 避免每次消费都移动内存。

### 9.1 讲解思路

面试时建议按这个顺序讲：

1. 先讲网络 I/O 的问题：非阻塞读写不保证一次处理完所有数据。
2. 再讲输入缓冲：读到的数据可能暂时无法组成完整 Packet，需要缓存。
3. 再讲输出缓冲：`write()` 可能短写，剩余字节必须保存，等下次 EPOLLOUT 继续写。
4. 再讲职责边界：`Buffer` 不读 fd、不解析协议、不做业务，只管理字节。
5. 再讲实现细节：用 `std::string + read_index_`，避免每次 `retrieve()` 都移动内存。

### 9.2 面试中容易被问到的问题

**Q1：为什么需要 output buffer？**

非阻塞 socket 的 `write()` 可能只写出一部分数据。没有 output buffer，剩余数据就会丢，消息会被截断。

**Q2：为什么需要 input buffer？**

一次 `read()` 可能只读到半个包，也可能读到多个包。input buffer 用来保存读到但还没处理完的数据。

**Q3：`Buffer` 和 `FrameDecoder` 的区别是什么？**

`Buffer` 是网络层通用字节容器，不理解协议；`FrameDecoder` 是协议层组件，理解 Header、Body 和 `body_len`。

**Q4：为什么 `peek()` 不消费数据？**

因为很多场景需要先查看数据是否足够，比如先看 Header，再决定是否能消费完整 frame。

**Q5：为什么 `retrieve()` 不每次都 `erase()`？**

`erase(0, len)` 会移动剩余数据，频繁调用成本高。用 `read_index_` 延迟压缩，可以减少内存移动。

**Q6：当前 `Buffer` 是最终高性能实现吗？**

不是。当前是第一版清晰实现。更高性能的实现可以用 prependable/readable/writable 三段式 buffer，或者环形缓冲区。

**Q7：为什么 `append(nullptr, 1)` 要抛异常？**

因为传入非零长度却没有有效数据指针是调用方错误。这里显式失败比静默行为更安全。

## 10. 下一步预告

Step 5 会实现：

```text
server/net/SocketUtil.hpp
```

它会封装 Linux socket 常用函数：

- `createNonBlockingSocket()`
- `setNonBlocking()`
- `setReuseAddr()`
- `setReusePort()`
- `closeFd()`
- `getSocketError()`

Step 4 的 `Buffer` 是数据容器，Step 5 的 `SocketUtil` 开始接触真正的 Linux socket API。
