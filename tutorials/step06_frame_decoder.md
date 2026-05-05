# Step 6: FrameDecoder

## 1. 本 Step 目标

Step 4 已经实现单个 `Packet` 的编码和 header 解析。Step 5 已经实现 `Packet.body` 里的 TLV 字段编解码。

但 TCP 是字节流，不是消息队列。一次 `read()` 可能拿到：

- 半个 Packet。
- 一个完整 Packet。
- 多个 Packet 粘在一起。
- 前半个 Packet + 后面另一个完整 Packet。

Step 6 的目标是实现：

```text
连续 TCP 字节流 -> 0 个 / 1 个 / 多个完整 Packet
```

本 Step 实现：

- `FrameDecoder`
- `feed()`
- `bufferedBytes()`
- `hasError()`
- `reset()`

本 Step 不实现：

- socket 读写。
- `epoll`。
- 网络 `Buffer`。
- `Session`。
- TLV 字段解析。
- 业务路由。

一句话：

> FrameDecoder 只做“字节流切包”，不关心这些字节从哪个 socket 来，也不关心 Packet body 里的业务字段是什么。

## 2. 为什么需要 FrameDecoder

TCP 只保证字节顺序，不保证消息边界。

例如客户端连续发送两个 Packet：

```text
[packet1][packet2]
```

服务端可能这样收到：

```text
read1: [packet1]
read2: [packet2]
```

也可能这样收到：

```text
read1: [packet1 的前半段]
read2: [packet1 的后半段]
```

也可能这样收到：

```text
read1: [packet1][packet2]
```

甚至：

```text
read1: [packet1 的前半段]
read2: [packet1 的后半段][packet2]
```

所以不能认为一次 `read()` 就是一条消息。必须自己维护一个解码器，把连续字节流还原成完整 Packet。

## 3. FrameDecoder 的职责

代码位置：

- `include/liteim/protocol/FrameDecoder.hpp`
- `src/protocol/FrameDecoder.cpp`

核心接口：

```cpp
class FrameDecoder {
public:
    Status feed(const std::uint8_t* data, std::size_t len, std::vector<Packet>& output);
    Status feed(const std::vector<std::uint8_t>& data, std::vector<Packet>& output);

    bool hasError() const noexcept;
    std::size_t bufferedBytes() const noexcept;
    void reset();
};
```

`feed()` 的含义是：

```text
给解码器喂一段新字节，它把能解析出的完整 Packet 放进 output。
```

一次 `feed()` 可能输出：

- 0 个 Packet：数据还不够一个完整包。
- 1 个 Packet：刚好拼出一个包。
- 多个 Packet：输入里有粘包。

## 4. 内部缓冲

`FrameDecoder` 内部有：

```cpp
std::vector<std::uint8_t> buffer_;
bool error_{false};
```

`buffer_` 用来保存还没组成完整 Packet 的字节。

流程：

1. 新数据追加到 `buffer_`。
2. 如果 `buffer_` 不够 20 字节 header，就等待下一次 `feed()`。
3. 如果够 20 字节，就调用 `parseHeader()`。
4. header 合法后，根据 `body_len` 算出完整帧长度。
5. 如果 `buffer_` 还不够完整帧，就继续等待。
6. 如果够，就切出一个完整 `Packet`。
7. 从 `buffer_` 删除已经消费的字节。
8. 继续尝试解析下一个 Packet。

## 5. error 状态

如果 header 非法，例如：

- magic 错。
- version 错。
- body_len 超过 1MB。

`FrameDecoder` 会进入 error 状态：

```cpp
error_ = true;
```

进入 error 状态后，后续 `feed()` 会直接返回错误：

```text
frame decoder is in error state
```

为什么不继续解析？

因为 TCP 字节流一旦出现非法 header，后面的字节边界已经不可信。继续猜边界容易把错误数据当成正常 Packet。更合理的做法是让上层 `Session` 关闭连接。

测试里也提供了 `reset()`，方便单元测试复用 decoder。真实网络连接里通常是直接关闭连接。

## 6. 和 Packet / TlvCodec 的边界

三个模块的关系：

```text
FrameDecoder
  -> 负责 TCP 字节流切出完整 Packet

Packet
  -> 负责单个 Packet header 编码/解析/校验

TlvCodec
  -> 负责 Packet body 里的字段编解码
```

所以：

- `FrameDecoder` 会调用 `parseHeader()`。
- `FrameDecoder` 不调用 `parseTlvMap()`。
- `FrameDecoder` 不知道 `Username`、`MessageText`、`GroupId`。
- `FrameDecoder` 不读 socket，只处理传进来的字节数组。

这个边界很重要。后续 `Session` 从 socket 读到字节后，会把字节交给 `FrameDecoder`；拿到完整 `Packet` 后，再交给 `MessageRouter` 或业务层。

## 7. 测试说明

Step 6 新增：

```text
tests/protocol/frame_decoder_test.cpp
```

核心测试：

- `FrameDecoderTest.CompletePacketEmitsOnePacket`
  - 一次输入一个完整包，输出一个 `Packet`。
- `FrameDecoderTest.PacketSplitAcrossFeedsEmitsAfterSecondFeed`
  - 一个包分两次输入，第一次没有输出，第二次输出完整包。
- `FrameDecoderTest.MultiplePacketsInOneFeedAreDecoded`
  - 一次输入两个包，输出两个 `Packet`。
- `FrameDecoderTest.HalfPacketThenStickyPacketAreDecodedTogether`
  - 第一次输入半包，第二次输入剩余半包和另一个完整包，输出两个 `Packet`。
- `FrameDecoderTest.InvalidMagicEntersErrorState`
  - 错误 magic 返回 `ParseError` 并进入 error 状态。
- `FrameDecoderTest.InvalidVersionEntersErrorState`
  - 错误 version 返回 `ParseError` 并进入 error 状态。
- `FrameDecoderTest.OversizedBodyLengthEntersErrorState`
  - `body_len` 超过 1MB 返回 `ParseError` 并进入 error 状态。
- `FrameDecoderTest.ErrorStateRejectsFurtherFeedUntilReset`
  - error 状态下拒绝继续解析，`reset()` 后恢复。
- `FrameDecoderTest.NullInputWithNonzeroLengthReturnsError`
  - 空指针但长度非 0 返回 `InvalidArgument`。

运行：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

## 8. 面试讲法

可以这样讲：

> TCP 是字节流，没有消息边界，所以我实现了一个 socket-agnostic 的 `FrameDecoder`。它内部维护未消费字节缓冲，每次 `feed()` 新字节后，先等够固定 20 字节 header，再根据 `body_len` 判断完整帧长度；如果数据不足就继续缓存，如果足够就输出一个或多个完整 `Packet`。错误 header 会让 decoder 进入 error 状态，后续拒绝继续解析，交给上层关闭连接。这个模块只负责流式解包，不处理 socket I/O、不解析 TLV、不做业务路由，边界清楚。

## 9. 本 Step 提交信息

```text
feat(protocol): implement tcp frame decoder
```
