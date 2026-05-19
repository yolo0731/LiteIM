# Step 52：实现 Qt 心跳、断线提示、本地设置和体验打磨

## 0. 本 Step 结论

Step 52 是 Qt 客户端阶段的稳定性收尾：

- 登录成功后 `ClientRuntime` 每 30 秒发送一次 `HeartbeatRequest`。
- `HeartbeatResponse` 由 runtime 自己消费，不会被认证层或聊天层误处理。
- 主窗口状态栏显示 `Online`、`Offline`、`Connecting...` 或连接错误。
- 连接断开后支持手动 `Reconnect`，并支持一次自动重连。
- 登录窗口继续用 `QSettings` 保存服务器地址、端口和最近用户名。
- 消息发送失败时，最新 outgoing 气泡显示 `Failed`。
- 切换会话时继续清理本地未读数。
- 截图交付采用 README 中的运行和截图说明，不引入额外截图依赖。

## 1. 为什么需要这个 Step

Step 51 已经能把 Qt UI 操作转成真实协议包，但客户端还缺少演示时必须有的稳定性反馈：

```text
服务端断开
    -> 以前：UI 不清楚当前是否离线
    -> 现在：状态栏显示 Offline，Reconnect 可点击

用户离线时发送消息
    -> 以前：本地气泡一直停在 Sending
    -> 现在：气泡变成 Failed
```

心跳的作用也很直接：Qt 客户端定期告诉服务端“这个连接还活着”，服务端 `HeartbeatService` 会返回 `HeartbeatResponse`，并在登录态存在时刷新 Redis 在线 TTL。

## 2. 本 Step 边界

本 Step 做：

- `ClientRuntime` 保存连接 endpoint。
- `ClientRuntime` 管理 30 秒心跳定时器。
- `ClientRuntime` 处理连接状态和一次自动重连。
- `MainWindow` 状态栏展示连接状态和重连按钮。
- `ChatPage` 支持把最新发送中气泡改成成功或失败。
- `AuthController` / `ChatController` 只消费自己负责的 pending response，避免心跳、登录、聊天响应互相抢。
- 测试覆盖心跳、断线提示、手动重连、自动重连、设置保存和发送失败 UI。

本 Step 不做：

- 不修改服务端协议。
- 不修改 MySQL schema。
- 不修改 Redis key。
- 不做本地 SQLite 缓存。
- 不做复杂主题系统。
- 不做系统托盘。
- 不保存密码，也不实现断线后的自动重新登录。
- 不实现可靠投递 ACK、重试队列或离线本地草稿。

## 3. 文件变化

| 文件 | 类型 | 作用 |
| --- | --- | --- |
| `client_qt/include/liteim_client/app/ClientRuntime.hpp` | 修改 | 增加 endpoint、心跳、重连、连接状态接口 |
| `client_qt/src/app/ClientRuntime.cpp` | 修改 | 实现心跳发送、状态更新和一次自动重连 |
| `client_qt/include/liteim_client/network/ClientSession.hpp` | 修改 | 增加 pending request 只读查询 |
| `client_qt/src/network/ClientSession.cpp` | 修改 | 实现 pending request 查询 |
| `client_qt/src/auth/AuthController.cpp` | 修改 | 只处理注册/登录响应，登录成功后启动心跳 |
| `client_qt/src/chat/ChatController.cpp` | 修改 | 只处理聊天相关响应 |
| `client_qt/include/liteim_client/ui/MainWindow.hpp` | 修改 | 增加状态栏、重连和失败处理成员 |
| `client_qt/src/ui/MainWindow.cpp` | 修改 | 接线 runtime 状态、重连按钮和发送失败反馈 |
| `client_qt/include/liteim_client/ui/ChatPage.hpp` | 修改 | 增加最新 outgoing 气泡状态更新接口 |
| `client_qt/src/ui/ChatPage.cpp` | 修改 | 实现最新发送中气泡成功/失败更新 |
| `client_qt/resources/qss/app.qss` | 修改 | 增加状态栏和重连按钮样式 |
| `client_qt/tests/qt_client_test.cpp` | 修改 | 增加 Step52 Qt 测试 |
| `client_qt/tests/CMakeLists.txt` | 修改 | 注册 `LiteIMQtClient.Step52` |

## 4. 核心接口与契约

### `ClientRuntime`

```cpp
void setConnectionEndpoint(const QString& host, quint16 port);
void reconnect();
void startHeartbeat(int interval_ms = 30000);
void stopHeartbeat();
bool isOnline() const noexcept;

signals:
    void connectionStatusChanged(QString status_text, bool online);
```

契约：

- `ClientRuntime` 是 Qt 客户端连接状态的唯一入口。
- 登录成功后启动心跳。
- 断线时停止心跳。
- 自动重连只尝试一次。
- 重连只是 TCP 连接恢复；第一版不保存密码，也不做自动重新登录。

### `ClientSession`

```cpp
std::optional<PendingRequest> pendingRequest(std::uint64_t seq_id) const;
std::optional<PendingRequest> takePending(std::uint64_t seq_id);
```

契约：

- `pendingRequest()` 只看不删。
- `takePending()` 才真正移除。
- 认证层、聊天层、runtime 都先判断 request 类型，再决定是否消费这个 response。

## 5. 运行流程

### 1. 登录后启动心跳

```text
LoginWindow
    -> AuthController::login()
    -> LoginResponse
    -> ClientSession::markLoggedIn(...)
    -> ClientRuntime::startHeartbeat()
    -> 每 30 秒发送 HeartbeatRequest
```

### 2. 心跳响应回到客户端

