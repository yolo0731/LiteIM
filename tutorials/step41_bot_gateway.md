# Step 41：BotGateway 和 EchoBot 普通用户接入

## 0. 本 Step 结论

- 目标：Step 41 把 seed 里的 `mira_bot` 接入业务层，作为普通 IM 用户触发 C++ 第一版 EchoBot。
- 前置依赖：依赖 Step 36 私聊、Step 38 群聊、Step 32 在线 session 查找、Step 31 MySQL/Redis 适配层。
- 主要交付：新增 `BotGateway` / `EchoBotGateway` / `BotService`，并接入 `ChatService` 和 `GroupService`。
- 协议边界：第一版不启用 `BotChatRequest` / `BotChatResponse` / `BotMessagePush`，继续走普通私聊和群聊协议。
- 存储边界：不修改 `users` 表，不新增 `users.user_type`；`mira_bot` 当前只通过集中 `BotOptions` 识别。
- 离线边界：`mira_bot` 是服务端内置 EchoBot，不写 bot 自己的 offline/unread；离线人类用户仍按普通消息规则写 offline/unread。

## 1. 为什么需要这个 Step

LiteIM 后续要让 PersonaAgent 以普通 Bot 用户身份接入，而不是把 Python、LangGraph、LLM SDK 嵌进 C++ 服务端。Step 41 先在 C++ 服务端做一个最小占位：

- 证明 `mira_bot` 可以通过普通私聊和群聊协议参与消息流。
- 给后续 Python BotClient / PersonaAgent 保留清晰边界。
- 不破坏当前客户端、TLV 协议、消息表和离线消息语义。
- 用 `EchoBotGateway` 代替真实 Agent，方便服务端先完成路由闭环。

## 2. 本 Step 边界

### 本 Step 做

- 新增 `BotOptions`，集中定义 `user_id=9001`、`username=mira_bot`、`mention=@mira_bot`。
- 新增 `BotGateway` 抽象，暴露 `onPrivateMessage()` 和 `onGroupMention()`。
- 新增 `EchoBotGateway`，返回 `Echo: <原消息>`。
- 新增 `BotService`，负责 bot 身份判断、bot 回复保存、push、offline/unread 处理。
- 用户私聊 `mira_bot` 时，保存用户原始消息并返回普通 `PrivateMessageResponse`。
- Bot 私聊回复作为普通私聊消息保存，再给原用户发送普通 `PrivateMessagePush`。
- 群聊中只有文本包含 `@mira_bot` 且 `mira_bot` 是该群成员时才触发 bot 回复。
- 群聊原始消息和 bot 回复都过滤 `mira_bot` 的 offline/unread。

### 本 Step 不做

- 不修改 MySQL schema，不新增 `users.user_type`。
- 不要求 seed 里的 `mira_bot` 当前能用真实密码登录。
- 不启用 `BotChatRequest` / `BotChatResponse` / `BotMessagePush`。
- 不伪造 bot session，不递归调用 `handlePrivateMessage()` 或 `handleGroupMessage()`。
- 不接入 Python BotClient、FastAPI、LangGraph、OpenAI、RAG 或 PersonaAgent。
- 不实现跨节点 bot 路由、消息可靠 ACK、已读回执或复杂 mention 解析。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/service/BotGateway.hpp` | 新增 | 定义 Bot 配置、回复结构、Gateway 抽象和 Echo 实现接口 |
| `include/liteim/service/BotService.hpp` | 新增 | 定义 bot 身份识别、私聊回复和群聊 mention 处理入口 |
| `src/service/BotService.cpp` | 新增 | 实现 EchoBot、bot 回复保存、push、offline/unread 过滤 |
| `include/liteim/service/ChatService.hpp` / `src/service/ChatService.cpp` | 修改 | 注入可选 `BotService`，私聊 `mira_bot` 时触发 bot 回复 |
| `include/liteim/service/GroupService.hpp` / `src/service/GroupService.cpp` | 修改 | 注入可选 `BotService`，过滤 bot offline/unread 并处理群聊 mention |
| `server/main.cpp` | 修改 | 创建 `EchoBotGateway` / `BotService` 并注入聊天服务 |
| `src/service/CMakeLists.txt` | 修改 | 编译 `BotService.cpp` |
| `tests/service/chat_service_test.cpp` | 修改 | 覆盖私聊 bot 和普通私聊不受影响 |
| `tests/service/group_service_test.cpp` | 修改 | 覆盖群聊 mention、成员校验、非 mention、回环保护 |
| `README.md` / planning 文件 | 更新 | 记录 Step 41 runtime、边界和验证结果 |

## 4. 核心接口与契约

### `BotOptions`

```cpp
struct BotOptions {
    std::uint64_t user_id{9001};
    std::string username{"mira_bot"};
    std::string mention{"@mira_bot"};
};
```

`BotOptions` 是集中配置点，避免把 `9001` 散落在 `ChatService` 和 `GroupService` 的业务分支中。当前第一版不从配置文件读取，后续如果要支持多个 bot 或可配置 bot，再扩展这里。

### `BotGateway`

```cpp
class BotGateway {
public:
    virtual Status onPrivateMessage(const MessageRecord& message, BotReply& reply) = 0;
    virtual Status onGroupMention(const MessageRecord& message, BotReply& reply) = 0;
};
```

`BotGateway` 只负责“给定一条已经保存的用户消息，生成一条 bot 回复文本”。它不操作 MySQL、Redis、Session，也不关心私聊或群聊如何投递。

### `EchoBotGateway`

`EchoBotGateway` 是 Step 41 的占位实现，默认把回复文本写成：

```text
Echo: <原消息文本>
```

它代表 C++ 服务端当前只做 echo 和路由验证，不代表最终 PersonaAgent 能力。

### `BotService`

`BotService` 依赖 `IStorage`、`ICache`、`OnlineService` 和 `BotGateway`：

```cpp
BotService(IStorage& storage,
           ICache& cache,
           OnlineService& online_service,
           BotGateway& gateway,
           BotOptions options = {});
