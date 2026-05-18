# Step 48：实现 Qt 常见 IM 三栏主窗口

## 0. 本 Step 结论

- 目标：Step 48 把登录后的空 `MainWindow` 改成常见 IM 三栏主窗口。
- 主要交付：新增 `SideBar`、`ConversationListWidget`、`ChatPage`，并由 `MainWindow` 用 `QSplitter` 组合成三栏布局。
- UI 边界：左侧是功能导航，中间是当前功能对应的占位列表，右侧是聊天页占位和当前用户状态。
- 行为边界：本 Step 只做布局和左侧切换，不加载真实好友、群、会话、消息或未读数。
- 构建边界：默认 `build/` 仍不依赖 Qt；只有 `LITEIM_BUILD_QT_CLIENT=ON` 才构建 Qt 客户端。

## 1. 为什么需要这个 Step

Step 47 已经能登录并打开 `MainWindow`，但 `MainWindow` 还是空白窗口。Step 48 的作用是先把客户端主界面骨架立起来：

- 左侧固定功能栏承载消息、联系人、群组、普通 Agent 联系人入口和设置。
- 中间区域根据左侧功能切换不同列表。
- 右侧聊天页预留后续消息展示、输入框和会话标题。
- 顶部展示当前用户昵称和在线状态。

这样 Step 49 做会话、联系人、群组和未读数时，可以直接往已有区域里填 model 数据，而不是同时处理布局和业务数据。

## 2. 本 Step 边界

### 本 Step 做

- 新增 `SideBar`，提供五个左侧功能按钮。
- 新增 `ConversationListWidget`，用占位列表表示消息、联系人、群组、Agent 和设置区域。
- 新增 `ChatPage`，显示当前用户昵称、在线状态、聊天区占位和输入框占位。
- `MainWindow` 使用 `QSplitter` 组合左、中、右三栏。
- 样式统一写入 `resources/qss/app.qss`。
- 增加 Qt 单测覆盖三栏对象、比例、功能切换和 resize。

### 本 Step 不做

- 不实现 `ConversationModel`、`ContactListWidget` 或真实数据模型。
- 不向服务端请求好友列表、群列表、历史消息或离线消息。
- 不实现未读红点、会话置顶、消息气泡或发送输入。
- 不实现心跳重连、断线提示或 push 消息刷新。
- 不加入 PersonaAgent、LLM 或任何特殊 AI 身份逻辑。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `client_qt/include/liteim_client/SideBar.hpp` | 新增 | 左侧功能导航栏 |
| `client_qt/src/SideBar.cpp` | 新增 | 五个导航按钮和选中状态 |
| `client_qt/include/liteim_client/ConversationListWidget.hpp` | 新增 | 中间列表区域 |
| `client_qt/src/ConversationListWidget.cpp` | 新增 | 根据功能切换占位列表 |
| `client_qt/include/liteim_client/ChatPage.hpp` | 新增 | 右侧聊天页 |
| `client_qt/src/ChatPage.cpp` | 新增 | 顶部用户状态、聊天区和输入占位 |
| `client_qt/include/liteim_client/MainWindow.hpp` | 修改 | 持有三栏子组件 |
| `client_qt/src/MainWindow.cpp` | 修改 | 使用 `QSplitter` 组合三栏并连接切换 |
| `client_qt/resources/qss/app.qss` | 修改 | 三栏布局和导航按钮样式 |
| `client_qt/tests/qt_client_test.cpp` | 修改 | 新增 Step 48 Qt 主窗口测试 |
| `client_qt/CMakeLists.txt` | 修改 | 注册新源文件和 `LiteIMQtClient.Step48` |
| `README.md` / process 文件 | 更新 | 记录 Step 48 边界和验证结果 |

## 4. 核心接口与契约

### `SideBar`

```cpp
class SideBar final : public QWidget {
signals:
    void sectionSelected(QString section_id);

public:
    void setActiveSection(const QString& section_id);
};
```

契约：

- 左侧按钮只表示当前 UI 区域，不直接访问网络或业务数据。
- 点击按钮发出 `sectionSelected(section_id)`。
- `section_id` 使用简单字符串：`messages`、`contacts`、`groups`、`agent`、`settings`。

### `ConversationListWidget`

```cpp
class ConversationListWidget final : public QWidget {
public:
    void setSection(const QString& section_id);
};
```

契约：

- 中间区域根据 `section_id` 切换标题和占位列表。
- 本 Step 只放占位文本，不保存真实会话、联系人、群组或未读状态。
- 后续 Step 49 可以把这里替换成 model-driven 列表。

### `ChatPage`

```cpp
class ChatPage final : public QWidget {
public:
    void setCurrentUser(const QString& nickname, bool online);
    void setActiveSection(const QString& section_title);
};
```

契约：

- 顶部显示当前用户昵称和在线状态。
- 右侧聊天区域暂时只显示占位内容。
- 输入框存在但禁用，避免 Step 48 提前实现发消息。

## 5. 运行流程

### 1. 登录成功后打开主窗口

