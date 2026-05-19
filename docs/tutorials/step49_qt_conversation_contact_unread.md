# Step 49：实现会话列表、联系人列表和未读数

## 0. 本 Step 结论

Step 49 把 Step 48 的中间占位栏升级成 Qt 侧 model-driven 列表：

- `ConversationModel` 保存会话摘要、时间和未读数。
- `ConversationListWidget` 的 Messages 页面改用 `QListView + ConversationModel`。
- `ContactListWidget` 复用同一套列表渲染好友和群组。
- PersonaAgent 只作为一个普通联系人占位项出现，不进入 `SideBar` 顶级导航。
- 当前未读数是 Qt 本地临时计数，不直接读取 Redis；后续真实 push / offline / unread 接入时再和服务端数据对齐。

## 1. 为什么需要这个 Step

Step 48 只完成了三栏布局。真正的聊天客户端不能把会话文本直接写死在 widget 里，否则后续收到 push、加载好友、加载群组、更新未读红点时会到处改 UI。

本 Step 先把数据放进 model：

```text
服务端/本地事件
    -> ConversationModel
    -> QListView 显示会话摘要和未读数
```

这样后续接真实协议时，只需要更新 model，不需要重写三栏结构。

## 2. 本 Step 边界

本 Step 做：

- 会话 item 展示：头像文本、昵称、最后一条消息摘要、时间、未读数。
- 好友列表展示在线 / 离线状态。
- 群组列表展示群名称和成员数量。
- 普通 PersonaAgent 联系人占位项。
- 收到本地模拟 incoming message 时更新摘要、置顶会话、增加未读。
- 当前打开会话收到消息时不增加未读。

本 Step 不做：

- 不修改服务端协议。
- 不修改 MySQL schema。
- 不修改 Redis key。
- 不从服务端真实加载好友、群组和会话。
- 不实现真实发消息、历史消息加载、消息气泡或心跳断线提示。
- 不实现 Python PersonaAgent BotClient。
- 不在 C++ 服务端加入 AI 身份判断。

## 3. 文件变化

| 文件 | 类型 | 作用 |
| --- | --- | --- |
| `client_qt/include/liteim_client/model/ConversationModel.hpp` | 新增 | Qt 会话摘要模型接口 |
| `client_qt/src/model/ConversationModel.cpp` | 新增 | 会话列表、置顶、未读计数实现 |
| `client_qt/include/liteim_client/ui/ContactListWidget.hpp` | 新增 | 好友/群组列表复用 widget |
| `client_qt/src/ui/ContactListWidget.cpp` | 新增 | 联系人式列表渲染 |
| `client_qt/include/liteim_client/ui/ConversationListWidget.hpp` | 修改 | 接入 model、联系人列表、群组列表 |
| `client_qt/src/ui/ConversationListWidget.cpp` | 修改 | 中间栏改为 stack + model list，并绘制会话未读 badge |
| `client_qt/resources/qss/app.qss` | 修改 | 支持 `QListView` 和多种中间列表样式 |
| `client_qt/src/CMakeLists.txt` | 修改 | 注册 model / widget 源文件 |
| `client_qt/tests/CMakeLists.txt` | 修改 | 注册 `LiteIMQtClient.Step49` |
| `client_qt/tests/qt_client_test.cpp` | 修改 | 新增 Step 49 Qt 测试 |
| `README.md` / process docs | 修改 | 同步 Step 49 边界和验证 |

## 4. 核心接口与契约

### `ConversationItem`

```cpp
struct ConversationItem {
    QString conversation_id;
    ConversationKind kind;
    QString title;
    QString last_message;
    QString timestamp;
    QString avatar_text;
    int unread_count;
};
```

它是一条会话摘要，不是一条聊天消息。

### `ConversationModel`

核心接口：

```cpp
void setConversations(const QVector<ConversationItem>& conversations);

void applyIncomingMessage(const ConversationUpdate& update,
                          const QString& active_conversation_id = QString());

void markConversationRead(const QString& conversation_id);
```

契约：

- `setConversations()` 重置整个列表。
- `applyIncomingMessage()` 更新最后消息摘要，并把该会话放到列表顶部。
- 如果 incoming message 属于当前打开会话，不增加未读。
- 如果 incoming message 不属于当前打开会话，未读数加一。
- `markConversationRead()` 把指定会话未读清零。

### `ContactListWidget`

```cpp
struct ContactListItem {
    QString title;
    QString subtitle;
    QString avatar_text;
    bool online;
    int unread_count;
};
```

它不区分“好友”和“群组”，只是一个可复用的列表控件。好友用 subtitle 表示 Online / Offline，群组用 subtitle 表示成员数量。

## 5. 运行流程

### 1. 打开主窗口

```text
MainWindow
    -> 创建 SideBar
    -> 创建 ConversationListWidget
    -> 创建 ChatPage
```

### 2. 初始化中间栏

```text
ConversationListWidget
    -> 创建 ConversationModel
    -> 创建 Messages QListView
    -> 创建 Contacts ContactListWidget
    -> 创建 Groups ContactListWidget
    -> 创建 Settings QListWidget
    -> 写入本地 demo 数据
```

### 3. 用户点击左侧导航

