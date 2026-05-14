# Step 4：Packet 编解码

## 0. 本 Step 结论

- 目标：本 Step 目标。
- 前置依赖：依赖 Step 0-3 已建立的工程、协议或运行时基础。
- 主要交付：`Packet 编解码` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不提前实现后续 Step 的业务能力

## 1. 为什么需要这个 Step

### 本 Step 目标

Step 3 已经定义了 `MessageType` 和 `TlvType`，但它们只是“编号”。Step 4 开始把一条消息变成可以在 TCP 连接上传输的二进制数据。

本 Step 实现：

- `PacketHeader`
- `Packet`
- `validateHeader()`
- `encodePacket()`
- `parseHeader()`

- TLV body 字段编解码，这属于 Step 5。
- TCP 半包 / 粘包流式解码，这属于 Step 6。
- socket、epoll、Reactor，这些属于后续网络 Step。

一句话：

> Step 4 只负责“单个 Packet 的 fixed header 怎么写进字节数组、怎么从字节数组读回来、怎么校验 header 合法”。

### Packet 是什么

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

### 为什么用网络字节序

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

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `Packet 编解码` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不提前实现后续 Step 的业务能力
- 不改变已经定义好的模块边界
- 不把阻塞 I/O 放进 Reactor I/O 线程

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/protocol/Packet.hpp` | 新增 | 定义固定 20 字节 header、Packet 和编解码接口 |
| `src/protocol/Packet.cpp` | 新增 | 实现 header 校验、网络字节序编码和 header 解析 |
| `src/protocol/CMakeLists.txt` | 修改 | 把 Packet 实现加入 `liteim_protocol` |
| `tests/protocol/packet_test.cpp` | 新增 | 覆盖 header 编码、校验和异常输入 |
| `README.md` | 更新 | 记录 Packet 外层协议 |
| `tutorials/step04_packet.md` | 新增 | 讲解 Packet wire shape |
| `task_plan.md / findings.md / progress.md` | 更新 | 记录 Step 4 过程和验证结果 |

## 4. 核心接口与契约

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

`Packet.hpp` 是 LiteIM 外层包格式的契约。

常量：

- `kPacketMagic = 0x4C494D31`，字节含义是 `"LIM1"`，用于快速识别协议。
- `kPacketVersion = 1`，当前协议版本。
- `kPacketFlagsNone = 0`，flags 当前预留。
- `kPacketHeaderSize = 20`，固定 header 长度。
- `kMaxPacketBodyLength = 1024 * 1024`，单包 body 最大 1MB，防止异常客户端制造超大包。

`PacketHeader` 字段：

- `magic`、`version`、`flags` 是协议识别和扩展字段。
- `msg_type` 是 `MessageType`，决定业务路由。
- `seq_id` 用于 request/response 匹配。
- `body_len` 是 body 字节数，由编码函数按 `body.size()` 填充。

`Packet` 字段：

- `header` 保存外层元信息。
- `body` 是 `liteim::Bytes`，当前由 Step 5 的 TLV 编码填充。

函数：

- `validateHeader(header)` 校验 magic、version 和 body 长度，失败返回 `ParseError`。
- `encodePacket(packet, output)` 先清空 `output`，再校验并按大端序写 header，最后追加 body。body 超过上限返回 `InvalidArgument`。
- `parseHeader(data, len, output)` 从至少 20 字节中解析 header；`data == nullptr` 返回 `InvalidArgument`，长度不足或字段非法返回 `ParseError`。

关键内部 helper 在 `ByteOrder.hpp`：

- `appendUint16BE/appendUint32BE/appendUint64BE` 按网络字节序写整数。
- `readUint16BE/readUint32BE/readUint64BE` 按网络字节序读整数。
- 这些 helper 避免直接 `memcpy` C++ struct 导致 padding 和大小端问题。

线程和所有权边界：

- Packet 编解码函数只操作调用方传入的内存，不持有 fd、线程或全局状态。
- `output` 由调用方拥有；函数成功后保存完整 wire bytes，失败时不代表连接必须关闭，由上层决定。
- `body` 只表达原始字节，不在本 Step 解析 TLV。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

`Packet` 是 LiteIM TCP 连接上真正发送和接收的完整协议包：业务层把 TLV body 放进 `Packet.body`，`Session::sendPacket()` 调用 `encodePacket()` 变成网络字节；`Session::handleRead()` 读到 TCP 字节后由 `FrameDecoder` 调用 `parseHeader()` 切出完整 Packet。

### 2. 上下层调用连接

```text
发送方向：业务字段
    -> TlvCodec 生成 body
    -> Packet(header + body)
    -> encodePacket()
    -> Session output Buffer
    -> TCP write

接收方向：TCP read bytes
    -> FrameDecoder::feed()
    -> parseHeader() / validateHeader()
    -> Packet
    -> Session MessageCallback
