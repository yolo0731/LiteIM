# Step 51：实现 Qt 私聊、群聊和外部 Agent 普通联系人项

## 0. 本 Step 结论

Step 51 把 Step 49/50 的 Qt UI 信号接到真实 LiteIM TLV 协议：

- `ClientRuntime` 统一保存 Qt 客户端的 `TcpClient` 和 `ClientSession`。
- `AuthController` 登录成功后把同一个 runtime 传给 `MainWindow`，主窗口继续使用已登录连接。
- `ChatController` 把 UI 动作转成 `HistoryRequest`、`PrivateMessageRequest`、`GroupMessageRequest`、`AddFriendRequest`、`CreateGroupRequest` 和 `JoinGroupRequest`。
- 点击好友打开私聊，点击群组打开群聊，输入文本后发送对应协议包。
- `PersonaAgent` 仍然只是普通联系人项，点击后走普通私聊，不引入特殊 AI 身份、特殊侧边栏或群聊 @ 触发。

## 1. 为什么需要这个 Step

Step 50 只完成了聊天页 UI：

```text
ChatPage
    -> emit historyRequested(...)
    -> emit sendMessageRequested(...)
```

这些信号还没有真正变成服务端能处理的 Packet。Step 51 的作用是补上 Qt 客户端的协议连接层：

```text
用户点击联系人 / 群组
    -> MainWindow 记录当前会话
    -> ChatPage 请求历史
    -> ChatController 发送 HistoryRequest

用户输入并发送文本
    -> ChatPage 先显示 Sending 气泡
    -> MainWindow 按会话类型分发
    -> ChatController 发送 PrivateMessageRequest 或 GroupMessageRequest
```

## 2. 本 Step 边界

本 Step 做：

- 登录窗口和主窗口共享 Qt 客户端 runtime。
- 联系人行、群组行、会话行携带普通用户或群组 id。
- 从好友列表打开私聊。
- 从群组列表打开群聊。
- 点击未来 PersonaAgent 联系人时打开普通私聊。
- 私聊发送 `PrivateMessageRequest`。
- 群聊发送 `GroupMessageRequest`。
- 打开会话发送 `HistoryRequest`。
- 收到 `PrivateMessagePush` / `GroupMessagePush` 后追加到当前聊天页并更新会话摘要。
- `ChatController` 支持添加好友、创建群、加入群的协议请求。

本 Step 不做：

- 不修改服务端协议。
- 不修改 MySQL schema。
- 不修改 Redis key。
- 不在 C++ 服务端加入 AI 身份判断。
- 不实现 Python PersonaAgent BotClient。
- 不实现心跳、断线提示和自动重连；这些留给 Step 52。
- 不实现完整好友/群组远程刷新列表；当前列表仍是 Qt demo seed data 加协议发送链路。

## 3. 文件变化

| 文件 | 类型 | 作用 |
| --- | --- | --- |
| `client_qt/include/liteim_client/app/ClientRuntime.hpp` | 新增 | 组合 Qt 端 `TcpClient` 和 `ClientSession` |
| `client_qt/src/app/ClientRuntime.cpp` | 新增 | runtime 访问接口实现 |
| `client_qt/include/liteim_client/chat/ChatController.hpp` | 新增 | 聊天和好友/群组协议控制器接口 |
| `client_qt/src/chat/ChatController.cpp` | 新增 | 构造请求、解析消息响应和 push |
| `client_qt/include/liteim_client/auth/AuthController.hpp` | 修改 | 改为持有 `ClientRuntime` 并暴露给登录窗口 |
| `client_qt/src/auth/AuthController.cpp` | 修改 | 登录/注册继续走同一 runtime |
| `client_qt/include/liteim_client/ui/LoginWindow.hpp` | 修改 | 暴露登录成功后的 runtime |
| `client_qt/src/ui/LoginWindow.cpp` | 修改 | 转发 `AuthController::runtime()` |
| `client_qt/src/app/ClientApp.cpp` | 修改 | 登录成功后创建 `MainWindow(login_window.runtime())` |
| `client_qt/include/liteim_client/model/ConversationModel.hpp` | 修改 | 会话项增加目标用户/群组 id |
| `client_qt/src/model/ConversationModel.cpp` | 修改 | 增加 `TargetIdRole` |
| `client_qt/include/liteim_client/ui/ContactListWidget.hpp` | 修改 | 联系人/群组行增加 id、conversation id 和激活信号 |
| `client_qt/src/ui/ContactListWidget.cpp` | 修改 | 把列表点击转换成带元数据的信号 |
| `client_qt/include/liteim_client/ui/ConversationListWidget.hpp` | 修改 | 对外发出 `conversationActivated` |
| `client_qt/src/ui/ConversationListWidget.cpp` | 修改 | demo 好友、群组、PersonaAgent 行携带真实协议目标 id |
| `client_qt/include/liteim_client/ui/MainWindow.hpp` | 修改 | 增加 runtime 注入、当前会话状态和协议连接方法 |
| `client_qt/src/ui/MainWindow.cpp` | 修改 | 连接列表、聊天页和 `ChatController` |
| `client_qt/src/CMakeLists.txt` | 修改 | 注册 `ClientRuntime` 和 `ChatController` |
| `client_qt/tests/CMakeLists.txt` | 修改 | 注册 `LiteIMQtClient.Step51` |
| `client_qt/tests/qt_client_test.cpp` | 修改 | 新增 Step 51 Qt 协议连接测试 |