```

契约：

- 不拥有 storage/cache/online/gateway，只保存引用。
- 运行在业务线程中，不进入 Reactor I/O 线程执行 MySQL/Redis。
- `isBotUser()` 用集中 bot id 判断身份。
- `shouldHandleGroupMention()` 同时检查群消息、发送者不是 bot、文本包含 mention、bot 是群成员。
- `handlePrivateMessageToBot()` 保存一条普通 bot 私聊回复，并向原用户 session 发送 `PrivateMessagePush`。
- `handleGroupMention()` 保存一条普通 bot 群聊回复，向在线人类成员发送 `GroupMessagePush`，给离线人类成员写 offline/unread。

## 5. 运行流程

### 1. 私聊 `mira_bot`

真实数据例子：

```text
sender_id = 1001
receiver_id = 9001
username = mira_bot
conversation_id = 10019001
MessageText = "hello mira"
```

流程：

```text
PrivateMessageRequest
    -> ChatService 校验登录用户和 receiver_id
    -> 发现 receiver_id == BotOptions.user_id
    -> 保存 1001 -> 9001 的原始私聊消息
    -> 不把 9001 放入 offline_user_ids
    -> 不给 9001 增加 Redis unread
    -> BotService 调用 EchoBotGateway
    -> 保存 9001 -> 1001 的普通私聊回复
    -> 给 Alice 当前 session 发送 PrivateMessagePush
    -> 返回原始消息的 PrivateMessageResponse
```

客户端看到的是普通私聊 push，不需要理解新的 bot 专用协议。

### 2. 群聊 `@mira_bot`

真实数据例子：

```text
group_id = 2001
sender_id = 1001
bot_user_id = 9001
MessageText = "hi @mira_bot"
```

触发条件：

```text
发送者是群成员
    && 文本包含 @mira_bot
    && mira_bot 也是该群成员
    && 发送者不是 mira_bot
```

流程：

```text
GroupMessageRequest
    -> GroupService 校验群存在和发送者是群成员
    -> 保存用户原始群消息
    -> 推送给在线人类成员
    -> 给离线人类成员写 offline/unread
    -> 过滤 mira_bot 的 offline/unread
    -> BotService 调用 EchoBotGateway
    -> 保存 mira_bot 发出的普通群消息
    -> 推送给在线人类成员，包括原发送者
    -> 给离线人类成员写 bot 回复的 offline/unread
