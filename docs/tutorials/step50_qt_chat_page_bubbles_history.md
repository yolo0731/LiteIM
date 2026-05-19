# Step 50：实现聊天窗口、消息气泡和历史消息加载

## 0. 本 Step 结论

Step 50 把右侧 `ChatPage` 从占位区升级成可用的聊天页：

- `MessageBubble` 渲染左右消息气泡、时间和发送状态。
- `ChatInputBar` 负责输入框、发送按钮、Enter 发送、Shift+Enter 换行。
- `ChatPage` 管理当前会话、消息列表、顶部加载更早消息入口和历史请求信号。
- 私聊和群聊共用同一套气泡组件；群聊 incoming 消息会显示发送者昵称。
- 当前 Step 只打通 Qt UI 行为和信号边界，真实私聊/群聊发包和服务端历史响应接入放到 Step 51。

## 1. 为什么需要这个 Step

Step 49 已经有会话列表，但右侧聊天区仍然只是占位。IM 客户端最核心的体验是：

```text
打开一个会话
    -> 拉最近历史
    -> 显示消息气泡
    -> 输入文本
    -> 先显示发送中
    -> 等服务端结果更新成功/失败
```

本 Step 先把这条 UI 主链路做成独立、可测试的组件。后续接真实 `PrivateMessageRequest`、`GroupMessageRequest`、`HistoryRequest` 时，不需要重写聊天页结构。

## 2. 本 Step 边界

本 Step 做：

- 右侧聊天页滚动消息区。
- 左右消息气泡。
- 文本自动换行。
- 发送时间显示。
- 发送中、成功、失败状态显示。
- 打开会话时发出最近历史请求信号。
- 点击顶部加载入口或滚动到顶部时，按最早 `message_id` 请求更早历史。
- 空消息不能发送。
- Enter 发送，Shift+Enter 换行。

本 Step 不做：

- 不修改服务端协议。
- 不修改 MySQL schema。
- 不修改 Redis key。
- 不实现真实好友/群组打开流程。
- 不把 `sendMessageRequested` 直接编码成私聊或群聊 packet。
- 不把 `historyRequested` 直接绑定到 `TcpClient`。
- 不实现断线重连和心跳状态栏。
- 不实现 Python PersonaAgent BotClient。

## 3. 文件变化

| 文件 | 类型 | 作用 |
| --- | --- | --- |
| `client_qt/include/liteim_client/ui/MessageBubble.hpp` | 新增 | 聊天气泡数据结构、方向、发送状态和 widget 接口 |
| `client_qt/src/ui/MessageBubble.cpp` | 新增 | 左右气泡、群聊发送者、时间和状态渲染 |
| `client_qt/include/liteim_client/ui/ChatInputBar.hpp` | 新增 | 输入栏接口和发送信号 |
| `client_qt/src/ui/ChatInputBar.cpp` | 新增 | 空输入禁用、Enter 发送、Shift+Enter 换行 |
| `client_qt/include/liteim_client/ui/ChatPage.hpp` | 修改 | 新增打开会话、设置消息、追加消息、历史/发送信号 |
| `client_qt/src/ui/ChatPage.cpp` | 修改 | 替换占位聊天区为滚动消息列表和输入栏 |
| `client_qt/resources/qss/app.qss` | 修改 | 补充聊天页、气泡、输入栏和发送按钮样式 |
| `client_qt/src/CMakeLists.txt` | 修改 | 注册新 Qt 组件 |
| `client_qt/tests/CMakeLists.txt` | 修改 | 注册 `LiteIMQtClient.Step50` |
| `client_qt/tests/qt_client_test.cpp` | 修改 | 新增 Step 50 Qt 测试 |
| `README.md` / process docs | 修改 | 同步 Step 50 边界和验证 |

## 4. 核心接口与契约

### `ChatMessage`

```cpp
struct ChatMessage {
    QString conversation_id;
    quint64 message_id;
    quint64 sender_id;
    QString sender_name;
    QString text;
    QDateTime sent_at;
    MessageDirection direction;
    MessageSendStatus status;
};
```

