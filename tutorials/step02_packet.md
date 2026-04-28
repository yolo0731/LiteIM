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
server/protocol/MessageType.hpp
server/protocol/Packet.hpp
server/protocol/Packet.cpp
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
server/protocol/MessageType.hpp
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
server/protocol/Packet.hpp
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

## 7. Packet.cpp 讲解

文件：

```text
server/protocol/Packet.cpp
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

## 12. 下一步预告

Step 3 会实现：

```text
server/protocol/FrameDecoder.hpp
server/protocol/FrameDecoder.cpp
tests/test_frame_decoder.cpp
```

它会解决：

- 半包
- 粘包
- 半包 + 粘包混合
- 错误包进入 error 状态

Step 2 定义“一条消息长什么样”，Step 3 负责“从 TCP 字节流里切出一条条消息”。
