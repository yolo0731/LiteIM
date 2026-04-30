# Step 3：实现 FrameDecoder

本步骤目标：实现 TCP 字节流解包器 `FrameDecoder`，让服务端能够从连续字节流中解析出一条或多条完整 `Packet`。

Step 2 定义了“一条消息长什么样”；Step 3 解决“从 TCP 字节流里怎么切出一条条消息”。

## 1. 这一步要解决什么问题

TCP 是字节流，不保留消息边界。

也就是说，客户端发送两条消息：

```text
Packet A
Packet B
```

服务端不一定按这两条消息分别读到。

可能出现：

```text
情况 1：只读到 Packet A 的前半部分
情况 2：一次读到 Packet A + Packet B
情况 3：读到 Packet A 的后半部分 + Packet B 的前半部分
```

这就是：

- 半包：一条消息分多次收到。
- 粘包：多条消息一次收到。
- 半包 + 粘包混合：前一条、后一条消息边界交错在多次 read 中。

`FrameDecoder` 的任务就是处理这些情况。

## 2. FrameDecoder 的职责边界

`FrameDecoder` 只处理字节到 `Packet` 的转换。

它不做：

- 不调用 `read()`。
- 不调用 `write()`。
- 不管理 fd。
- 不处理登录、私聊、群聊。
- 不访问数据库。

它只做：

```text
输入：一段新收到的字节
内部：追加到 buffer_
判断：是否有完整 Header
判断：是否有完整 Body
输出：0 个、1 个或多个 Packet
```

这样设计的好处：

- 协议解析可以单独测试。
- 后续 `Session` 只负责网络读写。
- 业务层只处理已经完整解析出来的 `Packet`。

## 3. FrameDecoder 的接口

文件：

```text
server/protocol/FrameDecoder.hpp
```

核心接口：

```cpp
class FrameDecoder {
public:
    std::vector<Packet> feed(const char* data, std::size_t len);

    bool hasError() const;
    const std::string& errorMessage() const;
    std::size_t bufferedBytes() const;
    void reset();
};
```

### feed()

`feed()` 是核心函数。

它的语义是：

```text
给我一段新收到的字节，我尽可能解析出完整 Packet。
```

它可能返回：

- 空 vector：数据不够，还不能组成完整包。
- 一个 Packet：刚好解析出一条完整消息。
- 多个 Packet：一次输入里有多个完整包。

### hasError()

用于判断解码器是否进入错误状态。

错误状态一般来自非法 Header：

- magic 错误。
- version 错误。
- body_len 超过上限。

### errorMessage()

返回当前错误原因。

这个接口主要用于日志和调试。例如后续 `Session` 收到非法包时，可以记录：

```text
invalid packet header
```

第一版错误信息比较简单，后续可以细化成错误码。

### bufferedBytes()

返回当前内部缓冲区还缓存了多少字节。

它主要用于：

- 单元测试确认半包确实被保留。
- 调试时观察连接上是否积压了未解析数据。

### reset()

清空内部 buffer 和错误状态。

第一版设计里，如果连接收到非法包，后续更常见的处理是直接关闭连接。`reset()` 主要用于测试和未来复用场景。

注意：`FrameDecoder` 的输入是已经从 socket 读出来的字节。它自己不拥有 fd，也不决定连接是否关闭；后续 `Session` 会根据 `hasError()` 决定是否关闭连接。

## 4. 核心实现逻辑

文件：

```text
server/protocol/FrameDecoder.cpp
```

核心流程：

```text
feed(data)
  ↓
追加到 buffer_
  ↓
buffer_ 不足 16 字节？
  是：返回空 vector，等待更多数据
  否：解析 Header
  ↓
Header 非法？
  是：进入 error 状态
  否：计算 frame_size = 16 + body_len
  ↓
buffer_ 不足 frame_size？
  是：返回空 vector，等待更多 Body
  否：构造 Packet，放入结果
  ↓
从 buffer_ 删除已解析字节
  ↓
继续循环解析下一个包
```

为什么要循环？

因为一次 `feed()` 可能带来多个完整包。比如：

```text
buffer_ = Packet A + Packet B
```

只解析一个包就停，会导致 Packet B 明明已经完整，却要等下一次网络事件才被处理。

### 4.1 feed() 的实现思路

`feed()` 是 `FrameDecoder` 最核心的函数。

它做四件事：

1. 如果 decoder 已经处于 error 状态，直接返回空结果，不继续解析。
2. 把本次传入的新字节追加到内部 `buffer_`。
3. 只要 `buffer_` 里至少有 16 字节，就尝试解析 Header。
4. 如果 Header 和 Body 都完整，就构造 `Packet` 并从 `buffer_` 里删除已消费字节。

它的输入是 `const char* data` 和 `std::size_t len`。

