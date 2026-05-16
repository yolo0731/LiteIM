# Step 7：实现 Buffer

## 0. 本 Step 结论

- 目标：本 Step 进入网络层，但还不写 socket、epoll、Reactor 或 Session。
- 前置依赖：依赖 Step 0-6 已建立的工程、协议或运行时基础。
- 主要交付：`实现 Buffer` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不实现 `SocketUtil`

## 1. 为什么需要这个 Step

本 Step 进入网络层，但还不写 socket、epoll、Reactor 或 `Session`。

目标是先实现一个可复用的字节缓冲区 `Buffer`。它可以作为网络读写路径里的通用字节存储，后续连接发送路径会用它保存待写出的输出数据：

```text
socket read -> FrameDecoder -> Packet
Packet / response -> output Buffer -> socket write
```

### 为什么需要 Buffer

TCP 是字节流，`read()` 一次读到的数据不一定刚好是一条完整消息。

如果每次读写都直接操作临时 `std::string`，或者在协议/网络层混用 `std::vector<char>` 和 `std::vector<std::uint8_t>`，后续会出现几个问题：

- 半包数据需要保存到下一次读事件。
- 粘包时需要从同一段字节里消费多个完整包。
- 输出数据可能因为对端太慢而暂时写不完。
- 频繁创建临时字符串会增加不必要的分配。

所以 `Buffer` 负责维护一段连续内存，并用两个索引划分状态：

```text
0                  read_index_          write_index_          buffer_.size()
| already read     | readable bytes     | writable bytes      |
```

当前 Step 只处理内存里的字节，不处理 fd。

### retrieve 为什么返回 Status

`retrieve(len)` 如果传入的 `len` 超过 `readableBytes()`，说明调用方想消费不存在的数据。

这里不使用 `assert`，而是返回：

```cpp
Status::error(ErrorCode::InvalidArgument, ...)
```

原因是服务端会面对网络输入、客户端断开、协议错误等情况。底层工具函数应该把错误交给上层决定如何关闭连接或记录日志，而不是直接让进程崩溃。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `实现 Buffer` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不实现 `SocketUtil`
- 不实现 `Epoller`
- 不实现 `Channel`
- 不实现 `EventLoop`
- 不实现 `Session`
- 不实现 `TcpServer`
- 不实现 慢客户端高水位策略

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/net/Buffer.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/Buffer.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/buffer_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/CMakeLists.txt` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

`Buffer.hpp` 定义网络层通用字节缓冲区。

常量和构造：

- `kDefaultBufferSize = 1024` 是默认初始容量。
- `Buffer(initial_size)` 创建内部 `Bytes`。如果传入 0，会至少创建 1 字节容量，避免空 vector 后续指针语义混乱。

public 查询：

- `readableBytes()` 返回 `write_index_ - read_index_`。
- `writableBytes()` 返回 `buffer_.size() - write_index_`。
- `peek()` 返回当前可读区域起始地址，不移动读指针。

append 系列：

- `append(const Byte* data, len)` 追加原始字节。`data == nullptr && len != 0` 返回 `InvalidArgument`，`len == 0` 是 no-op。
- `append(const Bytes&)` 追加项目统一字节数组。
- `append(const std::string&)` 追加文本内容的原始字节，主要方便测试和文本 payload。

retrieve 系列：

- `retrieve(len)` 消费指定字节。超过 `readableBytes()` 返回 `InvalidArgument`，并且不修改索引。
- `retrieveAll()` 把读写索引都归零，不释放容量。
- `retrieveAllAsString()` 复制所有可读字节为字符串，然后清空可读区域。

关键 private 成员和 helper：

- `buffer_` 是实际连续内存。
- `read_index_` 指向可读区域起点。
- `write_index_` 指向可写区域起点。
- `ensureWritableBytes(len)` 先尝试复用前面已读空间，再决定扩容。

线程和所有权边界：

- `Buffer` 是值对象，不拥有 fd。
- 一个实例通常只在所属 owner loop 中访问，例如 `Session` 输出缓冲。
- `peek()` 返回的指针只在下一次修改 buffer 前有效，调用方不能长期保存。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

当前路线里输入半包缓存由 `FrameDecoder` 维护，`Buffer` 主要服务 `Session` 输出路径：当非阻塞 socket 一次写不完 encoded Packet 时，剩余字节留在 `output_buffer_`，等下一次 `EPOLLOUT` 继续写。

### 2. 上下层调用连接

```text
Session::sendEncodedInLoop()
    -> output_buffer_.append(encoded)
    -> Channel::enableWriting()
    -> EventLoop / Epoller 关注 EPOLLOUT
    -> Session::handleWrite()
    -> write(fd, output_buffer_.peek(), readableBytes())
    -> output_buffer_.retrieve(n)
