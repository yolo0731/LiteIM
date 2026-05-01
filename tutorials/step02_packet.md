# Step 2：实现 Packet 编解码

本步骤目标：实现 LiteIM 的协议基础结构，让项目能够把一个业务消息编码成 TCP 可传输的字节串，并能从字节串中解析出 Header。

这一阶段只做协议外壳，不做网络 I/O，不做 epoll，不做登录和聊天业务。

## 1. 这一步要解决什么问题

TCP 是字节流协议。

这句话的意思是：TCP 只保证字节按顺序到达，但不会告诉应用层“哪几个字节是一条完整消息”。

举例：

```text
客户端连续发送：
消息 A
消息 B

服务端 read() 时可能读到：
A 的一半
或 A + B 一起
或 A 的后半段 + B 的前半段
```

这就是常说的：

- 半包：一条消息没有一次读完整。
- 粘包：多条消息被一次读出来。

Step 2 先不处理半包和粘包。Step 2 只定义“一条消息长什么样”。

后续 Step 3 的 `FrameDecoder` 才负责从连续 TCP 字节流中切出完整消息。

## 2. LiteIM 的协议格式

LiteIM 第一版协议采用：

```text
固定长度 Header + JSON Body
```

布局如下：

```text
+------------+------------+------------+------------+------------+----------------+
| magic(4B)  | version(2B)| type(2B)   | seq_id(4B) | body_len(4B)| JSON Body      |
+------------+------------+------------+------------+------------+----------------+
|                          固定 16 字节 Header                       | body_len 字节 |
+-------------------------------------------------------------------------+----------+
```

Header 固定 16 字节。

字段含义：

- `magic`：协议魔数，固定为 `0x4C494D31`，表示 `LIM1`。
- `version`：协议版本，当前固定为 `1`。
- `msg_type`：消息类型，例如登录请求、私聊请求、心跳请求。
- `seq_id`：请求序号，用于后续请求和响应对应。
- `body_len`：JSON Body 的字节长度。

为什么需要 `body_len`？

因为服务端读到 Header 后，就知道后面还要读多少字节才是一条完整消息。

## 3. 为什么不能直接发送 C++ struct

你可能会想：

```cpp
struct PacketHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t msg_type;
    uint32_t seq_id;
    uint32_t body_len;
};
```

然后直接：

```cpp
send(fd, &header, sizeof(header), 0);
```

这个做法不推荐。

原因：

1. C++ struct 可能有内存对齐和 padding。
2. 不同机器可能有不同字节序。
3. 后续 Python BotClient 要复用同一套协议，Python 不认识 C++ struct 的内存布局。

所以本项目采用逐字段编码：

```text
uint32_t -> htonl -> 写入 4 字节
uint16_t -> htons -> 写入 2 字节
```

解析时再反过来：

```text
4 字节 -> ntohl -> uint32_t
2 字节 -> ntohs -> uint16_t
```

这样 C++、Qt、Python 都能按同一套规则读写协议。

## 4. 本步骤新增文件

```text
include/liteim/protocol/MessageType.hpp
include/liteim/protocol/Packet.hpp
src/protocol/Packet.cpp
tests/test_protocol.cpp
```

并修改：

```text
server/CMakeLists.txt
tests/CMakeLists.txt
docs/protocol.md
tutorials/README.md
```

删除：

```text
tests/test_smoke.cpp
```

原因：Step 1 的 smoke test 只是构建占位。Step 2 开始有了真实协议测试，所以用 `test_protocol.cpp` 替代。

## 5. MessageType.hpp 讲解

文件：

```text
include/liteim/protocol/MessageType.hpp
```

它定义所有消息类型：