```text
LoginWindow::loginSucceeded
    -> ClientApp creates MainWindow
    -> MainWindow::buildUi()
    -> SideBar + ConversationListWidget + ChatPage
    -> show MainWindow
```

### 2. 主窗口初始化

```text
MainWindow::buildUi()
    -> create QSplitter(horizontal)
    -> add SideBar fixed width
    -> add ConversationListWidget medium width
    -> add ChatPage stretch area
    -> switchSection("messages")
```

### 3. 左侧功能切换

```text
SideBar button clicked
    -> emit sectionSelected(section_id)
    -> MainWindow::switchSection(section_id)
    -> SideBar::setActiveSection(section_id)
    -> ConversationListWidget::setSection(section_id)
    -> ChatPage::setActiveSection(title)
```

## 6. 关键实现点

### 1. 三栏布局用 `QSplitter`

`QSplitter` 比手写固定像素更适合主窗口 resize。左栏固定为 88 像素窄导航，中栏限制在 280-360 像素，右侧聊天页使用 stretch，占据剩余空间。

### 2. Step48 只做 UI shell

`ConversationListWidget` 目前只有占位数据。这是有意控制边界：如果本 Step 同时接入好友/群/未读模型，会把 Step 49 的范围提前混进来。

### 3. Agent 是普通联系人入口

左侧有 `Agent` 入口，但它只是普通联系人入口的 UI 占位。LiteIM C++ 服务端不识别 AI 身份，后续 PersonaAgent 仍然作为普通账号接入。

### 4. 样式集中在 QSS

主窗口、侧边栏、中间列表、聊天页、导航按钮、状态文本和输入占位样式都写在 `app.qss`，避免样式散落在 C++ 逻辑里。

## 7. 测试设计

| 风险 | 测试覆盖 |
| --- | --- |
| 主窗口仍是空白窗口 | `QtMainWindowTest.StartsWithThreeColumnChatLayout` 检查 `mainSplitter`、`sideBar`、`conversationListWidget`、`chatPage` |
| 左侧按钮缺失 | 同一测试检查消息、联系人、群组、Agent、设置按钮 |
| 当前用户状态缺失 | 同一测试检查 `currentUserNicknameLabel` 和 `onlineStatusLabel` |
| 切换按钮没有改变中间区域 | `QtMainWindowTest.SidebarButtonsSwitchMiddleArea` |
| resize 后布局不可用 | `QtMainWindowTest.ResizeKeepsColumnsUsable` |
| Step46/47 回归 | `LiteIMQtClient.Step46`、`LiteIMQtClient.Step47`、`LiteIMQtClient.Step48` 一起跑 |

## 8. 验证命令

Qt-enabled 构建和 Step 48 测试：

```bash
cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-qt --target liteim_qt_client_tests -j2
ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48" --output-on-failure
cmake --build build-qt --target liteim_qt_client -j2
```

默认构建边界验证：

```bash
cmake -S . -B build
cmake --build build --target liteim_tests -j2
ctest --test-dir build -L unit --output-on-failure
```

完整验证：

```bash
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build --output-on-failure
```

## 9. 面试表达

> Step 48 我把 Qt 客户端从登录后的空窗口升级成了常见 IM 三栏主界面。左侧 `SideBar` 放消息、联系人、群组、Agent 普通联系人入口和设置；中间 `ConversationListWidget` 根据左侧选择切换列表区域；右侧 `ChatPage` 展示聊天区占位、当前用户昵称和在线状态。布局用 `QSplitter`，左栏固定，中栏限制宽度，右栏自适应拉伸。

展开说：

> 这个 Step 我刻意没有接入真实会话模型、好友列表或未读数，因为这些属于下一步的数据模型和业务交互。这样做能保证 UI 结构先稳定，后续只需要往中间列表和右侧聊天页填数据。

## 10. 面试常见追问

### 为什么不用一个巨大 `MainWindow.cpp` 写完所有 UI？

因为三栏各自职责很清楚。`SideBar` 管导航，`ConversationListWidget` 管中间列表，`ChatPage` 管右侧聊天页。拆成三个 widget 后，后续替换真实数据模型时改动范围更小。

### 为什么中间列表现在只是占位数据？

Step48 的目标是布局，不是数据。真实会话、联系人、群组和未读数会在 Step49 接入。提前混进来会让这个 Step 变得难测，也会模糊边界。

### 为什么用 `QSplitter`？

`QSplitter` 天然适合三栏布局和窗口 resize。左侧导航可以固定宽度，中间列表限制宽度，右侧聊天页自动拿剩余空间。

### Agent 入口是不是特殊 AI 协议？

不是。这里的 Agent 只是一个普通联系人入口占位。LiteIM 服务端不会识别 AI 身份，后续 PersonaAgent 也通过普通账号登录和收发消息。

### resize 如何验证？

测试在 offscreen 环境下调整窗口尺寸，然后检查左栏、中栏、右栏仍有合理宽度和高度。它不验证视觉美观，只验证没有明显布局坍缩。