```

如果 `mira_bot` 不是群成员，即使文本包含 `@mira_bot`，也不会触发 bot 回复。

## 6. 关键实现点

### 1. bot 是普通用户，不是特殊协议

Step 41 没有启用 `BotChatRequest`、`BotChatResponse` 或 `BotMessagePush`。这些枚举先保留给后续扩展；当前客户端只需要按普通私聊和群聊处理。

### 2. 显式过滤 bot offline/unread

当前 `ChatService` 和 `GroupService` 的普通规则是：找不到在线 session 的接收者或群成员会进入 `offline_user_ids`，并增加 Redis unread。`mira_bot` 没有真实在线 session，所以 Step 41 必须显式过滤：

```text
receiver_id == 9001 -> 不写 bot offline/unread
group member == 9001 -> 不写 bot offline/unread
```

否则服务端 EchoBot 会不断给自己堆离线消息和未读计数。

### 3. bot 回复不递归 handler

Bot 回复不构造假的 `PrivateMessageRequest` 或 `GroupMessageRequest`，也不伪造 session。`BotService` 直接构造 `MessageRecord`，通过 `IStorage::saveMessageWithOfflineRecipients()` 保存，再通过 `Session::sendPacket()` 投递 push。

这样可以避免：

- handler 递归。
- bot 回复再次触发 bot。
- 把 bot 当成真实在线连接。
- 后续 Python BotClient 接入时和 C++ EchoBot 逻辑混在一起。

### 4. Redis unread 失败保持既有降级语义

消息已经写入 MySQL 后，Redis unread 增加失败只记录 warning，不把已保存消息改成发送失败。这和 Step 36 / Step 38 的消息语义保持一致。

## 7. 测试设计

| 风险 | 测试覆盖 |
| --- | --- |
| 私聊 bot 被当成普通离线用户 | 私聊 `mira_bot` 时断言 `offline_user_ids` 为空、unread 不给 9001 增加 |
| bot 私聊回复没有普通消息形态 | 断言 bot 回复保存为 `sender_id=9001`、`receiver_id=1001`，并发送 `PrivateMessagePush` |
| BotService 影响普通私聊 | 使用启用 bot 的 `ChatService` 给 Bob 发消息，确认普通 offline/unread 保持 |
| 群聊原始消息给 bot 写离线 | 群成员包含 `9001` 时，普通群消息 offline list 只包含离线人类成员 |
| 非成员 bot 被群聊触发 | 文本包含 `@mira_bot` 但 bot 不在群成员列表时，不生成 bot 回复 |
| 非 mention 群消息误触发 | 群成员包含 bot 但文本不含 mention 时，不生成 bot 回复 |
| bot 回复回环 | sender 是 `9001` 且文本包含 mention 时，只保存原始消息，不再次 echo |
| 在线成员 push 顺序和内容 | 群聊 mention 测试中 Bob 收到原始消息和 bot 回复，Alice 收到 bot 回复 |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
ctest --test-dir build -R "ChatService|GroupService|Bot" --output-on-failure
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build --output-on-failure
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
timeout 2s ./build/server/liteim_server || test $? -eq 124
```

## 9. 面试表达

一句话：

> 我把 AI Bot 先抽象成一个普通 IM 用户，通过 `BotGateway` 生成回复，但消息仍然走普通私聊和群聊协议。

展开说：

> Step 41 没有引入新的 bot 专用协议，也没有修改用户表结构。服务端通过集中配置识别 seed 用户 `mira_bot=9001`，用户私聊 bot 时仍保存一条普通私聊消息，然后 `BotService` 调用 `EchoBotGateway` 生成一条普通私聊回复；群聊只有在文本包含 `@mira_bot` 且 bot 是群成员时才触发回复。因为 bot 是服务端内置 echo 能力，没有真实在线 session，所以我显式过滤了 bot 自己的 offline message 和 unread 计数，但离线人类成员仍按普通消息规则处理。

容易被追问：

> 面试官可能会问为什么不直接接 LangGraph 或新协议。回答时强调：C++ IM 服务端只负责消息路由和协议边界，真实 PersonaAgent 作为 Python BotClient 后续接入；第一版 EchoBot 是为了先验证普通用户身份、存储、push、群聊 mention 和离线语义。

## 10. 面试常见追问

### Q1：为什么不新增 `users.user_type`？

当前 v1 schema 没有这个字段，Step 41 不需要为了 EchoBot 临时改表。`BotOptions` 已经可以集中识别 seed 用户。后续如果多个 bot、管理后台或权限需要数据库级区分，再单独做 schema migration。

### Q2：为什么不启用 `BotChatRequest`？

因为 Step 41 的目标是“bot 作为普通 IM 用户”。普通用户给 bot 发消息应该和给 Bob 发消息一样走 `PrivateMessageRequest`；群聊 @bot 也应该是普通 `GroupMessageRequest`。专用协议可以保留给后续 BotClient 管理或 Agent 控制面。

### Q3：为什么不给 `mira_bot` 写 offline/unread？

`mira_bot` 当前不是一个真实登录的客户端 session，而是 C++ 服务端内置 EchoBot。如果按普通离线用户处理，所有发给 bot 的消息都会堆到 `offline_messages(user_id=9001)` 和 unread 计数里，既没有消费方，也会污染在线状态展示。

### Q4：为什么群聊要确认 bot 是群成员？

这保持“bot 是普通 IM 用户”的语义。普通用户不能在自己没加入的群里发言，bot 也不能。只有 `mira_bot` 在群成员列表里时，`@mira_bot` 才会触发回复。

### Q5：为什么 bot 回复不递归调用 `handlePrivateMessage()`？

递归 handler 需要伪造 request、session 和登录身份，容易引入回环和边界混乱。`BotService` 直接保存普通 `MessageRecord` 并投递 push，更清楚地表达“这是服务端生成的一条普通 bot 消息”。

### Q6：后续 PersonaAgent 怎么接？

后续 Python BotClient 会作为 `mira_bot` 登录 LiteIM，使用同一套 TLV 协议收发消息，再调用独立 AgentService。到那时可以关闭或替换 C++ `EchoBotGateway`，让 bot 回复来自真实 Python Agent。