```cpp
enum class MsgType : std::uint16_t {
    REGISTER_REQ = 1001,
    REGISTER_RESP = 1002,
    LOGIN_REQ = 1003,
    LOGIN_RESP = 1004,
    FRIEND_LIST_REQ = 1101,
    FRIEND_LIST_RESP = 1102,
    PRIVATE_MSG_REQ = 2001,
    PRIVATE_MSG_PUSH = 2002,
    GROUP_MSG_REQ = 3001,
    GROUP_MSG_PUSH = 3002,
    HISTORY_REQ = 4001,
    HISTORY_RESP = 4002,
    HEARTBEAT_REQ = 5001,
    HEARTBEAT_RESP = 5002,
    ERROR_RESP = 9000,
};
```

为什么用 `enum class`？

- 比普通 enum 更安全。
- 不会把枚举值隐式混成 int。
- 命名空间更干净。

为什么底层类型是 `std::uint16_t`？

因为协议 Header 里 `msg_type` 就是 2 字节。

## 6. Packet.hpp 讲解

文件：

```text
include/liteim/protocol/Packet.hpp
```

核心常量：

```cpp
inline constexpr std::uint32_t kPacketMagic = 0x4C494D31;
inline constexpr std::uint16_t kPacketVersion = 1;
inline constexpr std::size_t kPacketHeaderSize = 16;
inline constexpr std::uint32_t kMaxBodyLength = 1024 * 1024;
```

解释：

- `kPacketMagic`：用于快速判断这是不是 LiteIM 协议包。
- `kPacketVersion`：协议版本，方便未来升级。
- `kPacketHeaderSize`：Header 固定 16 字节。
- `kMaxBodyLength`：Body 最大 1MB，防止异常包导致内存风险。

结构：

```cpp
struct PacketHeader {
    std::uint32_t magic = kPacketMagic;
    std::uint16_t version = kPacketVersion;
    std::uint16_t msg_type = 0;
    std::uint32_t seq_id = 0;
    std::uint32_t body_len = 0;
};

struct Packet {
    PacketHeader header;
    std::string body;
};
```

函数：

```cpp
std::string encodePacket(const Packet& packet);
std::optional<PacketHeader> parseHeader(const char* data, std::size_t len);
```

`encodePacket()`：

- 输入一个 `Packet`。
- 输出可直接写入 TCP 的字节串。

`parseHeader()`：

- 输入至少 16 字节的内存。
- 尝试解析 Header。
- 如果 Header 不合法，返回 `std::nullopt`。

这里要注意：`PacketHeader` 是逻辑结构，`encodePacket()` 才是真正定义网络传输格式的函数。不要把 `PacketHeader` 的内存布局直接当成网络协议。

## 7. Packet.cpp 讲解

文件：

```text
src/protocol/Packet.cpp
```

### 7.1 编码整数

```cpp
void appendUint16(std::string& output, std::uint16_t value) {
    const auto network_value = htons(value);
    const auto* bytes = reinterpret_cast<const char*>(&network_value);
    output.append(bytes, sizeof(network_value));
}
```

作用：

- 把 16 位整数转成网络字节序。
- 再把它追加到输出字符串。

`appendUint32()` 同理，只是处理 32 位整数。

### 7.1.1 toUint16()

`toUint16(MsgType type)` 定义在 `MessageType.hpp`。

它的作用是把强类型枚举 `MsgType` 转成 Header 里的 2 字节整数。

为什么需要这个函数：

- `enum class` 不会隐式转成整数，类型更安全。
- Header 里的 `msg_type` 是 `std::uint16_t`。
- 用 `toUint16()` 可以让转换位置更明确。

它只做类型转换，不判断这个消息类型是否允许出现在当前业务场景里。比如 `LOGIN_REQ` 是否只能在未登录时发送，是后续业务层要做的事情。

### 7.2 读取整数

```cpp
std::uint32_t readUint32(const char* data) {
    std::uint32_t network_value = 0;
    std::memcpy(&network_value, data, sizeof(network_value));
    return ntohl(network_value);
}
```

这里用 `std::memcpy`，不直接把 `char*` 强转成 `uint32_t*`。

原因：