## 4. 核心接口与契约

### `ClientRuntime`

```cpp
class ClientRuntime final : public QObject {
public:
    TcpClient& client() noexcept;
    ClientSession& session() noexcept;
};
```

契约：

- Qt 客户端一个登录会话使用一个 runtime。
- 登录阶段和主窗口阶段共享同一个 `TcpClient` 和 `ClientSession`。
- UI 组件不直接拥有新的 socket 连接。

### `ChatController`

```cpp
Status addFriend(std::uint64_t target_user_id);
Status createGroup(const QString& group_name);
Status joinGroup(std::uint64_t group_id);
Status sendPrivateMessage(std::uint64_t receiver_id, const QString& text);
Status sendGroupMessage(std::uint64_t group_id, const QString& text);
Status requestHistory(ConversationKind kind,
                      std::uint64_t conversation_id,
                      std::uint64_t before_message_id,
                      std::uint64_t limit = 20);
```

契约：

- `ChatController` 只负责协议请求和响应解析。
- `MainWindow` 决定当前 UI 会话是私聊还是群聊。
- 私聊会话 id 使用服务端同一套计算规则。
- `PersonaAgent` 不走单独 API；它只是 `receiver_id = 3001` 的普通私聊。

### `ConversationListWidget`

```cpp
signals:
    void conversationActivated(QString conversation_id,
                               QString title,
                               ConversationKind kind,
                               quint64 target_id);
```

契约：

- `conversation_id` 是 Qt UI 使用的字符串，例如 `private:1002` 或 `group:2001`。
- `target_id` 是协议使用的目标 id：私聊为对方 user_id，群聊为 group_id。
- 中间列表不直接发包，只告诉主窗口用户选中了什么。

## 5. 运行流程

### 1. 登录后进入主窗口

```text
LoginWindow
    -> AuthController::login()
    -> ClientRuntime::client() 发送 LoginRequest
    -> LoginResponse
    -> ClientRuntime::session().markLoggedIn(...)
    -> ClientApp 创建 MainWindow(login_window.runtime())
```

主窗口继续使用登录阶段的连接和 session。

### 2. 打开好友私聊

```text
Contacts 列表点击 Bob
    -> ContactListWidget::itemActivated("private:1002", "Bob", Private, 1002)
    -> ConversationListWidget::conversationActivated(...)
    -> MainWindow::openConversation(...)
    -> ChatPage::openConversation(...)
    -> emit historyRequested("private:1002", 0)
    -> ChatController::requestHistory(Private, 10011002, 0)
    -> TcpClient 发送 HistoryRequest
```

`10011002` 来自服务端和 Qt 共用的私聊 conversation id 规则。

### 3. 发送私聊消息

```text
ChatInputBar Send
    -> ChatPage 先追加 outgoing Sending 气泡
    -> emit sendMessageRequested("private:1002", "hello bob")
    -> MainWindow::sendMessage(...)
    -> ChatController::sendPrivateMessage(1002, "hello bob")
    -> TcpClient 发送 PrivateMessageRequest
```

### 4. 打开群聊并发送群消息

```text
Groups 列表点击 LiteIM Dev Group
    -> target_id = 2001
    -> HistoryRequest(ConversationType=Group, ConversationId=2001)

发送文本
    -> GroupMessageRequest(GroupId=2001, MessageText=...)
```

### 5. 收到普通 PersonaAgent 回复

```text
服务端 PrivateMessagePush(sender_id=3001, receiver_id=1001)
    -> TcpClient::packetReceived
    -> ChatController::messageReceived
    -> MainWindow::handleIncomingMessage
    -> 当前 conversation_id 是 private:3001
    -> ChatPage::appendMessage
```

整个流程没有 AI 特判。

## 6. 关键实现点

### 1. Runtime 共享避免登录态丢失

如果登录窗口用一个 `TcpClient`，主窗口再 new 一个新的 `TcpClient`，主窗口就没有登录态，也没有服务端绑定的 session。Step 51 引入 `ClientRuntime`，让登录和聊天使用同一个连接。

### 2. UI id 和协议 id 分开

Qt UI 用字符串 id：

```text
private:1002
group:2001
```

服务端协议用整数 id：

```text
ConversationType = 1 / 2
ConversationId = 10011002 / 2001
ReceiverId = 1002
GroupId = 2001
```

