# Step 7：实现 Buffer

本 Step 进入网络层，但还不写 socket、epoll、Reactor 或 `Session`。

目标是先实现一个可复用的字节缓冲区 `Buffer`。后续每个连接都会需要输入缓冲区和输出缓冲区：

```text
socket read -> input Buffer -> FrameDecoder -> Packet
Packet / response -> output Buffer -> socket write
```

## 1. 为什么需要 Buffer

TCP 是字节流，`read()` 一次读到的数据不一定刚好是一条完整消息。

如果每次读写都直接操作临时 `std::string` 或 `std::vector<char>`，后续会出现几个问题：

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

## 2. 本 Step 新增文件

```text
include/liteim/net/Buffer.hpp
src/net/Buffer.cpp
src/net/CMakeLists.txt
tests/net/buffer_test.cpp
```

同时更新：

```text
src/CMakeLists.txt
tests/CMakeLists.txt
```

生成新的库 target：

```text
liteim_net
```

## 3. Buffer 的公开接口

```cpp
class Buffer {
public:
    explicit Buffer(std::size_t initial_size = kDefaultBufferSize);

    std::size_t readableBytes() const noexcept;
    std::size_t writableBytes() const noexcept;
    const char* peek() const noexcept;

    Status append(const char* data, std::size_t len);
    Status append(const std::uint8_t* data, std::size_t len);
    void appendString(std::string_view value);

    Status retrieve(std::size_t len);
    void retrieveAll() noexcept;
    std::string retrieveAllAsString();

    void ensureWritableBytes(std::size_t len);
};
```

接口含义：

- `readableBytes()`：当前能被上层读取的字节数。
- `writableBytes()`：当前还能直接写入的剩余空间。
- `peek()`：返回可读区域起始地址，不移动读指针。
- `append()`：追加字节数据，空间不足时自动扩容或整理空间。
- `appendString()`：追加字符串，方便测试和后续文本 payload。
- `retrieve()`：消费指定长度的可读数据。
- `retrieveAll()`：清空当前可读区域。
- `retrieveAllAsString()`：取出所有可读数据并清空。
- `ensureWritableBytes()`：保证至少有指定长度的可写空间。

## 4. retrieve 为什么返回 Status

`retrieve(len)` 如果传入的 `len` 超过 `readableBytes()`，说明调用方想消费不存在的数据。

这里不使用 `assert`，而是返回：

```cpp
Status::error(ErrorCode::InvalidArgument, ...)
```

原因是服务端会面对网络输入、客户端断开、协议错误等情况。底层工具函数应该把错误交给上层决定如何关闭连接或记录日志，而不是直接让进程崩溃。

## 5. 自动扩容和空间复用

`ensureWritableBytes(len)` 的逻辑分两步：

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
ensureWritableBytes(6) 会把 "ef" 移到开头：

[e f _ _ _ _ _ _]
 ^ read_index_
     ^ write_index_
```

这样可以减少扩容次数。

## 6. 本 Step 不做什么

本 Step 不实现：

- `SocketUtil`
- `Epoller`
- `Channel`
- `EventLoop`
- `Session`
- `TcpServer`
- 慢客户端高水位策略

`Buffer` 只是这些模块后续会使用的基础组件。

## 7. 测试清单

新增测试文件：

```text
tests/net/buffer_test.cpp
```

测试用例：

```cpp
TEST(BufferTest, DefaultBufferHasNoReadableBytes)
TEST(BufferTest, AppendIncreasesReadableBytes)
TEST(BufferTest, AppendStringStoresReadableData)
TEST(BufferTest, AppendUint8PointerStoresBytes)
TEST(BufferTest, RetrieveAdvancesReadIndex)
TEST(BufferTest, RetrieveAllResetsBuffer)
TEST(BufferTest, RetrieveAllAsStringReturnsReadableDataAndClearsBuffer)
TEST(BufferTest, EnsureWritableBytesExpandsWhenNeeded)
TEST(BufferTest, EnsureWritableBytesCompactsReadableDataBeforeExpanding)
TEST(BufferTest, AppendExpandsAndPreservesExistingData)
TEST(BufferTest, RetrievePastReadableBytesReturnsError)
TEST(BufferTest, NullAppendWithNonzeroLengthReturnsError)
TEST(BufferTest, NullAppendWithZeroLengthIsOk)
```

这些测试覆盖：

- 初始状态。
- 字节追加。
- 字符串追加。
- `std::uint8_t*` 输入。
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
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

当前 Step 7 预期 CTest 通过 67 个测试，其中 13 个是新增的 `BufferTest`。

## 9. 面试讲法

可以这样讲：

> 我在网络层实现了一个 socket-agnostic `Buffer`，用读写索引维护可读区和可写区，支持 append、retrieve、自动扩容和前部空间复用。这个 Buffer 后续会作为每个连接的输入缓冲区和输出缓冲区，配合 `FrameDecoder` 解决 TCP 半包/粘包，也为输出缓冲区高水位回压做准备。

注意不要把 Step 7 说成已经实现了 Reactor 或高并发服务器。当前只是网络层基础组件。

## 10. 提交信息

```text
feat(net): add buffer abstraction
```
