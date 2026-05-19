# Step 47：实现 Qt 登录和注册窗口

## 0. 本 Step 结论

- 目标：Step 47 实现 Qt 客户端入口，让用户输入服务器地址、用户名和密码后能发起登录或注册。
- 主要交付：新增 `AuthController`、`LoginWindow`、`RegisterDialog`，并让 `main.cpp` 先显示登录窗口。
- 协议边界：登录和注册仍然使用服务端已有 `RegisterRequest` / `LoginRequest` / `RegisterResponse` / `LoginResponse` / `ErrorResponse`。
- UI 边界：窗口只收集输入、显示状态和响应信号，不直接操作 `QTcpSocket`。
- 构建边界：默认 `build/` 仍不依赖 Qt；只有 `LITEIM_BUILD_QT_CLIENT=ON` 才构建和测试 Qt 登录注册代码。

## 1. 为什么需要这个 Step

Step 46 已经有 `PacketCodec`、`TcpClient` 和 `ClientSession`，但用户还不能通过图形界面连接服务器。Step 47 的作用是把“用户输入”和“协议请求”接起来：

- `LoginWindow` 负责服务器地址、端口、用户名、密码和登录按钮。
- `RegisterDialog` 负责注册用户名、密码和可选昵称。
- `AuthController` 负责把 UI 输入转换成 TLV Packet，并解析服务端响应。
- 登录成功后才进入 `MainWindow`，避免未登录状态直接展示聊天主界面。

这样 Step 48 做三栏布局时，就可以假设客户端已经有登录态。

## 2. 本 Step 边界

### 本 Step 做

- 新增 `AuthController`，封装注册/登录请求、响应解析、错误处理和登录态写入。
- 新增 `LoginWindow`，包含服务器地址、端口、用户名、密码、登录按钮、注册按钮和状态提示。
- 新增 `RegisterDialog`，包含注册用户名、密码、昵称和提交/取消按钮。
- 登录成功后由 `main.cpp` 打开 `MainWindow`。
- 使用 `QSettings` 记住最近一次服务器地址、端口和用户名。
- 新增 Qt 单测覆盖输入禁用、注册弹窗输入校验、注册成功后登录、错误响应显示。

### 本 Step 不做

- 不实现三栏聊天主窗口。
- 不实现好友列表、会话列表、消息气泡、历史消息加载或未读数。
- 不实现心跳重连、断线提示或自动重试策略。
- 不修改服务端 AuthService、MySQL schema、Redis key 或协议数字值。
- 不加入 PersonaAgent 或任何 LLM 行为。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `client_qt/include/liteim_client/auth/AuthController.hpp` | 新增 | 登录/注册协议控制器 |
| `client_qt/src/auth/AuthController.cpp` | 新增 | 构造请求、解析响应、维护登录态 |
| `client_qt/include/liteim_client/app/ClientApp.hpp` | 新增 | 连接登录成功信号和主窗口启动逻辑 |
| `client_qt/src/app/ClientApp.cpp` | 新增 | 登录成功后创建 `MainWindow` 并关闭登录窗口 |
| `client_qt/include/liteim_client/ui/LoginWindow.hpp` | 新增 | 登录入口窗口 |
| `client_qt/src/ui/LoginWindow.cpp` | 新增 | 登录 UI、QSettings、状态提示和注册入口 |
| `client_qt/include/liteim_client/ui/RegisterDialog.hpp` | 新增 | 注册弹窗 |
| `client_qt/src/ui/RegisterDialog.cpp` | 新增 | 注册字段和输入校验 |
| `client_qt/src/main.cpp` | 修改 | 先显示登录窗口，登录成功后打开主窗口 |
| `client_qt/tests/qt_client_test.cpp` | 修改 | 新增 Step 47 Qt 测试 |
| `client_qt/tests/qt_client_test_main.cpp` | 修改 | 改用 `QApplication` 以支持 QWidget 测试 |
| `client_qt/src/CMakeLists.txt` | 修改 | 注册 Step 47 新 Qt 源文件 |
| `client_qt/tests/CMakeLists.txt` | 修改 | 注册 `LiteIMQtClient.Step47` 测试入口 |
| `README.md` / process 文件 | 更新 | 记录 Step 47 边界和验证结果 |

## 4. 核心接口与契约

### `AuthController`

```cpp
class AuthController final : public QObject {
public:
    void login(const QString& host, quint16 port,
               const QString& username, const QString& password);

    void registerUser(const QString& host, quint16 port,
                      const QString& username, const QString& password,
                      const QString& nickname);

signals:
    void registerSucceeded(AuthResult result);
    void loginSucceeded(AuthResult result);
    void authFailed(QString message);
    void busyChanged(bool busy);
};
```

契约：

- UI 调用 `login()` / `registerUser()`，不直接拼 TLV。
- 连接建立后通过 `TcpClient::sendPacket()` 发送请求。
- 收到 `RegisterResponse` 发出 `registerSucceeded`。
- 收到 `LoginResponse` 写入 `ClientSession` 登录态并发出 `loginSucceeded`。
- 收到 `ErrorResponse` 发出 `authFailed`，错误文本来自服务端 `TlvType::ErrorMessage`。

### `LoginWindow`

契约：

- `serverHostEdit`、`serverPortSpinBox`、`usernameEdit`、`passwordEdit` 收集登录输入。
- `loginButton` 只有在服务器、用户名、密码都非空时才启用。
- 点击登录会保存最近一次服务器地址、端口和用户名。
- 登录成功时发出 `loginSucceeded(AuthResult)`，由 `main.cpp` 打开 `MainWindow`。

### `RegisterDialog`

契约：

