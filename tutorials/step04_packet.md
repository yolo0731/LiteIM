# Step 4: Packet 编解码

## 1. 本 Step 目标

Step 3 已经定义了 `MessageType` 和 `TlvType`，但它们只是“编号”。Step 4 开始把一条消息变成可以在 TCP 连接上传输的二进制数据。

本 Step 实现：

- `PacketHeader`
- `Packet`
- `validateHeader()`
- `encodePacket()`
- `parseHeader()`

本 Step 不实现：

- TLV body 字段编解码，这属于 Step 5。
- TCP 半包 / 粘包流式解码，这属于 Step 6。
- socket、epoll、Reactor，这些属于后续网络 Step。

一句话：

> Step 4 只负责“单个 Packet 的 fixed header 怎么写进字节数组、怎么从字节数组读回来、怎么校验 header 合法”。

## 2. Packet 是什么

LiteIM 的一条网络消息分成两部分：

```text
+--------------------+----------------------+
| fixed PacketHeader | body bytes           |
+--------------------+----------------------+
| 20 bytes           | body_len bytes       |
+--------------------+----------------------+
```

`PacketHeader` 固定 20 字节，用来告诉接收方：

- 这是不是 LiteIM 协议包。
- 协议版本是多少。
- 这条消息是什么业务类型。
- 请求序列号是多少。
- 后面 body 有多少字节。

`body` 现在只是原始字节数组。等 Step 5 做完 `TlvCodec` 后，body 里面才会放 TLV 字段。

## 3. Header 字段

代码在：

- `include/liteim/protocol/Packet.hpp`
- `src/protocol/Packet.cpp`

Header 定义：

```cpp
struct PacketHeader {
    std::uint32_t magic{kPacketMagic};
    std::uint8_t version{kPacketVersion};
    std::uint8_t flags{kPacketFlagsNone};
    MessageType msg_type{MessageType::Unknown};
    std::uint64_t seq_id{0};
    std::uint32_t body_len{0};
};
```

字段含义：

| 字段 | 大小 | 作用 |
| --- | --- | --- |
| `magic` | 4 字节 | 固定 `0x4C494D31`，ASCII 是 `LIM1`，用于识别 LiteIM 协议包。 |
| `version` | 1 字节 | 当前固定为 `1`，后续协议升级时使用。 |
| `flags` | 1 字节 | 预留字段，当前固定为 `0`。 |
| `msg_type` | 2 字节 | `MessageType`，例如 `LoginRequest`、`PrivateMessagePush`。 |
| `seq_id` | 8 字节 | 请求序列号，后续客户端用它匹配 request / response。 |
| `body_len` | 4 字节 | body 字节数，最大 1MB。 |

总大小：

```text
4 + 1 + 1 + 2 + 8 + 4 = 20 bytes
```

所以代码里有：

```cpp
inline constexpr std::size_t kPacketHeaderSize = 20;
```

## 4. 为什么用网络字节序

不同 CPU 对多字节整数的内存表示可能不同。比如 `0x01020304`：

- 大端机器内存顺序：`01 02 03 04`
- 小端机器内存顺序：`04 03 02 01`

网络协议不能依赖本机内存布局，所以 LiteIM 规定：

> 多字节字段全部按网络字节序，也就是大端序写入。

因此 `Packet.cpp` 里没有直接把结构体 `memcpy` 到网络缓冲区，而是通过协议层共享的 `ByteOrder.hpp` 写：

```cpp
appendUint32BE(output, header.magic);
appendUint16BE(output, static_cast<std::uint16_t>(header.msg_type));
appendUint64BE(output, header.seq_id);
appendUint32BE(output, header.body_len);
```

这样可以避免结构体 padding、对齐方式和本机字节序影响协议格式。

Step 16 前代码清理后，Packet 和 TLV 不再各自维护一套大端读写 helper，而是统一复用：

```text
include/liteim/protocol/ByteOrder.hpp
```

## 5. `validateHeader()`

`validateHeader()` 只校验 header 级别的规则：

```cpp
Status validateHeader(const PacketHeader& header);
```

当前规则：

- `magic` 必须等于 `kPacketMagic`。
- `version` 必须等于 `kPacketVersion`。
- `body_len` 不能超过 `kMaxPacketBodyLength`，也就是 1MB。

它不检查：