这样 UI 可读，协议仍保持服务端现有格式。

### 3. PersonaAgent 只是普通联系人

`PersonaAgent` 当前是 demo 联系人行：

```text
title = PersonaAgent
target_id = 3001
conversation_id = private:3001
kind = Private
```

点击它和点击 Bob 一样，都是普通私聊。

### 4. 避免重复打开会话

`ContactListWidget` 只在 `itemClicked` 时发出激活信号，不在 `currentRowChanged` 时发。否则测试或键盘选择可能导致一次点击发出两次 `HistoryRequest`。

## 7. 测试设计

| 风险 | 测试 |
| --- | --- |
| 好友/群组管理请求字段错误 | `QtChatControllerTest.SendsFriendAndGroupManagementRequests` |
| 点击好友没有发历史请求 | `QtMainWindowStep51Test.ContactSelectionOpensPrivateChatAndSendsPrivatePacket` |
| 私聊发送没有发 `PrivateMessageRequest` | `QtMainWindowStep51Test.ContactSelectionOpensPrivateChatAndSendsPrivatePacket` |
| 点击群组没有发群历史请求 | `QtMainWindowStep51Test.GroupSelectionOpensGroupChatAndSendsGroupPacket` |
| 群聊发送没有发 `GroupMessageRequest` | `QtMainWindowStep51Test.GroupSelectionOpensGroupChatAndSendsGroupPacket` |
| PersonaAgent 被当成特殊类型 | `QtMainWindowStep51Test.PersonaAgentContactIsOrdinaryPrivateChat` |
| PersonaAgent 普通私聊 push 不显示 | `QtMainWindowStep51Test.PrivatePushFromPersonaAgentAppearsInCurrentChat` |
| 前面 Qt 步骤回归 | `LiteIMQtClient.Step46` 到 `LiteIMQtClient.Step51` |

TDD 过程：

- RED 首次失败于缺少 `liteim_client/app/ClientRuntime.hpp`。
- GREEN 后 Step51 编译通过。
- 第一次 Step51 运行失败时读到第二个 `HistoryRequest`，原因是列表选择触发了两次；修复为只响应真实 `itemClicked` 后通过。

## 8. 验证命令

Qt-enabled 构建和 Step 51 测试：

```bash
cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2
ctest --test-dir build-qt -R LiteIMQtClient.Step51 --output-on-failure
ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMQtClient.Step50|LiteIMQtClient.Step51|LiteIMCMake.QtClientFoundation" --output-on-failure
```

Qt offscreen 启动：

```bash
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib \
QT_QPA_PLATFORM=offscreen \
timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124
```

默认服务端回归：

```bash
cmake -S . -B build
cmake --build build --target liteim_tests -j2
ctest --test-dir build -L unit --output-on-failure
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

> Step 51 我把 Qt 客户端从纯 UI 信号推进到真实协议连接。登录窗口和主窗口共享同一个 `ClientRuntime`，避免登录成功后主窗口重新建 socket 导致 session 丢失。主窗口只负责当前会话状态，`ChatController` 负责把 UI 动作转成服务端已有 TLV Packet，比如 `HistoryRequest`、`PrivateMessageRequest` 和 `GroupMessageRequest`。好友、群组和未来 PersonaAgent 都走普通账号协议，服务端不需要知道某个账号背后是不是 AI。

> 设计上我把 UI id 和协议 id 分开：UI 里用 `private:1002` / `group:2001` 这种可读字符串，发包时再转换成服务端需要的 `ConversationType`、`ConversationId`、`ReceiverId` 或 `GroupId`。这样 Qt 组件简单，协议仍和 CLI、Python E2E、未来 BotClient 保持一致。

## 10. 面试常见追问

### 为什么要加 `ClientRuntime`？

因为登录和聊天必须共享连接和登录态。没有 runtime，登录窗口登录成功后主窗口重新建连接，服务端并不知道这个新连接已经登录。

### 为什么不让 `ChatPage` 直接发 `PrivateMessageRequest`？

`ChatPage` 是 UI 组件，它不知道当前会话的协议目标 id，也不应该直接操作 socket。它只发 `sendMessageRequested`，由 `MainWindow + ChatController` 决定发私聊还是群聊。

### 为什么私聊 conversation id 是 `10011002`？

这是服务端已有规则：小用户 id 用 `min_id * 10000 + max_id` 生成私聊会话 id。Qt 端复用同一规则，才能让历史查询和服务端校验一致。

### PersonaAgent 为什么不做特殊分支？

因为 LiteIM 的边界是普通 IM 协议。未来 PersonaAgent 是外部 Python BotClient，登录成普通用户账号后收发消息。LLM、RAG、Persona、安全策略都在外部服务，不进入 C++ server。

### Step 51 为什么还不做断线重连？

断线提示、心跳和自动重连属于客户端稳定性体验，会影响定时器、socket 状态和 UI 提示，放到 Step 52 单独处理。