- 避免未对齐访问问题。
- 避免严格别名规则问题。
- 写法更稳。

### 7.3 encodePacket()

核心逻辑：

```cpp
PacketHeader header = packet.header;
header.magic = kPacketMagic;
header.version = kPacketVersion;
header.body_len = static_cast<std::uint32_t>(packet.body.size());
```

注意：

- `magic` 和 `version` 由协议层强制设置。
- `body_len` 由真实 body 长度计算，不信任调用方传入的旧值。
- 如果 body 超过 1MB，直接抛出 `std::invalid_argument`。

这个函数的输入是一个内存里的 `Packet` 对象，输出是可以直接写入 TCP 的字节串。

它有两个重要副作用或者说“修正行为”：

- 即使调用方传错了 `header.magic` 或 `header.version`，编码时也会强制写成当前协议值。
- 即使调用方传错了 `header.body_len`，编码时也会按 `packet.body.size()` 重新计算。

这样做是为了保证真正发出去的包符合协议，而不是信任外部传入的 Header 字段。

### 7.4 parseHeader()

核心逻辑：

```cpp
if (data == nullptr || len < kPacketHeaderSize) {
    return std::nullopt;
}
```

如果连 Header 的 16 字节都不够，就不能解析。

后面依次解析：

```cpp
header.magic = readUint32(data);
header.version = readUint16(data + 4);
header.msg_type = readUint16(data + 6);
header.seq_id = readUint32(data + 8);
header.body_len = readUint32(data + 12);
```

偏移对应：

```text
0  - 3   magic
4  - 5   version
6  - 7   msg_type
8  - 11  seq_id
12 - 15  body_len
```

最后校验：

```cpp
if (header.magic != kPacketMagic) return std::nullopt;
if (header.version != kPacketVersion) return std::nullopt;
if (header.body_len > kMaxBodyLength) return std::nullopt;
```

这个函数只解析 Header，不解析 Body。

原因是：

- Step 2 只负责 Header 编解码。
- Body 是否完整要结合 TCP 缓冲区长度判断，这属于 Step 3 `FrameDecoder` 的职责。
- `parseHeader()` 返回 `std::optional`，能表达“Header 不合法或者还不够解析”的结果。

它的失败场景包括：

- `data == nullptr`。
- `len < 16`。
- `magic` 不等于 `0x4C494D31`。
- `version` 不等于 `1`。
- `body_len` 超过 1MB。

## 8. 测试讲解

文件：

```text
tests/test_protocol.cpp
```

当前没有引入 GoogleTest，而是用一个轻量级测试 runner。

原因：

- Step 2 重点是协议，不是测试框架。
- 避免太早引入第三方依赖。
- 后续 Step 25 可以再统一切换到 GoogleTest 或 Catch2。

测试覆盖：

1. 正常 Packet 编码。
2. Header 解码字段一致。
3. 错误 magic 返回失败。
4. 错误 version 返回失败。
5. `body_len` 超过 1MB 返回失败。
6. Header 不足 16 字节返回失败。
7. 编码超过 1MB 的 Body 抛异常。

这些测试的大概思路：

- 正常路径：先构造一个 `Packet`，调用 `encodePacket()`，再用 `parseHeader()` 解回 Header，验证字段一致。
- 协议识别：手动改坏 `magic`，验证服务端不会把未知协议当成 LiteIM 包。
- 协议版本：手动改坏 `version`，验证版本不兼容时不会继续进入业务层。
- 长度保护：手动把 `body_len` 改成超过 1MB，验证协议层能拒绝异常长度。
- 输入不完整：只传不足 16 字节，验证 `parseHeader()` 不会越界读取。
- 编码保护：构造超过 1MB 的 body，验证 `encodePacket()` 不允许生成超大包。

测试通过说明：

```text
Packet 的基本网络字节序编码、Header 字段解析、非法 Header 拦截和 body 长度保护都能工作。
```

## 9. 编译和测试

在 `LiteIM/` 根目录执行：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

