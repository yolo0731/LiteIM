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
