# LiteIM 协议说明

LiteIM 使用 TCP 长连接传输数据。由于 TCP 是字节流，不保留消息边界，所以应用层需要自己定义协议来区分一条完整消息。

第一版协议采用：

```text
固定长度 Header + JSON Body
```

Header 负责描述消息元信息，Body 保存具体业务数据。

计划 Header 字段：

- `magic`：协议魔数，固定为 `0x4C494D31`，表示 `LIM1`。
- `version`：协议版本，第一版固定为 `1`。
- `msg_type`：消息类型，例如登录请求、私聊请求、心跳请求。
- `seq_id`：请求序号，用于请求和响应对应。
- `body_len`：JSON Body 字节长度。

协议层目标：

- 解决 TCP 粘包问题。
- 解决 TCP 半包问题。
- 校验非法 `magic`、非法 `version` 和超大 `body_len`。
- 为 C++ 服务端、Qt 客户端和 Python BotClient 提供统一通信格式。

具体编解码逻辑会在 Step 2 实现，拆包逻辑会在 Step 3 的 `FrameDecoder` 中实现。

## Step 2 已实现内容

Step 2 已经实现协议基础结构：

- `server/protocol/MessageType.hpp`
- `server/protocol/Packet.hpp`
- `server/protocol/Packet.cpp`
- `tests/test_protocol.cpp`

当前协议常量：

```cpp
inline constexpr std::uint32_t kPacketMagic = 0x4C494D31;  // "LIM1"
inline constexpr std::uint16_t kPacketVersion = 1;
inline constexpr std::size_t kPacketHeaderSize = 16;
inline constexpr std::uint32_t kMaxBodyLength = 1024 * 1024;
```

当前 Header 结构：

```cpp
struct PacketHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t msg_type;
    std::uint32_t seq_id;
    std::uint32_t body_len;
};
```

虽然 C++ 里定义了 `PacketHeader`，但编码时不能直接把 struct 原样写入 TCP。

原因：

- struct 可能有 padding，跨编译器和跨语言不稳定。
- 不同机器可能有不同字节序。
- 后续 Python BotClient 必须能和 C++ 服务端解析出完全一致的字段。

所以 `encodePacket()` 采用逐字段编码：

1. `magic` 写入网络字节序。
2. `version` 写入网络字节序。
3. `msg_type` 写入网络字节序。
4. `seq_id` 写入网络字节序。
5. `body_len` 写入网络字节序。
6. 追加 JSON Body 字节。

`parseHeader()` 只解析 Header，不解析 Body。它负责校验：

- Header 长度是否至少 16 字节。
- `magic` 是否等于 `0x4C494D31`。
- `version` 是否等于 `1`。
- `body_len` 是否不超过 1MB。

如果校验失败，返回 `std::nullopt`。

## 当前消息类型

`MessageType.hpp` 中定义了第一版消息类型：

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

注意：Step 2 只定义消息类型和 Packet 编解码，不实现登录、私聊、群聊等业务逻辑。

## 当前测试覆盖

`tests/test_protocol.cpp` 覆盖：

- 正常 Packet 编码。
- Header 解码字段一致。
- 错误 magic 返回失败。
- 错误 version 返回失败。
- `body_len` 超过 1MB 返回失败。
- Header 长度不足返回失败。
- 编码超过 1MB 的 body 抛出异常。

后续 Step 3 会基于这里的 `Packet` 和 `parseHeader()` 实现 `FrameDecoder`，用于处理 TCP 半包、粘包和半包 + 粘包混合场景。

## Step 3 已实现内容

Step 3 已经实现 `FrameDecoder`：

- `server/protocol/FrameDecoder.hpp`
- `server/protocol/FrameDecoder.cpp`
- `tests/test_frame_decoder.cpp`

`FrameDecoder` 的职责是从 TCP 字节流中切出完整的 `Packet`。

它不负责：

- 不负责 `read()` socket。
- 不负责 `write()` socket。
- 不负责登录、私聊、群聊等业务逻辑。

它只负责：

- 保存还没解析完的字节。
- 判断当前缓冲区里是否已经有完整 Header。
- 通过 `parseHeader()` 校验 Header。
- 根据 `body_len` 判断 Body 是否完整。
- 完整时返回 `Packet`。
- 不完整时继续缓存。
- 非法 Header 时进入 error 状态。

### feed() 的返回语义

`feed(const char* data, std::size_t len)` 每次可能返回：

- 0 个 `Packet`：数据还不够，属于半包。
- 1 个 `Packet`：刚好解析出一条完整消息。
- 多个 `Packet`：一次输入里包含多条完整消息，属于粘包。

### 半包处理

如果当前缓冲区不足 16 字节，说明连 Header 都不完整。

此时：

- 不返回 `Packet`。
- 不报错。
- 继续缓存，等待下一次 `feed()`。

如果 Header 完整，但 `body_len` 指定的 Body 还没收齐，也属于半包。

此时同样：

- 不返回 `Packet`。
- 不报错。
- 保留 Header 和已收到的部分 Body。

### 粘包处理

如果一次 `feed()` 收到：

```text
Packet A + Packet B
```

`FrameDecoder` 会在内部循环解析：

1. 先解析 Packet A。
2. 从缓冲区删除 Packet A 对应字节。
3. 继续判断剩余字节是否还能解析 Packet B。
4. 如果 Packet B 完整，也一起返回。

所以一次 `feed()` 可以返回多个 `Packet`。

### 错误状态

如果 Header 已经够 16 字节，但：

- `magic` 错误。
- `version` 错误。
- `body_len` 超过 1MB。

`parseHeader()` 会失败，`FrameDecoder` 会进入 error 状态。

进入 error 状态后：

- 清空内部缓冲区。
- `hasError()` 返回 true。
- 后续 `feed()` 不再继续解析。
- 必须调用 `reset()` 才能恢复。

这样做的原因是：协议已经失去同步，继续猜测边界风险很高，第一版直接关闭连接或重置解码器更简单可靠。

### 当前测试覆盖

`tests/test_frame_decoder.cpp` 覆盖：

- 一个完整包。
- 空 Body 包。
- 只收到半个 Header。
- 收到完整 Header 但 Body 不完整。
- 一次收到两个完整包。
- 第一次 `feed()` 半包，第二次 `feed()` 剩余部分。
- 半包 + 粘包混合。
- 错误 magic。
- `body_len` 超过 1MB。
- error 后 `reset()` 恢复。

后续 Step 10 的 `Session` 会在 `handleRead()` 中循环 `read()` 到 `EAGAIN`，然后把读到的字节交给 `FrameDecoder`。