```

上游是 `Session` 的发送逻辑，下游是非阻塞 `write()` 和 `Channel` 写事件开关。

### 3. 整体运行链路

1. `Session` 已经把 `Packet` 编码成 `Bytes`。
2. `sendEncodedInLoop()` 在 append 前做输出高水位检查。
3. [Buffer::append()](../src/net/Buffer.cpp) 确保尾部有足够可写空间。
4. 如果空间不足，[ensureWritableBytes()](../src/net/Buffer.cpp) 先复用已读前缀，仍不够再扩容。
5. `Session` 开启写事件。
6. `handleWrite()` 用 `peek()` 和 `readableBytes()` 写 socket。
7. 写出多少字节就 [retrieve()](../src/net/Buffer.cpp) 多少字节。
8. 缓冲区清空时关闭 `EPOLLOUT`。

### 4. 自身内部运行流程

整体可以看成 3 步：追加、消费、复用空间。

核心成员职责：

- `buffer_` 是底层连续字节数组。
- `read_index_` 指向可读区起点。
- `write_index_` 指向可读区终点和可写区起点。
- 可读区是 `[read_index_, write_index_)`，可写区是 `[write_index_, buffer_.size())`。

核心函数流程：

- [append(const Byte*, size_t)](../src/net/Buffer.cpp)：空输入成功，非空空指针失败，正常路径先确保空间再 `memcpy`。
- [append(const Bytes&)](../src/net/Buffer.cpp)：复用 raw bytes 入口。
- [retrieve(size_t)](../src/net/Buffer.cpp)：消费指定字节，越界返回错误且不改变状态。
- [retrieveAll()](../src/net/Buffer.cpp)：读写索引归零。
- [retrieveAllAsString()](../src/net/Buffer.cpp)：把当前可读区复制成字符串后清空。
- [ensureWritableBytes()](../src/net/Buffer.cpp)：先看尾部空间，再移动可读区到开头，最后才扩容。

`ensureWritableBytes(len)` 可以理解成“先用现有空间，再整理，最后扩容”：

```text
追加写入请求
    ↓
优先使用 write_index_ 后面的剩余空间
    ↓
剩余空间不足时，整理 read_index_ 前方的已读空间
    ↓
整理后仍不足，再扩大底层 vector
    ↓
append() 获得连续可写区域
```

这里的核心是尽量复用已经读走的前缀空间，避免每次 append 都扩容；只有可写尾部和可回收前缀合起来仍不够时，才真的调整 `buffer_` 容量。

### 5. 该项目代码在实际应用中的具体数据例子

Bob (`user_id=1002`) 离线期间，Alice 的消息 `message_id=5001` 会等 Bob 下次上线再推送。服务端准备给 Bob 的 `session_id=42` 写出 `PRIVATE_MESSAGE_PUSH` 时，encoded Packet 先 append 到 `Buffer`；如果 socket 只写出前 30 字节，`retrieve(30)` 后剩余字节继续留在 output buffer，下一次 `EPOLLOUT` 再写。

## 6. 关键实现点

### Buffer 的公开接口

```cpp
class Buffer {
public:
    explicit Buffer(std::size_t initial_size = kDefaultBufferSize);

    std::size_t readableBytes() const noexcept;
    std::size_t writableBytes() const noexcept;
    const Byte* peek() const noexcept;

    Status append(const Byte* data, std::size_t len);
    Status append(const Bytes& data);
    Status append(const std::string& value);