它的输出是 `std::vector<Packet>`，因为一次输入可能解析出多个完整包。

边界情况：

- `data == nullptr && len > 0`：进入 error 状态。
- `len == 0`：不追加新字节，但仍可以尝试解析已经缓存的数据。
- Header 不完整：返回空 vector，不报错。
- Body 不完整：返回空 vector，不报错。
- Header 非法：进入 error 状态。

### 4.2 setError() 的实现思路

`setError()` 是私有辅助函数，只在内部调用。

它做三件事：

1. 清空 `buffer_`，避免继续使用已经失去同步的字节流。
2. 设置 `has_error_ = true`。
3. 保存错误说明到 `error_message_`。

它是私有函数，因为外部不应该随意把 decoder 标成错误。外部只需要通过 `hasError()` 查询状态。

### 4.3 reset() 的实现思路

`reset()` 用来恢复 decoder。

它会：

- 清空 `buffer_`。
- 把 `has_error_` 设回 false。
- 清空 `error_message_`。

在真实连接里，收到非法包后一般会直接关闭连接；但保留 `reset()` 对测试很有用，也方便未来复用 decoder。

## 5. 半包处理

### 5.1 Header 半包

如果只收到 10 字节：

```text
buffer_.size() = 10
kPacketHeaderSize = 16
```

此时连 Header 都不完整，不能解析。

处理方式：

- 不报错。
- 不返回 Packet。
- 把 10 字节留在 `buffer_`。

### 5.2 Body 半包

如果 Header 已经完整，并且 Header 里写：

```text
body_len = 20
```

但当前只收到：

```text
16 字节 Header + 3 字节 Body
```

此时也不能返回 Packet。

处理方式：

- 不报错。
- 不返回 Packet。
- 把 Header 和 3 字节 Body 都留在 `buffer_`。

下一次 `feed()` 收到剩余 17 字节后，再返回完整 Packet。

## 6. 粘包处理

如果一次收到：

```text
Packet A + Packet B
```

`FrameDecoder` 会：

1. 解析 Packet A。
2. 删除 Packet A 的字节。
3. 继续解析 Packet B。
4. 返回两个 Packet。

测试里对应：

```text
frame decoder two complete packets
```

## 7. 半包 + 粘包混合

更真实的情况是：

第一次收到：

```text
完整 Packet A + Packet B 的前 4 字节
```

第二次收到：

```text
Packet B 剩余字节
```

这要求 `FrameDecoder`：

- 第一次能返回 Packet A。
- 同时保留 Packet B 的前 4 字节。
- 第二次补齐后返回 Packet B。

测试里对应：

```text
frame decoder partial and sticky mixed
```

这是面试里最值得讲的测试之一。

## 8. 错误状态设计

如果 Header 已经够 16 字节，但字段非法：

```text
magic 错误
version 错误
body_len 超过 1MB
```

说明当前连接发来的数据不符合 LiteIM 协议。

第一版处理：

- 清空 `buffer_`。
- 设置 `has_error_ = true`。
- 保存错误信息。
- 后续 `feed()` 直接返回空。

为什么不尝试跳过坏字节继续找下一个 magic？

因为第一版项目目标是简洁可靠，不做复杂协议恢复。真实服务端通常会认为这类连接异常，直接关闭连接。

## 9. 本步骤新增文件

```text
server/protocol/FrameDecoder.hpp
server/protocol/FrameDecoder.cpp
tests/TestUtil.hpp
tests/test_main.cpp
tests/test_frame_decoder.cpp
tutorials/step03_frame_decoder.md
```

修改文件：

```text
server/CMakeLists.txt
tests/CMakeLists.txt
tests/test_protocol.cpp
docs/protocol.md
tutorials/README.md
```

说明：

- `tests/TestUtil.hpp`：轻量测试工具，提供 `expect()` 和 `runTests()`。
- `tests/test_main.cpp`：统一测试入口，后续新增测试文件不用再写多个 `main()`。
- `tests/test_protocol.cpp`：从原来的自带 `main()` 改成返回测试用例列表。

## 10. 测试覆盖

`tests/test_frame_decoder.cpp` 覆盖：

1. 正常完整包。
2. 空 Body 包。
3. 只收到半个 Header。
4. 收到完整 Header 但 Body 不完整。
5. 一次收到两个完整包。
6. 第一次 `feed()` 半包，第二次 `feed()` 剩余部分。
7. 半包 + 粘包混合。
8. 错误 magic。
9. `body_len` 超过 1MB。
10. error 后 `reset()` 恢复。

这些测试的大概思路：