```

上游是业务字段和 `TlvCodec`，下游是 TCP 字节流、`FrameDecoder` 和 `Session` 回调。

### 3. 整体运行链路

1. 发送方设置 `PacketHeader.msg_type`、`seq_id` 和 body。
2. [encodePacket()](../src/protocol/Packet.cpp) 根据 `body.size()` 重写 `body_len`。
3. `encodePacket()` 校验 magic、version 和最大 body 长度。
4. `encodePacket()` 按网络字节序写入固定 20 字节 header，再追加 body。
5. 接收方先积累到至少 20 字节。
6. [parseHeader()](../src/protocol/Packet.cpp) 从字节流读出 header 并校验。
7. `FrameDecoder` 根据 `body_len` 判断是否已经收到完整包。
8. 完整后构造 `Packet` 并交给 `Session` 上层 callback。

### 4. 自身内部运行流程

整体可以看成 3 步：校验 header、编码完整包、解析固定 header。

核心成员和常量职责：

- `PacketHeader` 保存 magic、version、flags、`msg_type`、`seq_id` 和 `body_len`。
- `Packet.body` 保存 TLV body，不理解字段含义。
- `kPacketHeaderSize` 固定为 20 字节，让 TCP 切包有明确边界。
- `kMaxPacketBodyLength` 限制 body，避免异常包撑爆内存和输出缓冲。
- `Bytes` 是统一 wire bytes 容器。

核心函数流程：

- [validateHeader()](../src/protocol/Packet.cpp)：检查 magic、version 和 body 上限。
- [encodePacket()](../src/protocol/Packet.cpp)：清空输出、重算 body_len、校验 header、按大端序写字段、追加 body。
- [parseHeader()](../src/protocol/Packet.cpp)：检查指针和长度，从固定偏移读字段，再调用 `validateHeader()`。

`encodePacket(packet, output)` 可以理解成“业务 Packet 到 wire bytes”：

```text
Packet.header + Packet.body
    ↓
按 body.size() 刷新 header.body_len
    ↓
validateHeader() 检查 magic / version / body 上限
    ↓
按网络字节序写入 20 字节 header
    ↓
追加 TLV body，形成可发送的 Bytes
```

`parseHeader(data, len, output)` 可以理解成“TCP bytes 到 header”：

```text
TCP 字节缓存
    ↓
确认至少有固定 20 字节 header
    ↓
按固定偏移读取 magic / version / flags / msg_type / seq_id / body_len
    ↓
validateHeader() 检查协议边界
    ↓
交给 FrameDecoder 判断 body 是否完整
```

这两个函数只处理 Packet 外壳：`encodePacket()` 不理解 TLV 字段含义，`parseHeader()` 也不消费 body；完整包边界由 `FrameDecoder` 继续判断。

### 5. 该项目代码在实际应用中的具体数据例子

一条私聊请求可以表示成：`magic=0x4C494D31`、`version=1`、`msg_type=PRIVATE_MESSAGE_REQUEST`、`seq_id=7`、`body_len=58`。body 里再用 TLV 保存 `sender_id=1001`、`receiver_id=1002`、`conversation_id=10011002` 和文本 `hello bob`。`encodePacket()` 把这些字段写成网络字节序；`FrameDecoder` 收够 20 字节 header 和 58 字节 body 后才产出完整 `Packet`。

## 6. 关键实现点

### `validateHeader()`

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

### `encodePacket()`

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

### `parseHeader()`

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

### CMake 变化

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

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `Packet 编解码` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

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

## 8. 验证命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 9. 面试表达

### 一句话

我在协议层实现了固定 20 字节的 Packet header，包括 magic、version、flags、msg_type、seq_id 和 body_len。

### 展开说

可以这样讲：

> 我在协议层实现了固定 20 字节的 Packet header，包括 magic、version、flags、msg_type、seq_id 和 body_len。编码时不直接 memcpy 结构体，而是手动按网络字节序写入，避免 CPU 字节序、结构体 padding 和对齐差异影响 wire format。解析时先校验 magic、version 和 body_len 上限，非法包直接返回错误。Packet 层只处理 header 和原始 body 字节，TLV body 编解码和 TCP 半包 / 粘包处理分别放到后续独立模块里，边界比较清楚。

### 容易被追问

- 为什么 Packet header 固定 20 字节？
- 为什么 body 不直接用字符串？

## 10. 面试常见追问

### Q1：为什么 Packet header 固定 20 字节？

固定 header 让 FrameDecoder 可以先稳定判断 magic、version、type、seq_id 和 body_len，再决定是否继续等待 body。这样 TCP 半包和粘包都能按同一套规则处理。

### Q2：为什么 body 不直接用字符串？

body 后续要承载 user_id、conversation_id、message text、错误码等多种字段。TLV 比单字符串更适合逐步扩展协议，同时还能保持二进制边界清楚。