```text
TcpClient::packetReceived(HeartbeatResponse)
    -> ClientRuntime::handlePacketReceived()
    -> pendingRequest(seq_id) 是 HeartbeatRequest
    -> takePending(seq_id)
    -> 状态保持 Online
```

这里不会进入 `AuthController` 或 `ChatController` 的业务响应处理。

### 3. 连接断开

```text
QTcpSocket disconnected
    -> TcpClient::disconnected
    -> ClientRuntime::handleDisconnected()
    -> stopHeartbeat()
    -> emit connectionStatusChanged("Offline", false)
    -> 如果开启自动重连且还没用过，延迟调用 reconnect()
```

### 4. 主窗口状态栏

```text
ClientRuntime::connectionStatusChanged(...)
    -> MainWindow::updateConnectionStatus(...)
    -> connectionStatusLabel 更新文字
    -> reconnectButton 根据 online/endpoint 启用或禁用
    -> ChatPage 顶部 Online/Offline 同步更新
```

### 5. 发送失败

```text
ChatInputBar Send
    -> ChatPage 先追加 Sending 气泡
    -> ChatController 发送失败或收到 ErrorResponse
    -> MainWindow::handleRequestFailed(...)
    -> ChatPage::markLatestOutgoingFailed()
    -> 最新 outgoing 气泡显示 Failed
```

## 6. 关键实现点

### 1. pending response 不能被抢

Qt 里多个对象都监听 `TcpClient::packetReceived`：

```text
ClientRuntime
AuthController
ChatController
```

如果某个对象不判断 request 类型就 `takePending()`，就可能把别人的响应拿走。Step 52 增加 `pendingRequest()`，先看类型，再消费。

### 2. 心跳属于连接层

心跳不是聊天消息，也不是登录消息，所以放在 `ClientRuntime`：

```text
ClientRuntime
    -> 知道 socket 是否连接
    -> 知道 session 是否登录
    -> 知道 endpoint 和重连策略
```

这样 UI 层只显示状态，不需要自己拼 `HeartbeatRequest`。

### 3. 发送失败只做 UI 反馈

本 Step 的失败状态不是可靠消息系统。它只说明：

```text
这次 Qt 客户端发送请求没有成功完成
```

不代表服务端已经永久丢弃，也不做自动重试。可靠 ACK、重试队列、离线草稿以后再设计。

### 4. 重连不等于重新登录

当前服务端登录态绑定在 TCP session 上。Step 52 不保存密码，也没有 token re-auth 协议，所以自动重连只恢复 TCP 连接和 UI 状态，不自动重新登录账号。

## 7. 测试设计

| 风险 | 测试 |
| --- | --- |
| 心跳定时器没有发包 | `QtClientRuntimeStep52Test.HeartbeatTimerSendsHeartbeatRequest` |
| 断线后状态不变 | `QtClientRuntimeStep52Test.DisconnectedSocketReportsOfflineAndManualReconnectWorks` |
| 手动重连不可用 | `QtMainWindowStep52Test.StatusBarShowsOfflineAndReconnectButtonReconnects` |
| 自动重连无限循环 | `QtClientRuntimeStep52Test.AutoReconnectRunsOnlyOncePerDisconnect` |
| AuthController 抢走聊天响应 | `QtClientRuntimeStep52Test.AuthControllerLeavesChatResponsesForChatController` |
| 离线发送一直显示 Sending | `QtMainWindowStep52Test.SendFailureMarksOutgoingBubbleFailed` |
| 本地设置没有保存 | `QtLoginWindowStep52Test.SavesAndReloadsRecentConnectionSettings` |

## 8. 验证命令

```bash
cmake --build build-qt --target liteim_qt_client_tests -j2
ctest --test-dir build-qt -R LiteIMQtClient.Step52 --output-on-failure
ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMQtClient.Step50|LiteIMQtClient.Step51|LiteIMQtClient.Step52|LiteIMCMake.QtClientFoundation" --output-on-failure
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib QT_QPA_PLATFORM=offscreen timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124
cmake -S . -B build
cmake --build build --target liteim_tests -j2
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

这个 Step 可以这样讲：

> 我在 Qt 客户端增加了连接运行时层，由 `ClientRuntime` 管理 endpoint、心跳、连接状态和一次自动重连。登录成功后客户端定时发送 `HeartbeatRequest`，断线后状态栏展示 Offline 并提供手动重连，发送失败时把本地消息气泡改成 Failed。这里还修了一个 Qt 信号槽下的 pending response 归属问题：认证层、聊天层和心跳层都先检查 request 类型，再消费对应 response，避免互相抢包。

## 10. 面试常见追问

**Q：为什么心跳放在 `ClientRuntime`，不是 `ChatController`？**

A：心跳是连接保活，不是聊天业务。`ClientRuntime` 正好拥有 socket、session、endpoint 和重连策略，所以放这里最清楚。

**Q：自动重连为什么只做一次？**

A：第一版避免无限重连打爆日志或反复弹状态。后续可以加指数退避和用户可配置策略。

**Q：重连后为什么不自动重新登录？**

A：当前协议没有 token re-auth，客户端也不应该为了重连长期保存密码。本 Step 只恢复 TCP 连接；完整重登录需要单独设计认证续期协议。

**Q：为什么要加 `pendingRequest()`？**

A：Qt 多个对象都会收到同一个 `packetReceived` 信号。如果直接 `takePending()`，可能把别的控制器的响应删掉。先看类型再消费，能保证响应归属清楚。

**Q：发送失败为什么只标记 UI，不自动重试？**

A：自动重试涉及幂等、消息去重、ACK 和本地持久化，这不是 Qt demo 收尾 Step 的边界。当前先给用户明确失败反馈。