也可以直接运行测试二进制：

```bash
./build/tests/liteim_tests
```

预期输出：

```text
[PASS] encode packet normal
[PASS] parse header fields
[PASS] wrong magic fails
[PASS] wrong version fails
[PASS] oversized body_len fails
[PASS] short header fails
[PASS] encode oversized body throws
```

## 10. 本步骤验收标准

必须满足：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/server/liteim_server
```

其中：

- `liteim_server` 仍能正常编译和运行。
- `liteim_tests` 能通过协议测试。
- 没有实现 Step 3 的 `FrameDecoder`。
- 没有实现网络读写。
- 没有引入 Boost.Asio。

## 11. 面试时怎么讲

可以这样说：

> 我在协议层定义了固定 16 字节 Header + JSON Body 的 TLV 风格协议。因为 TCP 是字节流，不保留消息边界，所以 Header 里带 `body_len`，后续 FrameDecoder 可以先读 Header，再按长度读取 Body。编码时我没有直接发送 C++ struct，而是逐字段写入网络字节序，避免 struct padding、字节序和跨语言兼容问题。协议层还校验 magic、version 和 body_len 上限，防止非法包进入后续业务层。

这一步的重点不是功能多，而是设计边界清楚：

- `Packet` 负责一条消息的格式。
- `encodePacket()` 负责对象到字节串。
- `parseHeader()` 负责 Header 校验。
- `FrameDecoder` 留到 Step 3 处理 TCP 字节流。

### 11.1 讲解思路

面试时建议按这个顺序讲：

1. 先讲问题：TCP 是字节流，不保留消息边界。
2. 再讲方案：每条消息前面加固定 16 字节 Header，Header 里包含 `body_len`。
3. 再讲字段：`magic` 用于识别协议，`version` 用于升级，`msg_type` 用于路由，`seq_id` 用于请求响应匹配，`body_len` 用于拆包。
4. 再讲跨语言兼容：不直接发送 C++ struct，而是逐字段转网络字节序。
5. 最后讲防御：非法 `magic`、非法 `version`、超大 `body_len` 都在协议层拦截。

### 11.2 面试中容易被问到的问题

**Q1：为什么 Header 要固定长度？**

固定长度 Header 容易解析。服务端只要先读 16 字节，就能拿到 `body_len`，再判断后面 Body 是否完整。

**Q2：为什么不用分隔符，比如 `\n`？**

聊天消息 Body 是 JSON，未来也可能包含各种字符。定长 Header + body_len 更通用，不依赖内容里是否出现分隔符。

**Q3：为什么不能直接 `send(sizeof(PacketHeader))`？**

C++ struct 可能有 padding，而且不同机器字节序可能不同。直接发送 struct 不利于 Python BotClient 和 Qt 客户端复用协议。

**Q4：`magic` 有什么用？**

快速判断这是不是 LiteIM 协议包。收到错误 magic 时可以直接拒绝，避免把垃圾数据交给业务层。

**Q5：`version` 有什么用？**

用于协议升级。比如未来 Header 加字段或 Body 格式变化时，可以通过 version 做兼容判断。

**Q6：为什么要限制 `body_len` 最大 1MB？**

防止异常包或恶意包让服务端分配过大内存。IM 文本消息第一版不需要非常大的 Body。

**Q7：Step 2 为什么不直接实现拆包？**

这是职责划分。Step 2 只定义单条消息的编码格式；Step 3 的 `FrameDecoder` 再负责从 TCP 字节流里切出完整消息。

## 12. 下一步预告

Step 3 会实现：

```text
include/liteim/protocol/FrameDecoder.hpp
src/protocol/FrameDecoder.cpp
tests/test_frame_decoder.cpp
```

它会解决：

- 半包
- 粘包
- 半包 + 粘包混合
- 错误包进入 error 状态

Step 2 定义“一条消息长什么样”，Step 3 负责“从 TCP 字节流里切出一条条消息”。
