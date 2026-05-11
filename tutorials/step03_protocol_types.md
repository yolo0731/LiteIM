# Step 3: MessageType / TLV 类型定义

## 1. 本 Step 解决什么问题

LiteIM 后续要使用自定义二进制协议。协议通常由两部分组成：

```text
Packet Header
    -> magic / version / flags / msg_type / seq_id / body_len

Packet Body
    -> TLV field list
       type / length / value
```

Step 3 只做最基础的一件事：先把 `msg_type` 和 TLV 字段类型固定下来。

也就是说，本 Step 定义：

- `MessageType`：这包消息是登录请求、私聊请求、群聊推送、历史消息响应，还是错误响应。
- `TlvType`：body 里某个字段是用户名、密码、消息文本、群组 ID，还是错误信息。
- `toString()`：把类型转成可读字符串，方便日志、测试和调试。
- `isRequestType()` / `isResponseType()` / `isPushType()`：区分请求、响应和服务端推送，为后续 `MessageRouter`、Qt 客户端和 Python BotClient 做准备。

本 Step 不做：

- `PacketHeader`
- `Packet`
- `encodePacket()`
- `parseHeader()`
- TLV body 编解码
- 网络字节序转换
- TCP 半包 / 粘包处理

这些会放到后续 Step。

## 2. 本 Step 新增文件

```text
include/liteim/protocol/
├── MessageType.hpp
└── Tlv.hpp

src/protocol/
├── CMakeLists.txt
├── MessageType.cpp
└── Tlv.cpp

tests/protocol/
├── message_type_test.cpp
└── tlv_type_test.cpp
```

`protocol` 是一个独立模块，编译成 `liteim_protocol`。

后续协议、网络、CLI、Qt 和 Python BotClient 都应该围绕这些类型保持一致。

## MessageType.hpp / Tlv.hpp 接口说明

`MessageType.hpp` 定义 Packet header 中 `msg_type` 字段的取值：

- 底层类型是 `std::uint16_t`，对应 Packet header 里的 2 字节字段。
- `Unknown = 0` 用于默认值或未识别类型，不作为正常业务消息。
- `HeartbeatRequest/Response` 使用 1-2，作为连接保活控制消息。
- 100 段是注册、登录、登出。
- 200 段是好友相关请求和响应。
- 300 段是私聊请求、响应和服务端推送。
- 400 段是群组和群聊消息。
- 500 段是离线消息和历史消息。
- 600 段是 Bot / PersonaAgent 消息。
- `ErrorResponse = 900` 用于统一错误响应。
- `toString(MessageType)` 返回稳定可读名称，未知值返回 `Unknown`。
- `isRequestType()`、`isResponseType()`、`isPushType()` 按消息语义分类，不读取 Packet body。

`Tlv.hpp` 定义 Packet body 中 TLV 字段的 type：

- 底层类型同样是 `std::uint16_t`，对应 TLV header 的 2 字节 type。
- 用户和认证字段包括 `Username`、`Password`、`UserId`、`Nickname`、`Token`、`SessionId`。
- 好友字段包括 `FriendId`、`TargetUserId`。
- 群组字段包括 `GroupId`、`GroupName`。
- 消息字段包括 `ConversationType`、`ConversationId`、`MessageId`、`MessageText`、`SenderId`、`ReceiverId`、`TimestampMs`、`Offset`、`Limit`、`UnreadCount`。
- 错误字段包括 `ErrorCode`、`ErrorMessage`。
- Bot 字段包括 `BotId`、`PersonaId`。
- `toString(TlvType)` 只做字段名转换，不解析 value。

这两个头文件没有类构造/析构语义，也没有 fd、线程或内存所有权。它们是跨模块、跨语言协议常量，值一旦发布就应保持兼容。

## Protocol Types 的作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

`MessageType` 是 Packet 级路由键，决定这是登录、私聊、群聊、Bot 还是错误响应；`TlvType` 是 body 字段键，决定用户名、密码、用户 ID、消息文本等字段怎么读。LiteIM server、Qt client 和 PersonaAgent BotClient 必须共享同一套数字值。

### 2. 上下层调用连接

```text
业务动作
    -> MessageType / TlvType
    -> Packet.header.msg_type / TLV body
    -> encodePacket() / parseHeader()
    -> TlvCodec parse/get
    -> MessageRouter / service / BotGateway 后续处理
```

