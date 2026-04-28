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