- `registerUsernameEdit` 和 `registerPasswordEdit` 必填。
- `registerNicknameEdit` 可选。
- 注册按钮只有用户名和密码非空时启用。
- 点击注册只关闭弹窗并把字段交给 `LoginWindow`，真正发协议由 `AuthController` 负责。

## 5. 运行流程

### 1. 启动客户端

```text
main()
    -> QApplication
    -> load QSS
    -> LoginWindow.show()
    -> QApplication::exec()
```

### 2. 登录请求

```text
LoginWindow::startLogin()
    -> save QSettings
    -> AuthController::login(host, port, username, password)
    -> TcpClient::connectToHost()
    -> connected()
    -> AuthController::sendPendingRequest()
    -> PacketCodec::appendStringField(Username / Password)
    -> TcpClient::sendPacket(LoginRequest)
```

### 3. 登录响应

```text
TcpClient::packetReceived(LoginResponse)
    -> AuthController::handlePacketReceived()
    -> parse UserId / Username / Nickname / SessionId
    -> ClientSession::markLoggedIn()
    -> emit loginSucceeded(result)
    -> main.cpp opens MainWindow
    -> LoginWindow closes
```

### 4. 注册响应

```text
RegisterDialog accepted
    -> LoginWindow calls AuthController::registerUser()
    -> RegisterRequest
    -> RegisterResponse
    -> LoginWindow fills username and asks user to log in
```

## 6. 关键实现点

### 1. UI 不直接访问 socket

`LoginWindow` 只调用 `AuthController`。真正的连接、发送、收包和协议错误都在 `AuthController + TcpClient + PacketCodec` 中处理。这样后续 UI 改版不会影响网络层。

### 2. AuthController 复用 Step 46 能力

Step 47 没有重新写 TCP 客户端，也没有重新写 TLV。它直接复用：

- `TcpClient`：连接和信号。
- `PacketCodec`：QString 和 TLV 的互转。
- `ClientSession`：seq_id、pending request 和登录态。

### 3. 错误来自服务端

错误密码、限流、用户不存在这类失败都由服务端返回 `ErrorResponse`。Qt 端解析 `TlvType::ErrorMessage` 并显示给用户，不在客户端重复实现认证规则。

### 4. 登录成功才打开主窗口

Step 45 的空窗口现在变成登录成功后的主窗口。Step 48 再在这个 `MainWindow` 上扩展三栏聊天布局。

## 7. 测试设计

| 风险 | 测试覆盖 |
| --- | --- |
| 空输入也能点登录 | `QtLoginWindowTest.LoginButtonRequiresServerUsernameAndPassword` |
| 注册弹窗空输入也能提交 | `QtRegisterDialogTest.RegisterButtonRequiresUsernameAndPassword` |
| Qt 注册请求 wire format 错误 | `QtAuthControllerTest.RegisterSuccessThenLoginSuccessUsesSameWireProtocol` 用 `QTcpServer` 读真实 Packet |
| 注册成功后不能继续登录 | 同一个 AuthController 测试先返回 `RegisterResponse`，再发送 `LoginRequest` |
| 登录响应没有写入登录态 | 测试检查 `controller.session().isLoggedIn()` |
| 登录成功后没有进入主窗口 | `QtClientAppTest.LoginSuccessOpensMainWindowAndClosesLoginWindow` |
| 错误密码不显示服务端错误 | `QtAuthControllerTest.ErrorResponseEmitsAuthFailed` |
| Step 46 协议/网络回归 | `LiteIMQtClient.Step46` 和 `LiteIMQtClient.Step47` 分开跑 |

## 8. 验证命令

Qt-enabled 构建和 Step 47 测试：

```bash
cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-qt --target liteim_qt_client_tests -j2
ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47" --output-on-failure
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

> Step 47 我实现了 Qt 客户端的登录注册入口，但没有让 UI 直接操作 socket。登录窗口只负责输入和展示状态，注册弹窗只负责收集注册字段，真正的协议请求都走 `AuthController`。`AuthController` 复用 Step 46 的 `TcpClient`、`PacketCodec` 和 `ClientSession`，发送普通 `RegisterRequest` / `LoginRequest`，解析服务端 `RegisterResponse` / `LoginResponse` / `ErrorResponse`。登录成功后才打开主窗口。

展开说：

> 这样设计的好处是 UI 和网络协议解耦。后续 Step 48 做三栏主界面时，不需要关心 TCP 半包粘包和 TLV 细节；登录失败时也直接展示服务端返回的错误消息，客户端不重复实现认证规则。

## 10. 面试常见追问

### 为什么要单独做 `AuthController`？

如果登录窗口直接操作 `QTcpSocket`，UI 会和协议细节耦合。`AuthController` 把注册/登录协议封装起来，窗口只关心成功、失败和 busy 状态。

### 为什么注册成功后没有自动登录？

当前实现注册成功后填回用户名并提示用户登录。这样流程简单明确，也避免注册成功但密码输入框状态、服务端连接状态和登录态混在一起。后续可以在 UI polish 阶段改成注册成功后自动登录。

### 错误密码为什么客户端不自己判断？

密码正确性必须由服务端判断。客户端只显示服务端 `ErrorResponse` 中的错误消息，避免客户端和服务端认证规则不一致。

### 为什么测试用 `QTcpServer` 而不是 mock？

Step 47 的核心风险是 Qt 端发出的 Packet 是否真能被服务端协议解析。用本地 `QTcpServer` 读真实 TCP 字节，可以同时覆盖 Qt socket、PacketCodec 和 AuthController 的协作。

### 为什么登录成功才打开 `MainWindow`？

主窗口后续会依赖当前用户、会话、好友和消息状态。先登录再进入主窗口，可以避免未登录状态下 UI 需要处理大量无效分支。