    Status retrieve(std::size_t len);
    void retrieveAll() noexcept;
    std::string retrieveAllAsString();
};
```

接口含义：

- `readableBytes()`：当前能被上层读取的字节数。
- `writableBytes()`：当前还能直接写入的剩余空间。
- `peek()`：返回可读区域起始地址，不移动读指针。
- `append(const Byte*, len)`：追加原始字节数据，空间不足时自动扩容或整理空间。
- `append(const Bytes&)`：追加项目统一的二进制字节数组。
- `append(const std::string&)`：追加文本字节，方便测试和后续文本 payload。
- `retrieve()`：消费指定长度的可读数据。
- `retrieveAll()`：清空当前可读区域。
- `retrieveAllAsString()`：取出所有可读数据并清空。

### 自动扩容和空间复用

内部 `ensureWritableBytes(len)` 的逻辑分三步：

1. 如果尾部可写空间已经够，直接返回。
2. 如果尾部空间不够，但前面已经消费掉的空间加起来够用，就把可读数据移动到开头。
3. 如果仍然不够，再扩容 `buffer_`。

示例：

```text
buffer size = 8
data = "abcdef"
retrieve(4)

状态变成：

[a b c d e f _ _]
         ^ read_index_
             ^ write_index_

可读数据只剩 "ef"，前面 4 字节可以复用。
追加 6 个新字节前，内部会把 "ef" 移到开头：

[e f _ _ _ _ _ _]
 ^ read_index_
     ^ write_index_
```

这样可以减少扩容次数。

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `实现 Buffer` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

新增测试文件：

```text
tests/net/buffer_test.cpp
```

测试用例：

```cpp
TEST(BufferTest, DefaultBufferHasNoReadableBytes)
TEST(BufferTest, AppendIncreasesReadableBytes)
TEST(BufferTest, AppendStringStoresReadableData)
TEST(BufferTest, AppendBytePointerStoresBytes)
TEST(BufferTest, RetrieveAdvancesReadIndex)
TEST(BufferTest, RetrieveAllResetsBuffer)
TEST(BufferTest, RetrieveAllAsStringReturnsReadableDataAndClearsBuffer)
TEST(BufferTest, AppendExpandsWhenNeeded)
TEST(BufferTest, AppendCompactsReadableDataBeforeExpanding)
TEST(BufferTest, AppendExpandsAndPreservesExistingData)
TEST(BufferTest, RetrievePastReadableBytesReturnsError)
TEST(BufferTest, NullAppendWithNonzeroLengthReturnsError)
TEST(BufferTest, NullAppendWithZeroLengthIsOk)
```

这些测试覆盖：

- 初始状态。
- 字节追加。
- 字符串追加。
- `Byte*` 输入。
- 读指针移动。
- 清空缓冲区。
- 自动扩容。
- 已读空间复用。
- 越界消费保护。
- 空指针输入保护。

## 8. 验证命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 9. 面试表达

### 一句话

我在网络层实现了一个 socketagnostic Buffer，用读写索引维护可读区和可写区，支持 append、retrieve、自动扩容和前部空间复用。

### 展开说

可以这样讲：

> 我在网络层实现了一个 socket-agnostic `Buffer`，用读写索引维护可读区和可写区，支持 append、retrieve、自动扩容和前部空间复用。这个 Buffer 后续主要作为每个连接的输出缓冲区，接住非阻塞 socket 暂时写不完的数据，也为输出缓冲区高水位回压做准备；输入半包/粘包缓存由 `FrameDecoder` 自己维护。

注意不要把 Step 7 说成已经实现了 Reactor 或高并发服务器。当前只是网络层基础组件。

### 容易被追问

- Buffer 为什么需要 read/write index？
- 为什么 retrieve 返回 Status？

## 10. 面试常见追问

### Q1：Buffer 为什么需要 read/write index？

网络写入经常只完成一部分。read index 标记已经消费的位置，write index 标记可读数据末尾，这样不用每次都搬移整段内存。

### Q2：为什么 retrieve 返回 Status？

调用方可能传入超过 readable bytes 的长度。返回 Status 可以把边界错误显式暴露给测试和上层，而不是触发未定义行为。