```text
SideBar::sectionSelected(section_id)
    -> MainWindow::switchSection(section_id)
    -> ConversationListWidget::setSection(section_id)
    -> QStackedWidget 切换 Messages / Contacts / Groups / Settings
```

### 4. 收到一条消息

当前 Step 用本地接口表达这个行为：

```text
applyIncomingMessage(update, active_conversation_id)
    -> 找到已有会话，或创建新会话
    -> 更新 last_message / timestamp
    -> 如果不是当前打开会话，unread_count + 1
    -> 把会话移动到顶部
    -> QListView 自动刷新显示
    -> delegate 绘制头像、摘要、时间和红色未读 badge
```

## 6. 关键实现点

### 1. 数据不散落在 widget 里

Step 48 的 `ConversationListWidget` 是占位列表。Step 49 改成：

```text
ConversationModel owns data
QListView displays data
ConversationListWidget chooses which list is visible
```

这样后续服务端 push 到达时，只更新 model。

### 2. 会话 item 自己绘制 badge

Messages 页面不是简单显示一行文本，而是用 `QStyledItemDelegate` 绘制一条会话 row：

```text
avatar + title + last_message + timestamp + unread badge
```

这样未读数能以红色 badge 形式出现，后续接真实 push 时仍然只改 model 数据。

### 3. 未读数是本地临时计数

服务端 Redis 已经有 unread 计数，但 Qt 当前还没有真实好友/会话加载协议。因此本 Step 不直接读 Redis，而是在 Qt 侧先定义显示行为：

- 当前会话收到消息：未读不变。
- 非当前会话收到消息：未读加一。
- 打开/读完会话：未读清零。

这保证 UI 行为先可测，后续再把数据来源换成服务端。

### 4. PersonaAgent 是普通联系人

本 Step 的联系人 demo 数据里有一个 `PersonaAgent` 项。它只是普通联系人占位，不代表 C++ 服务端有特殊 Agent 协议。

正确边界是：

```text
未来 Python PersonaAgent BotClient
    -> 用普通账号登录 LiteIM
    -> 在 Qt 中显示为普通联系人/普通会话
```

## 7. 测试设计

| 风险 | 测试 |
| --- | --- |
| incoming message 不更新摘要 | `QtConversationModelTest.IncomingMessageUpdatesSummaryAndUnreadCount` |
| 当前会话收到消息也增加未读 | `QtConversationModelTest.ActiveConversationDoesNotIncreaseUnreadAndCanBeMarkedRead` |
| 收到消息后会话没有置顶 | `QtConversationModelTest.IncomingMessageMovesConversationToTopOrCreatesOne` |
| 联系人列表不显示在线状态和未读 | `QtContactListWidgetTest.RendersContactsWithOnlineStateAndUnreadBadge` |
| 主窗口没有接入 model-backed 中间栏 | `QtMainWindowStep49Test.ShowsModelBackedConversationContactAndGroupLists` |
| Step 48 三栏切换回归 | `QtMainWindowTest.SidebarButtonsSwitchMiddleArea` |

RED 记录：

```text
cmake --build build-qt --target liteim_qt_client_tests -j2
```

首次失败于：

```text
fatal error: liteim_client/model/ConversationModel.hpp: No such file or directory
```

说明测试先明确要求 Step 49 的 model 层存在。

## 8. 验证命令

Qt Step 49：

```bash
cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2
ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMCMake.QtClientFoundation" --output-on-failure
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

> Step 49 我没有把会话、联系人、群组和未读数直接写死在 Qt widget 里，而是先抽了一个 `ConversationModel`。中间栏的 Messages 页面用 `QListView` 绑定这个 model，联系人和群组用可复用的 `ContactListWidget` 渲染。收到消息时只更新 model：更新摘要、移动到顶部、按当前会话状态决定未读是否加一。这样后续接服务端 push 或离线未读数据时，UI 结构不用重写。

> PersonaAgent 在这里不是特殊 UI 分类，也不是 C++ 服务端内置 AI。它只是联系人列表里的普通账号占位。未来 Python BotClient 登录 LiteIM 后，也会像普通用户一样出现在联系人或会话列表里。

## 10. 面试常见追问

### 为什么用 `QAbstractListModel`，不用直接操作 `QListWidget`？

因为会话列表是数据驱动的。直接操作 `QListWidget` 简单，但后续 push、历史加载、未读刷新都会散落到 UI 代码里。`QAbstractListModel` 能把“数据怎么变”和“界面怎么显示”分开。

### 为什么 Step 49 的 unread 不直接读 Redis？

Qt 客户端还没有真实会话加载和未读同步协议。本 Step 先定义 UI 行为和本地临时计数，后续接服务端接口时再把数据来源替换成 Redis / offline pull / push。

### 为什么当前打开会话收到消息不加未读？

因为用户已经在看这个会话。未读数表示“用户还没看到的消息”，当前会话里的新消息应该直接进入聊天区，不应该增加红点。

### 为什么 PersonaAgent 不放左侧导航？

因为 PersonaAgent 是普通 LiteIM 账号，不是服务端特殊功能入口。放进联系人或会话列表，能保持“人类用户”和“外部 Agent 控制账号”在协议和 UI 上一致。

### 当前 Step 还缺什么？

还缺真实好友/群组加载、真实消息 push 接入、消息气泡、历史加载、发送消息、断线状态和心跳提示。这些属于后续 Qt Steps。
