# Step 5: TlvCodec

## 1. 本 Step 目标

Step 4 已经实现了 Packet 外层结构：

```text
PacketHeader + body bytes
```

但 `body` 还只是一段原始字节。Step 5 要实现 body 内部的 TLV 编解码。

TLV 是：

```text
Type-Length-Value
```

也就是：

```text
+--------+--------+--------------+
| type   | len    | value        |
+--------+--------+--------------+
| 2 byte | 4 byte | len bytes    |
+--------+--------+--------------+
```

本 Step 实现：

- `appendString()`
- `appendUint64()`
- `parseTlvMap()`
- `getString()`
- `getUint64()`
- `getRepeatedString()`
- `getRepeatedUint64()`

本 Step 不实现：

- TCP 半包 / 粘包处理，这属于 Step 6。
- 登录、私聊、群聊业务，这些属于后续业务 Step。
- socket、epoll、Reactor，这些属于后续网络 Step。

## 2. 为什么 body 要用 TLV

如果直接把业务字段固定成一个 C++ struct，会遇到几个问题：

- 字段增删不方便，协议升级成本高。
- 字符串长度不固定，无法简单放进固定结构体。
- 不同语言客户端要复用协议，不能依赖 C++ 内存布局。
- 业务消息种类很多，登录、私聊、群聊、Bot 消息需要的字段不一样。

TLV 的好处是：

- `type` 表示字段含义，例如 `Username`、`UserId`、`MessageText`。
- `len` 表示字段长度。
- `value` 保存真实字节。
- 新增字段时，老客户端可以选择忽略未知字段。
- 同一个字段可以重复出现，例如联系人列表、群成员列表、消息列表。

## 3. 本项目的 TLV 格式

每个 TLV 字段固定使用 6 字节 header：

```text
type: 2 bytes, uint16, network byte order
len : 4 bytes, uint32, network byte order
value: len bytes
```

例如：

```text
Username = "bob"
```

编码后是：

```text
00 01   00 00 00 03   62 6F 62
type    len            value
```

`00 01` 是 `TlvType::Username`。

`00 00 00 03` 表示 value 长度为 3。

`62 6F 62` 是字符串 `"bob"` 的 UTF-8 字节。

## 4. 为什么继续手写网络字节序

和 Step 4 一样，TLV 里多字节整数也不能直接 `memcpy`。

原因：

- 不同 CPU 可能有不同字节序。
- C++ struct 可能有 padding。
- Python BotClient、Qt 客户端和 C++ 服务端必须看到同一套 wire format。

所以 `TlvCodec.cpp` 通过协议层共享的 `ByteOrder.hpp` 写：

```cpp
appendUint16BE(output, static_cast<std::uint16_t>(type));
appendUint32BE(output, static_cast<std::uint32_t>(len));
```

`uint64` 和 `int64` 的 value 也按大端序写入。

Step 16 前代码清理后，TLV 和 Packet 共用同一个大端工具头：

```text
include/liteim/protocol/ByteOrder.hpp
```

## TlvCodec.hpp 接口说明

`TlvCodec.hpp` 负责 Packet body 内部字段编解码。

常量和类型：

- `kTlvHeaderSize = 6`，每个字段固定 `type(2) + len(4)`。
- `kMaxTlvValueLength = 1024 * 1024`，单个 TLV value 最大 1MB。
- `TlvValue = Bytes`，表示一个字段值的原始字节。
- `TlvValues = std::vector<TlvValue>`，表示同一种字段可以出现多次。
- `TlvMap = std::unordered_map<TlvType, TlvValues>`，解析后的字段表。

append 系列：

- `appendString(type, value, output)` 写入字符串字段。`Unknown` type、非空 value 但 data 为空、value 超限都会返回错误。
- `appendUint64(type, value, output)` 先把整数按大端序编码成 8 字节，再写入 TLV。
- append 函数不会清空 `output`，因为调用方经常要连续追加多个字段。

parse 系列：

- `parseTlvMap(data, len, output)` 先清空 `output`，再顺序解析所有 TLV。
- `parseTlvMap(body, output)` 是 `Bytes` 版本的便利重载。
- header 不完整、value 长度超限、value 长度超过 body 剩余字节都会返回 `ParseError`。
- 空 body 解析成功，得到空 map。

get 系列：

- `getString()` 读取指定字段的第一个 value 并转成字符串。
- `getUint64()` 要求第一个 value 正好 8 字节，再按大端序读整数。
- `getRepeatedString()` 返回同 type 的所有字符串值。
- `getRepeatedUint64()` 返回同 type 的所有 8 字节整数值。
- 缺失字段返回 `NotFound`，整数长度错误返回 `ParseError`。

关键 private helper 在 `.cpp` 中：

- `appendValue()` 统一写 TLV header、长度检查和 value 追加。
- `findValues()` 统一处理字段缺失和空数组检查，避免四个 get 函数重复判断。