它是一条聊天消息，不是会话摘要。会话摘要仍然属于 Step 49 的 `ConversationModel`。

### `MessageBubble`

```cpp
MessageBubble(const ChatMessage& message,
              ConversationKind conversation_kind,
              QWidget* parent = nullptr);

void setStatus(MessageSendStatus status);
```

契约：

- incoming 消息靠左。
- outgoing 消息靠右。
- 私聊 incoming 不显示发送者昵称。
- 群聊 incoming 显示发送者昵称。
- outgoing 显示发送状态。

### `ChatInputBar`

```cpp
signals:
    void sendRequested(QString text);
```

契约：

- 输入为空或只包含空白时，发送按钮禁用。
- Enter 发送当前文本。
- Shift+Enter 插入换行。
- 成功发出 `sendRequested` 后清空输入框。

### `ChatPage`

```cpp
void openConversation(const QString& conversation_id,
                      const QString& title,
                      ConversationKind kind);

void setMessages(const QVector<ChatMessage>& messages);
void appendMessage(const ChatMessage& message);
void updateMessageStatus(quint64 message_id, MessageSendStatus status);

signals:
    void historyRequested(QString conversation_id, quint64 before_message_id);
    void sendMessageRequested(QString conversation_id, QString text);
```

契约：

- 打开会话时清空当前消息，并发出 `historyRequested(conversation_id, 0)`。
- 加载更早消息时使用当前列表里最早的非零 `message_id` 作为游标。
- 输入栏发送后，先追加一条本地 outgoing `Sending` 气泡，再发出 `sendMessageRequested`。
- 后续真实服务端响应可以用 `updateMessageStatus()` 把发送中改成成功或失败。

## 5. 运行流程

### 1. 打开会话

```text
ConversationListWidget / 后续真实联系人入口
    -> ChatPage::openConversation(conversation_id, title, kind)
    -> 更新右侧标题
    -> 清空旧消息
    -> 启用输入栏
    -> emit historyRequested(conversation_id, 0)
```

### 2. 历史消息返回

当前 Step 用本地接口表达这个行为：

```text
setMessages(messages)
    -> 过滤当前 conversation_id
    -> 重建 MessageBubble 列表
    -> 滚动到底部
```

### 3. 用户发送消息

```text
ChatInputBar
    -> Enter 或 Send 按钮
    -> emit sendRequested(text)
    -> ChatPage::handleSendRequested(text)
    -> append outgoing Sending bubble
    -> emit sendMessageRequested(conversation_id, text)
```

### 4. 收到 push

当前 Step 用本地接口表达这个行为：

```text
appendMessage(incoming_message)
    -> conversation_id 匹配当前会话
    -> 创建 incoming MessageBubble
    -> 私聊靠左
    -> 群聊靠左并显示 sender_name
```

### 5. 加载更早消息

```text
点击 Load earlier messages
或滚动到顶部
    -> 找到最早 message_id
    -> emit historyRequested(conversation_id, earliest_message_id)
```

## 6. 关键实现点

### 1. UI 先可测，真实协议后接入

`ChatPage` 不直接持有 `TcpClient`。它只发出两个信号：

```text
sendMessageRequested
historyRequested
```

这样 UI 组件保持简单，Step 51 再根据会话类型把信号转成 `PrivateMessageRequest`、`GroupMessageRequest` 或 `HistoryRequest`。

### 2. 气泡组件复用私聊和群聊

`MessageBubble` 只关心：

- 当前消息是 incoming 还是 outgoing。
- 当前会话是 private 还是 group。
- 当前消息状态是什么。

所以私聊和群聊不用两套 UI。群聊只是多显示一个发送者昵称。

### 3. 发送状态属于 outgoing 消息

incoming 消息不显示 `Sending / Sent / Failed`。这些状态只对本机发出的消息有意义。

### 4. 历史分页用 `message_id` 游标

服务端 Step 39 的历史查询已经支持 `before_message_id`。Qt 侧保留相同语义：