- body 内容是不是合法 TLV。
- `msg_type` 对不对应某个业务 handler。
- 用户是否登录。

这些属于后续 `TlvCodec`、`MessageRouter` 和业务层。

## 6. `encodePacket()`

函数声明：

```cpp
Status encodePacket(const Packet& packet, Bytes& output);
```

职责：

1. 清空 `output`。
2. 检查 `packet.body.size()` 是否超过 1MB。
3. 复制一份 header。
4. 用 `packet.body.size()` 重新设置 `header.body_len`。
5. 校验 header。
6. 按网络字节序写入 20 字节 header。
7. 追加 body 原始字节。

关键点：

```cpp
PacketHeader header = packet.header;
header.body_len = static_cast<std::uint32_t>(packet.body.size());
```

这里故意不相信调用方传进来的 `packet.header.body_len`，因为真实长度应该以 `packet.body.size()` 为准。这样可以避免 header 里写 100 字节，但 body 实际只有 5 字节的错误。

## 7. `parseHeader()`

函数声明：

```cpp
Status parseHeader(const std::uint8_t* data, std::size_t len, PacketHeader& output);
```

职责：

1. 检查 `data != nullptr`。
2. 检查 `len >= kPacketHeaderSize`。
3. 按网络字节序读取 header 字段。
4. 调用 `validateHeader()`。
5. 成功后把结果写入 `output`。

它只解析 header，不读取 body。原因是 TCP 是字节流，可能一次只收到半个包，也可能一次收到多个包。完整的“先等 header，再等 body，再产出 Packet”会在 Step 6 的 `FrameDecoder` 里做。

## 8. CMake 变化

`src/protocol/CMakeLists.txt` 把 `Packet.cpp` 加入 `liteim_protocol`：

```cmake
add_library(liteim_protocol
    MessageType.cpp
    Packet.cpp
    Tlv.cpp
)
```

因为 `Packet` 返回 `Status` 和 `ErrorCode`，所以 `liteim_protocol` 需要链接 `liteim_base`：

```cmake
target_link_libraries(liteim_protocol
    PUBLIC
        liteim_base
)
```

`tests/CMakeLists.txt` 加入：

```cmake
protocol/packet_test.cpp
```

## 9. 测试说明

Step 4 新增 `tests/protocol/packet_test.cpp`。

核心测试：

- `PacketTest.EncodePacketThenParseHeader`
  - 编码普通 body，再解析 header，检查字段和 body 都保持一致。
- `PacketTest.EmptyBodyCanBeEncoded`
  - body 为空时，encoded 长度应该只有 20 字节。
- `PacketTest.Utf8BodyCanBeEncoded`
  - 中文和 emoji 只是 UTF-8 字节，Packet 层不解释内容，只保证字节不丢。
- `PacketTest.HeaderUsesNetworkByteOrder`
  - 检查 `magic`、`msg_type`、`seq_id`、`body_len` 的字节顺序。
- `PacketTest.InvalidMagicReturnsError`
  - 错误 magic 返回 `ParseError`。
- `PacketTest.InvalidVersionReturnsError`
  - 错误 version 返回 `ParseError`。
- `PacketTest.OversizedBodyLengthReturnsError`
  - header 里的 `body_len` 超过 1MB 返回 `ParseError`。
- `PacketTest.EncodingOversizedBodyReturnsError`
  - encode 时 body 实际超过 1MB 返回 `InvalidArgument`。
- `PacketTest.IncompleteHeaderReturnsError`
  - 输入不足 20 字节返回 `ParseError`。
- `PacketTest.NullHeaderDataReturnsError`
  - 空指针返回 `InvalidArgument`。

运行：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

## 10. 面试讲法

可以这样讲：

> 我在协议层实现了固定 20 字节的 Packet header，包括 magic、version、flags、msg_type、seq_id 和 body_len。编码时不直接 memcpy 结构体，而是手动按网络字节序写入，避免 CPU 字节序、结构体 padding 和对齐差异影响 wire format。解析时先校验 magic、version 和 body_len 上限，非法包直接返回错误。Packet 层只处理 header 和原始 body 字节，TLV body 编解码和 TCP 半包 / 粘包处理分别放到后续独立模块里，边界比较清楚。

## 11. 本 Step 提交信息

```text
feat(protocol): add packet encoding and header validation
```