线程和所有权边界：

- TLV 编解码函数无全局状态，可以跨线程并发调用不同输入输出。
- `output` 和 `TlvMap` 由调用方拥有。
- 这里不决定业务字段是否必填，只提供“取不到就返回错误”的基础语义。

## TlvCodec 的作用场景和运行流程

### 1. 在 LiteIM 里的具体使用场景

`TlvCodec` 负责 Packet body 的字段编解码。登录、私聊、群聊、历史消息和 Bot 请求都可以复用同一个 `type(2) + len(4) + value` 格式，把业务字段装进 `Packet.body`，接收方再解析成 `TlvMap` 按字段读取。

### 2. 上下层调用连接

```text
发送方向：业务字段
    -> appendString() / appendUint64()
    -> body Bytes
    -> Packet.body
    -> encodePacket()
    -> Session::sendPacket()

接收方向：Session MessageCallback(Packet)
    -> parseTlvMap(packet.body)
    -> getString() / getUint64() / getRepeated*()
    -> 业务 service
```

上游是业务字段，下游是 `Packet` body。`TlvCodec` 不判断登录态、权限或路由，只保证字段格式正确。

### 3. 整体运行链路

1. 发送方创建空 `Bytes body`。
2. 每个字段调用 `appendString()` 或 `appendUint64()` 写入 type、len、value。
3. body 放进 `Packet.body` 后交给 `encodePacket()`。
4. 接收方拿到完整 `Packet` 后调用 `parseTlvMap()`。
5. parser 从 offset 0 循环读取 TLV header 和 value。
6. 同一个 `TlvType` 可以出现多次，全部追加到 `TlvValues`。
7. 业务代码用 `getString()`、`getUint64()` 或 repeated getter 读取字段。

### 4. 自身内部运行流程

整体可以看成 3 步：append 生成字段、parse 分组字段、get 做类型读取。

核心数据职责：

- `Bytes` 保存原始 body。
- `TlvMap` 是 `TlvType -> vector<Bytes>`，支持重复字段。
- `kTlvHeaderSize` 固定 6 字节。
- `kMaxTlvValueLength` 限制单个字段 value。

核心函数流程：