上游是业务语义，下游是 Step 4 `Packet` 和 Step 5 `TlvCodec`。这些枚举自己不做 I/O，只给协议层提供稳定数字。

### 3. 整体运行链路

1. 发送方根据业务动作选择一个 `MessageType`，例如登录请求使用 `LoginRequest`。
2. 发送方根据字段含义选择 `TlvType`，例如用户名使用 `Username`。
3. Step 5 把字段写成 TLV body。
4. Step 4 把 `MessageType` 写进 Packet header。
5. 接收方先从 header 读 `msg_type`，决定交给哪类业务处理。
6. 业务处理再从 `TlvMap` 按 `TlvType` 取字段。
7. 日志和测试用 `toString()` 把枚举转成可读名称。

### 4. 自身内部运行流程

整体可以看成 3 步：定义协议数字、提供可读字符串、提供轻量分类。

核心数据职责：

- [MessageType.hpp](../include/liteim/protocol/MessageType.hpp#L7) 保存消息级枚举数字。
- [Tlv.hpp](../include/liteim/protocol/Tlv.hpp#L7) 保存字段级枚举数字。
- `toString()` 只服务日志和测试，不参与 wire format。
- `isRequestType()` / `isResponseType()` / `isPushType()` 帮助上层做基础分类。

核心函数流程：

- [toString(MessageType)](../src/protocol/MessageType.cpp#L5)：`switch` 枚举，返回稳定字符串。
- [isRequestType()](../src/protocol/MessageType.cpp#L78)：列出所有 request 类型，其他返回 false。
- [isResponseType()](../src/protocol/MessageType.cpp#L120)：列出所有 response 类型，push 不算 response。
- [isPushType()](../src/protocol/MessageType.cpp#L162)：只识别服务端主动推送类型。
- [toString(TlvType)](../src/protocol/Tlv.cpp#L5)：把字段枚举转成日志字符串。

分类函数可以理解成“按消息用途分组判断”：

```text
MessageType
    ↓
request 分组：Heartbeat / Login / PrivateMessage / ...
    ↓
response 分组：HeartbeatResponse / LoginResponse / ...
    ↓
push 分组：PrivateMessagePush / GroupMessagePush / BotMessagePush
    ↓
上层获得轻量分类结果
```

也就是说，[isRequestType()](../src/protocol/MessageType.cpp#L78)、[isResponseType()](../src/protocol/MessageType.cpp#L120) 和 [isPushType()](../src/protocol/MessageType.cpp#L162) 不解析 body、不分发业务，只维护“哪些枚举属于哪一组”的协议规则。

### 5. 该项目代码在实际应用中的具体数据例子

Alice (`user_id=1001`) 给 Bob (`user_id=1002`) 发私聊时，客户端会使用 `MessageType::kPrivateMessageRequest`；服务端确认后返回 `MessageType::kPrivateMessageResponse`；如果 Bob 在线，还会给 Bob 推送 `MessageType::kPrivateMessagePush`。同一个请求可以带 `seq_id=7`，让客户端把响应和发送中的消息气泡对应起来。

## 3. MessageType 设计

`MessageType` 的底层类型是 `std::uint16_t`：

```cpp
enum class MessageType : std::uint16_t {
    Unknown = 0,

    HeartbeatRequest = 1,
    HeartbeatResponse = 2,

    RegisterRequest = 100,
    RegisterResponse = 101,
    LoginRequest = 102,
    LoginResponse = 103,
    LogoutRequest = 104,
    LogoutResponse = 105,

    AddFriendRequest = 200,
    AddFriendResponse = 201,
    ListFriendsRequest = 202,
    ListFriendsResponse = 203,

    PrivateMessageRequest = 300,
    PrivateMessageResponse = 301,
    PrivateMessagePush = 302,

    CreateGroupRequest = 400,
    CreateGroupResponse = 401,
    JoinGroupRequest = 402,
    JoinGroupResponse = 403,
    ListGroupsRequest = 404,
    ListGroupsResponse = 405,
    GroupMessageRequest = 406,
    GroupMessageResponse = 407,
    GroupMessagePush = 408,

    OfflineMessagesRequest = 500,
    OfflineMessagesResponse = 501,
    HistoryRequest = 502,
    HistoryResponse = 503,

    BotChatRequest = 600,
    BotChatResponse = 601,
    BotMessagePush = 602,

    ErrorResponse = 900,
};
```

为什么使用 `std::uint16_t`：

- 后续 Packet header 里 `msg_type` 可以固定为 2 字节。
- 类型值足够多，未来可以继续扩展。
- 跨语言实现时，Python BotClient 也可以按整数值映射消息类型。

为什么按范围分组：

```text
1-99      heartbeat / control
100-199  auth
200-299  friend
300-399  private chat
400-499  group chat
500-599  offline / history
600-699  bot / agent
900      error response
```

这样日志里看到数字时也更容易判断大类。

## 4. Request / Response / Push 的区别

LiteIM 里消息类型可以分成三类。

### Request

客户端主动发给服务端的请求，例如：

```text
LoginRequest
PrivateMessageRequest
GroupMessageRequest
HistoryRequest
BotChatRequest
```

`isRequestType()` 对这些类型返回 `true`。

### Response

服务端对请求的应答，例如：

```text
LoginResponse
PrivateMessageResponse
HistoryResponse
ErrorResponse
```

`isResponseType()` 对这些类型返回 `true`。

`ErrorResponse` 也被归为 response，因为它通常是对某个请求失败后的统一错误返回。

### Push

服务端主动推送给客户端的消息，例如：

```text
PrivateMessagePush
GroupMessagePush
BotMessagePush
```

`isPushType()` 对这些类型返回 `true`。

Push 既不是 request，也不是 response，但它也不能和 `Unknown` 混在一起。后续 Qt 客户端和 Python BotClient 收到 Push 时，要走会话列表刷新、消息气泡追加、桌面通知或 Bot 处理逻辑；收到未知类型时应该走错误日志或忽略策略。

举例：

```text
Alice -> Server: PrivateMessageRequest
Server -> Alice: PrivateMessageResponse
Server -> Bob:   PrivateMessagePush
```

Alice 发私聊请求，服务端给 Alice 一个发送结果响应，同时给 Bob 推送新消息。

所以分类关系应该是互斥的：

```text
Request  -> isRequestType(type) == true
Response -> isResponseType(type) == true
Push     -> isPushType(type) == true
Unknown  -> 三个函数都返回 false
```

## 5. `toString(MessageType)`

接口：

```cpp
const char* toString(MessageType type) noexcept;
```

作用：把枚举转成可读字符串。

例如：

```cpp
toString(MessageType::LoginRequest) == "LOGIN_REQUEST"
toString(MessageType::GroupMessagePush) == "GROUP_MESSAGE_PUSH"
toString(static_cast<MessageType>(65535)) == "UNKNOWN"
```

为什么返回 `const char*`：

- 返回的是静态字符串字面量，不需要动态分配。
- 日志和测试都可以直接使用。
- 比 `std::string` 更轻量，也不在公共接口引入 `std::string_view`。

## 6. TlvType 设计

`TlvType` 表示 TLV body 里的字段类型：

```cpp
enum class TlvType : std::uint16_t {
    Unknown = 0,

    Username = 1,
    Password = 2,
    UserId = 3,
    Nickname = 4,
    Token = 5,
    SessionId = 6,

    FriendId = 20,
    TargetUserId = 21,

    GroupId = 30,
    GroupName = 31,

    ConversationType = 40,
    ConversationId = 41,
    MessageId = 42,
    MessageText = 43,
    SenderId = 44,
    ReceiverId = 45,
    TimestampMs = 46,
    Offset = 47,
    Limit = 48,
    UnreadCount = 49,

    ErrorCode = 90,
    ErrorMessage = 91,

    BotId = 100,
    PersonaId = 101,
};
```

可以把 TLV 理解成：

```text
type:   这个字段是什么
length: value 有多长
value:  具体内容
```

例如登录请求后续可以编码成：

```text
MessageType: LOGIN_REQUEST
Body:
  TLV(USERNAME, "alice")
  TLV(PASSWORD, "123456")
```

私聊请求可以编码成：

```text
MessageType: PRIVATE_MESSAGE_REQUEST
Body:
  TLV(RECEIVER_ID, 1002)
  TLV(MESSAGE_TEXT, "hello")
```

本 Step 只定义字段编号，不定义 value 如何编码。字符串、整数、时间戳的二进制编码会在 TLV 编解码 Step 中实现。

## 7. CMake 变化

`src/CMakeLists.txt` 新增：

```cmake
add_subdirectory(protocol)
```

`src/protocol/CMakeLists.txt` 新增 `liteim_protocol`：

```cmake
add_library(liteim_protocol
    MessageType.cpp
    Tlv.cpp
)
```

`tests/CMakeLists.txt` 让测试程序链接 `liteim_protocol`：

```cmake
target_link_libraries(liteim_tests PRIVATE liteim_base liteim_protocol GTest::gtest_main)
```

这样协议类型不是测试文件里的临时定义，而是正式项目模块。

## 8. 测试说明

本 Step 新增 2 个测试文件、8 个测试用例。

### `tests/protocol/message_type_test.cpp`

测试：

```cpp
TEST(MessageTypeTest, CoreTypesReturnReadableNames)
TEST(MessageTypeTest, UnknownTypeReturnsUnknown)
TEST(MessageTypeTest, RequestTypesAreClassified)
TEST(MessageTypeTest, ResponseTypesAreClassified)
TEST(MessageTypeTest, PushTypesAreClassified)
TEST(MessageTypeTest, UnknownTypesAreNotClassified)
```

覆盖点：

- 核心消息类型能转换成可读字符串。
- `MessageType::Unknown` 和未注册整数值返回 `UNKNOWN`。
- 请求类型能被 `isRequestType()` 正确识别。
- 响应类型能被 `isResponseType()` 正确识别。
- Push 类型能被 `isPushType()` 正确识别。
- Unknown 和未注册类型不会被误判为 request、response 或 push。

### `tests/protocol/tlv_type_test.cpp`

测试：

```cpp
TEST(TlvTypeTest, CoreTypesReturnReadableNames)
TEST(TlvTypeTest, UnknownTypeReturnsUnknown)
```

覆盖点：

- 核心 TLV 字段类型能转换成可读字符串。
- `TlvType::Unknown` 和未注册整数值返回 `UNKNOWN`。

## 9. 如何验证

在 `LiteIM/` 目录下运行：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

CTest 预期：

```text
100% tests passed, 0 tests failed out of 23
```

测试通过说明：

- `liteim_protocol` 可以被测试程序正常链接。
- 消息类型和 TLV 类型的字符串映射稳定。
- 请求、响应、推送和未知消息的分类边界清晰。

## 10. 面试时怎么讲

可以这样讲：

> 我在协议层先定义了 `MessageType` 和 `TlvType`。`MessageType` 进入 Packet header，用来区分登录、私聊、群聊、历史消息、Bot 消息和错误响应；`TlvType` 用在 body 里，用 TLV 方式表达用户名、用户 ID、消息文本、群组 ID 等字段。请求、响应和推送消息分别用 `isRequestType()`、`isResponseType()` 和 `isPushType()` 显式分类，后续路由层和客户端都能区分业务请求、请求结果和服务端主动推送。

重点表达：

- 协议类型定义是跨服务端、CLI、Qt、Python BotClient 的共同契约。
- Step 3 先固定类型编号，后续 Packet / TLV 编解码在这个基础上实现。
- Push 类型用于服务端主动投递，不能简单归为 response，也不能用“不是 request/response”来替代判断，否则会和 Unknown 混淆。
- 未知类型返回 `UNKNOWN`，有利于日志和错误处理。
- 用 GoogleTest 固定协议类型行为，避免后续改动破坏跨语言协议兼容。

## 11. 面试常见追问

### 为什么要自定义 MessageType，而不是直接发 JSON 字符串？

自定义二进制协议更贴近高性能服务器项目的训练目标。`msg_type` 固定为整数，路由判断更直接，header 长度固定，后续处理半包/粘包也更清晰。JSON 可以作为 value 或调试格式，但不适合作为这个项目的主协议表达。

### TLV 的好处是什么？

TLV 可以让 body 字段可扩展。老客户端遇到不认识的 TLV type，可以跳过对应 length；新字段也不一定要求所有旧代码同时修改。对 IM 协议来说，登录、私聊、群聊、历史消息可以共用同一套 TLV 编码规则。

### Push 为什么不是 Response？

Response 是对某个请求的直接应答，通常回给发起请求的连接。Push 是服务端主动投递给另一个客户端，比如 Bob 收到 Alice 的消息。Push 不一定对应 Bob 的请求，所以不能归为 response。

### 为什么当前只做类型定义，不做 Packet 编码？

这是为了保持 Step 边界清楚。Step 3 固定协议契约，Step 4 再实现 Packet header 编解码，Step 5 再实现 TLV body 编解码，Step 6 再处理 TCP 流式解码。这样每一步都能单独测试，出问题时定位更简单。
