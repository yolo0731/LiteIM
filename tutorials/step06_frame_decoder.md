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
    Status feed(const Byte* data, std::size_t len, std::vector<Packet>& output);
    Status feed(const Bytes& data, std::vector<Packet>& output);

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

## FrameDecoder.hpp 接口说明

`FrameDecoder` 是 socket 无关的 TCP 切包器。

public 接口：

- `feed(const Byte* data, std::size_t len, std::vector<Packet>& output)` 接收一段新字节，清空并填充 `output`。成功返回 `Status::ok()`；输入指针为空但长度非 0 返回 `InvalidArgument`；header 解析失败会进入 error 状态并返回对应 `ParseError`。
- `feed(const Bytes& data, output)` 是字节数组版本，语义相同。
- `hasError()` 查询解码器是否已经进入错误状态。
- `bufferedBytes()` 返回内部尚未组成完整 Packet 的字节数。
- `reset()` 清空缓存并退出 error 状态，通常只在测试或明确重新同步协议时使用。

关键 private 成员：

- `buffer_` 保存跨多次 `feed()` 的未消费字节，是处理 TCP 半包的核心。
- `error_` 记录不可恢复解析错误。进入 error 后，后续 `feed()` 会直接返回错误，避免继续在错位字节流上解析。

内部 helper 来自 Packet 层：

- `parseHeader()` 读取前 20 字节。
- `validateHeader()` 间接校验 magic、version 和 body 长度。

线程和所有权边界：

- 一个 `FrameDecoder` 实例应归属于一个连接的 owner loop，不跨多个连接共享。
- `feed()` 修改内部 `buffer_` 和 `error_`，同一个实例不能多线程并发调用。
- 输出的 `Packet` 按值放进调用方 vector，解码器不持有回调或 fd。

## FrameDecoder 的作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

`FrameDecoder` 解决 TCP 半包和粘包。`Session::handleRead()` 每次从 socket 读到一段字节后直接喂给 `FrameDecoder::feed()`；decoder 内部保存跨 read 事件的残留字节，一次可以输出 0 个、1 个或多个完整 `Packet`。

### 2. 上下层调用连接

```text
Session::handleRead()
    -> read(fd, stack buffer)
    -> FrameDecoder::feed(bytes)
    -> parseHeader() / Packet
    -> vector<Packet>
    -> Session MessageCallback
```

上游是 `Session` 的 socket 读事件，下游是 Step 4 `Packet` 解析和上层 message callback。

### 3. 整体运行链路

1. `EPOLLIN` 到来后，`Session` 循环 `read()` 到 `EAGAIN`。
2. 每次读到 `n > 0` 的字节就调用 [FrameDecoder::feed()](../src/protocol/FrameDecoder.cpp#L9)。
3. `feed()` 把新字节追加到内部 `buffer_`。
4. 只要 `buffer_` 至少有 20 字节，就调用 `parseHeader()`。
5. 如果 header 合法但 body 不完整，保留缓存并返回成功。
6. 如果完整，就构造 `Packet`、删除已消费字节，并继续尝试解析后续粘包。
7. `Session` 对输出的每个 Packet 刷新活跃时间并调用 message callback。

### 4. 自身内部运行流程

整体可以看成 4 步：拒绝错误状态、追加新字节、循环切包、维护缓存。

核心成员职责：

- `buffer_` 保存跨多次 read 的累计字节流。
- `error_` 表示已经遇到协议错位；置位后后续 feed 直接失败。
- `output` 是本次 feed 解析出的完整 Packet 列表，由调用方传入。

核心函数流程：

- [feed(const Byte*, size_t, vector<Packet>&)](../src/protocol/FrameDecoder.cpp#L9)：主解析入口。
- [feed(const Bytes&, vector<Packet>&)](../src/protocol/FrameDecoder.cpp#L48)：轻量转调，方便测试和上层用 `Bytes`。
- [bufferedBytes()](../src/protocol/FrameDecoder.cpp#L56)：暴露当前残留字节数，主要用于测试。
- [reset()](../src/protocol/FrameDecoder.cpp#L60)：清空缓存并清 error，生产连接通常关闭而不是重同步。

`feed()` 可以理解成“追加字节，然后尽量切完整 Packet”：

```text
socket 新读到的 Bytes
    ↓
追加进 FrameDecoder 内部缓存
    ↓
尝试解析固定 20 字节 header
    ↓
根据 body_len 判断完整帧是否已经到齐
    ↓
到齐的帧转换成 Packet 输出
    ↓
未到齐的半包继续留在内部缓存
```

如果 header 校验失败，`FrameDecoder` 进入错误状态，后续输入都会被拒绝直到 `reset()`；如果只是 body 没收齐，它不会报错，而是等待下一次 `feed()` 补齐。

### 5. 该项目代码在实际应用中的具体数据例子

假设 Alice 的客户端发送一个 `seq_id=7`、body 58 字节的私聊 `Packet`。第一次 `read()` 只收到 10 字节 header，`FrameDecoder::bufferedBytes()` 变成 10，输出 0 个 Packet；第二次收到剩余 68 字节后，decoder 拼出完整 Packet，输出 1 个请求，后续 `Session` 才刷新 `last_active_time` 并把消息交给业务回调。

## 4. 内部缓冲

`FrameDecoder` 内部有：

```cpp
Bytes buffer_;
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

## 面试常见追问

### Q1：FrameDecoder 为什么不直接解析 TLV？

它只负责 TCP 字节流到 Packet 的边界恢复。TLV 属于业务 body 解析，分开后 Session 可以先确认 Packet 完整合法，再交给更高层解释字段。

### Q2：错误 header 后为什么进入 error 状态？

协议魔数、版本或长度已经非法时，继续从同一字节流猜边界很容易误解析。进入 error 状态并让上层关闭连接更简单，也更安全。