- 完整包测试：验证正常数据能被解析成一个 `Packet`，并且 Header 和 Body 字段一致。
- 空 Body 测试：验证 `body_len = 0` 时也能解析，心跳包后续会用到这种情况。
- Header 半包测试：只输入不足 16 字节，确认 decoder 不报错、不返回包，而是缓存起来。
- Body 半包测试：输入完整 Header 和部分 Body，确认 decoder 等待剩余 Body。
- 粘包测试：一次输入两个完整包，确认 `feed()` 能返回两个 `Packet`。
- 分次 feed 测试：第一次输入半包，第二次补齐，确认内部 buffer 能跨调用保存状态。
- 半包 + 粘包混合测试：模拟更真实的 TCP 读入边界，确认已完整的包能先返回，未完整的包继续缓存。
- 错误包测试：手动破坏 magic 或 body_len，确认 decoder 进入 error 状态。
- reset 测试：确认 error 后可以通过 `reset()` 清空状态，便于测试和未来复用。

测试通过说明：

```text
FrameDecoder 已经能处理 TCP 字节流中最核心的边界问题：半包、粘包、混合场景和非法 Header。
```

## 11. 编译和测试

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
[PASS] frame decoder complete packet
[PASS] frame decoder empty body packet
[PASS] frame decoder partial header
[PASS] frame decoder complete header incomplete body
[PASS] frame decoder two complete packets
[PASS] frame decoder partial then remaining
[PASS] frame decoder partial and sticky mixed
[PASS] frame decoder wrong magic sets error
[PASS] frame decoder oversized body_len sets error
[PASS] frame decoder reset after error
```

## 12. 面试时怎么讲

可以这样说：

> TCP 是字节流，不保留应用层消息边界，所以我把协议解析拆成了两层。`Packet` 定义单条消息格式，`FrameDecoder` 负责从连续字节流里切出完整 Packet。FrameDecoder 内部维护 buffer，每次 feed 新字节后先判断 Header 是否完整，再根据 body_len 判断 Body 是否完整。它支持半包、粘包以及半包加粘包混合；如果 magic、version 或 body_len 非法，会进入 error 状态，后续连接可以直接关闭。

面试展开点：

- 为什么 Header 固定长度。
- 为什么 Body 长度从 Header 读取。
- 为什么不能假设一次 `read()` 就是一条完整消息。
- 为什么错误 Header 后不继续猜测边界。
- 为什么 FrameDecoder 不直接操作 socket。

### 12.1 讲解思路

面试时建议按这个顺序讲：

1. 先说明 TCP 的问题：`read()` 读到的是字节，不是消息。
2. 再说明协议基础：Step 2 已经定义了 16 字节 Header 和 `body_len`。
3. 再说明 decoder 的核心：内部维护 `buffer_`，每次 `feed()` 都追加新字节并循环解析。
4. 再说明三种场景：半包继续缓存，粘包循环解析，非法 Header 进入 error。
5. 最后说明职责边界：`FrameDecoder` 不操作 socket，后续 `Session` 负责读写和连接关闭。

### 12.2 面试中容易被问到的问题

**Q1：为什么 `feed()` 返回 `vector<Packet>`，不是单个 Packet？**

因为一次 `read()` 可能读到多个完整包。如果只返回一个，剩余完整包要等下一次事件才处理，会增加延迟。

**Q2：半包为什么不算错误？**

半包是 TCP 正常现象。只要 Header 或 Body 还没收齐，就继续缓存等待下一次 `feed()`。

**Q3：Header 已经 16 字节但 magic 错了，为什么算错误？**

因为这说明当前字节流不符合 LiteIM 协议。第一版没有做复杂的重同步，直接让上层关闭连接更简单可靠。

**Q4：为什么错误后不继续扫描下一个 magic？**

扫描 magic 会引入复杂的协议恢复逻辑，也可能误判 Body 中的数据。第一版项目重点是清晰可靠，非法连接直接断开。

**Q5：`FrameDecoder` 和 `Buffer` 有什么区别？**

`FrameDecoder` 是协议层缓冲，理解 Header 和 Body；`Buffer` 是网络层通用字节容器，不理解协议。

**Q6：为什么 `FrameDecoder` 不直接 `read(fd)`？**

这是职责解耦。socket 读写属于 `Session`，事件触发属于 `Channel/EventLoop`，协议解析属于 `FrameDecoder`。

**Q7：`body_len` 如果特别大会怎么样？**

`parseHeader()` 会限制最大 1MB，超过就失败，`FrameDecoder` 进入 error 状态，避免服务端缓存过大数据。

## 13. 下一步预告

Step 4 会实现：

```text
server/net/Buffer.hpp
server/net/Buffer.cpp
```

`Buffer` 是网络层输入缓冲区和输出缓冲区的基础组件。

虽然 `FrameDecoder` 当前内部用 `std::string buffer_` 保存未解析字节，但后续 `Session` 还需要一个更通用的 `Buffer` 来处理：

- socket read 缓冲。
- socket write 缓冲。
- 短写后剩余数据保留。