```text
before_message_id = 当前列表中最早的 message_id
```

这比按时间字符串分页稳定，因为 `message_id` 是服务端生成的单调标识。

## 7. 测试设计

| 风险 | 测试 |
| --- | --- |
| 打开会话不触发历史加载 | `QtChatPageTest.OpenConversationRequestsRecentHistory` |
| 空消息也能发送 | `QtChatPageTest.SendTextAppendsOutgoingBubbleAndRejectsEmptyInput` |
| 发送后不出现右侧发送中气泡 | `QtChatPageTest.SendTextAppendsOutgoingBubbleAndRejectsEmptyInput` |
| 收到私聊 push 后没有左侧气泡 | `QtChatPageTest.IncomingPrivatePushAppearsAsLeftBubble` |
| 群聊消息不显示发送者 | `QtChatPageTest.GroupIncomingMessageShowsSenderName` |
| 加载更早历史没有使用最早 `message_id` | `QtChatPageTest.LoadEarlierRequestsBeforeEarliestMessageId` |
| Enter / Shift+Enter 行为混乱 | `QtChatInputBarTest.EnterSendsAndShiftEnterInsertsNewLine` |

RED 记录：

```text
cmake --build build-qt --target liteim_qt_client_tests -j2
```

首次失败于：

```text
fatal error: liteim_client/ui/ChatInputBar.hpp: No such file or directory
```

说明测试先明确要求 Step 50 的输入栏和聊天页组件存在。

## 8. 验证命令

Qt Step 50：

```bash
cmake --build build-qt --target liteim_qt_client_tests -j2
ctest --test-dir build-qt -R LiteIMQtClient.Step50 --output-on-failure
```

Qt 回归：

```bash
cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2
ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMQtClient.Step50|LiteIMCMake.QtClientFoundation" --output-on-failure
```

Qt offscreen 启动：

```bash
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib \
QT_QPA_PLATFORM=offscreen \
timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124
```

默认构建回归：

```bash
cmake -S . -B build
cmake --build build --target liteim_tests -j2
ctest --test-dir build -L unit --output-on-failure
```

全量回归：

```bash
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

> Step 50 我把 Qt 右侧聊天区拆成三个清晰组件：`ChatPage` 管当前会话和消息列表，`MessageBubble` 管单条消息的左右气泡、时间和发送状态，`ChatInputBar` 管输入、Enter 发送和 Shift+Enter 换行。打开会话时，ChatPage 会发出 history 请求信号；加载更早消息时用最早的 message_id 作为游标。UI 不直接持有 TcpClient，真实私聊、群聊和历史包的编码在下一步统一接入，这样聊天页本身可以独立测试。

> 视觉上它参考常见微信式 IM 的三栏和左右气泡交互，但不使用微信品牌、logo、名称、图标、截图或素材。

## 10. 面试常见追问

### 为什么不让 `ChatPage` 直接发 `Packet`？

因为 `ChatPage` 是 UI 组件。它不知道当前会话最终要走私聊、群聊还是未来普通 PersonaAgent 账号私聊。让它只发信号，后续 controller 再编码 packet，边界更清楚。

### 为什么发送后先显示 `Sending`？

用户点击发送后应该立即看到自己的消息。服务端确认还没回来时先显示 `Sending`，成功响应后改成 `Sent`，失败响应或断线后改成 `Failed`。

### 为什么群聊要显示发送者昵称，私聊不用？

私聊左侧消息天然来自对方；群聊里左侧消息可能来自不同成员，所以需要在气泡上显示 `sender_name`。

### 为什么历史分页用 `message_id`，不用时间？

时间可能重复，也可能受客户端本地格式影响。`message_id` 是服务端持久化消息的稳定游标，更适合做 `before_message_id` 分页。

### 当前 Step 还缺什么？

还缺真实点击会话打开私聊/群聊、把发送信号编码成协议包、解析历史响应、接收真实 push、心跳断线状态和本地设置打磨。这些属于 Step 51 和 Step 52。