- [appendValue()](../src/protocol/TlvCodec.cpp#L12)：校验 type、指针和长度，然后写 type、len、value。
- [appendString()](../src/protocol/TlvCodec.cpp#L42)：把 string 视为原始字节交给 `appendValue()`。
- [appendUint64()](../src/protocol/TlvCodec.cpp#L46)：先按大端序生成 8 字节，再写 TLV。
- [parseTlvMap()](../src/protocol/TlvCodec.cpp#L53)：循环读取 TLV header，检查长度，再把 value 复制到 map。
- [getString()](../src/protocol/TlvCodec.cpp#L88)：取第一个 value 并转成 string。
- [getUint64()](../src/protocol/TlvCodec.cpp#L99)：要求 value 正好 8 字节，再按大端序读整数。
- [getRepeatedUint64()](../src/protocol/TlvCodec.cpp#L128)：遍历同一字段下的所有 value。

`parseTlvMap(data, len, output)` 可以理解成“按 TLV 头切字段”：

```text
Packet.body 原始字节
    ↓
读取 TLV header：type + len
    ↓
检查 value 长度边界
    ↓
按 type 把 value 放入 TlvMap
    ↓
重复字段继续追加到同一个 TlvValues
    ↓
解析结束后交给 getString() / getUint64() 读取
```

遇到 TLV header 不完整、value 长度超过剩余 body、单字段超过上限或 type 未知时，解析立即结束并把具体错误交给上层。成功路径只是分组保存原始 value，不在这里做登录、权限或业务路由。

### 5. 小例子和边界

小例子：好友列表响应可以多次写入 `TlvType::FriendId`。解析后 `TlvMap[FriendId]` 里有多个 8 字节 value，`getRepeatedUint64()` 返回所有好友 ID。

边界：重复字段是合法行为，单值 get 默认取第一个；未知 `TlvType` 数字仍进入 map，便于老版本忽略新字段；value 长度越界说明 body 损坏，上层可以返回错误响应或关闭连接。

## 5. `TlvMap` 的设计

代码在：

- `include/liteim/protocol/TlvCodec.hpp`
- `src/protocol/TlvCodec.cpp`

核心类型：

```cpp
using TlvValue = Bytes;
using TlvValues = std::vector<TlvValue>;
using TlvMap = std::unordered_map<TlvType, TlvValues>;
```

为什么不是：

```cpp
std::unordered_map<TlvType, TlvValue>
```

因为重复字段要能处理。比如后续好友列表可能这样表达：

```text
FriendId: 1001
FriendId: 1002
FriendId: 1003
```

所以一个 `TlvType` 对应多个 value。

## 6. append 系列函数

### `appendString()`

```cpp
Status appendString(TlvType type, const std::string& value, Bytes& output);
```

把字符串按原始 UTF-8 字节追加进 body。

它不关心字符串是英文、中文还是 emoji。Packet/TLV 层只负责字节不丢，真正的文本含义由上层业务处理。

### `appendUint64()`

```cpp
Status appendUint64(TlvType type, std::uint64_t value, Bytes& output);
```

把 64 位无符号整数按网络字节序写成 8 字节。

后续 `UserId`、`MessageId`、`TimestampMs` 这类字段都可以用它。

## 7. parse 和 get 的职责边界

### `parseTlvMap()`

```cpp
Status parseTlvMap(const Byte* data, std::size_t len, TlvMap& output);
Status parseTlvMap(const Bytes& body, TlvMap& output);
```

职责：

- 顺序扫描 body。
- 每次读取 `type + len + value`。
- 检查 TLV header 是否完整。
- 检查 `len` 是否超过剩余 body。
- 把重复字段保存到 `TlvMap`。

它不判断某个业务消息是否缺少字段。原因是：

- 登录请求需要 `Username` 和 `Password`。
- 私聊请求需要 `ReceiverId` 和 `MessageText`。
- 历史消息请求需要 `ConversationId`、`Offset`、`Limit`。

不同业务消息的必需字段不一样，所以“缺字段”应该由 getter 或业务层判断。

### `getString()`

```cpp
Status getString(const TlvMap& map, TlvType type, std::string& output);
```

从 map 里取一个必需字符串字段。字段不存在时返回 `ErrorCode::NotFound`。

### `getUint64()`

```cpp
Status getUint64(const TlvMap& map, TlvType type, std::uint64_t& output);
```

从 map 里取一个必需 `uint64` 字段。字段不存在返回 `NotFound`，字段长度不是 8 字节返回 `ParseError`。

### `getRepeatedString()`

```cpp
Status getRepeatedString(const TlvMap& map, TlvType type, std::vector<std::string>& output);
```

读取同一个 `TlvType` 下的多个字符串字段。

### `getRepeatedUint64()`

```cpp
Status getRepeatedUint64(const TlvMap& map, TlvType type, std::vector<std::uint64_t>& output);
```

读取同一个 `TlvType` 下的多个 `uint64` 字段。后续好友列表、群成员列表、我的群列表和离线消息 ID 列表会更常用这一类重复 ID 字段。

## 8. 测试说明

Step 5 新增：

```text
tests/protocol/tlv_codec_test.cpp
```

核心测试：

- `TlvCodecTest.StringFieldCanBeEncodedAndDecoded`
  - 单个字符串字段可以编码、解析、读取。
- `TlvCodecTest.MultipleFieldsCanBeEncodedAndDecoded`
  - 一个 body 内可以同时放 username、user_id、message_text。
- `TlvCodecTest.Utf8StringCanBeEncodedAndDecoded`
  - 中文和 emoji 作为 UTF-8 字节不丢失。
- `TlvCodecTest.RepeatedStringFieldsArePreserved`
  - 重复字段不会被后一个覆盖。
- `TlvCodecTest.RepeatedUint64FieldsArePreserved`
  - 重复的 `FriendId` 这类 ID 字段可以按出现顺序读取。
- `TlvCodecTest.Uint64UsesNetworkByteOrder`
  - `uint64` value 按大端序写入。
- `TlvCodecTest.TlvLengthOutOfBoundsReturnsError`
  - `len` 超过剩余 body 时返回错误。
- `TlvCodecTest.IncompleteTlvHeaderReturnsError`
  - 不足 6 字节的 TLV header 返回错误。
- `TlvCodecTest.MissingStringFieldReturnsError`
  - 必需字符串字段缺失返回 `NotFound`。
- `TlvCodecTest.MissingUint64FieldReturnsError`
  - 必需整数字段缺失返回 `NotFound`。
- `TlvCodecTest.WrongUint64LengthReturnsError`
  - 用字符串伪装成 `uint64` 时返回 `ParseError`。
- `TlvCodecTest.UnknownTypeCannotBeEncoded`
  - 主动编码 `TlvType::Unknown` 返回 `InvalidArgument`。

运行：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

## 9. 面试讲法

可以这样讲：

> 我把 Packet body 设计成 TLV 格式，每个字段是 2 字节 type、4 字节 len 和可变长 value。TLV 的多字节字段统一使用网络字节序，避免结构体 padding 和大小端问题。解析时先做 len 边界检查，防止非法长度导致越界读取；同一个字段可以重复出现，所以解析结果用 `unordered_map<TlvType, vector<vector<uint8_t>>>` 保存。缺失必需字段不在通用 parser 里判断，而是在 `getString()`、`getUint64()` 或业务 handler 读取字段时返回 `NotFound`，这样协议层和业务层边界更清楚。

## 10. 本 Step 提交信息

```text
feat(protocol): implement tlv codec
```
