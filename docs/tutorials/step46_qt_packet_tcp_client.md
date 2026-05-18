# Step 46：实现 Qt PacketCodec 和 TcpClient

## 0. 本 Step 结论

- 目标：Step 46 给 Qt 客户端补齐网络和协议基础，让后续登录注册窗口可以直接发送 LiteIM TLV Packet。
- 主要交付：新增 `PacketCodec`、`TcpClient`、`ClientSession` 和 `liteim_qt_client_core`。
- 协议边界：Qt 客户端复用服务端 `liteim_protocol` 的 `Packet`、`TlvCodec` 和 `FrameDecoder`，不重新定义 wire format。
- 网络边界：`TcpClient` 使用 `QTcpSocket`，提供 `connected`、`disconnected`、`packetReceived`、`errorOccurred` 信号。
- 状态边界：`ClientSession` 只管理客户端本地 seq_id、pending request 和登录态，不访问 MySQL / Redis，也不替代服务端 Session。

## 1. 为什么需要这个 Step

Step 45 只有空 Qt 窗口。真正的 IM 客户端不能让 UI 层直接拼二进制 TLV，也不能把 `QTcpSocket` 逻辑散落到登录窗口、主窗口和消息面板里。

本 Step 的价值是把 Qt 客户端拆出一层清晰的网络协议核心：

- UI 层只构造业务动作，例如登录、发消息、拉历史。
- `PacketCodec` 负责把业务字段转成 LiteIM TLV Packet。
- `TcpClient` 负责连接 server、发送 Packet、把 TCP 字节流还原成 Packet。
- `ClientSession` 负责客户端本地请求序号、等待中的请求和登录状态。

这样 Step 47 做登录注册时，不需要一边写 UI 一边处理 TCP 半包/粘包。

## 2. 本 Step 边界

### 本 Step 做

- 新增 `liteim_qt_client_core` 静态库，承载可测试的 Qt 客户端核心。
- 新增 `PacketCodec`，提供 Qt 友好的 `QByteArray` / `QString` 包装，同时复用 `liteim_protocol`。
- 新增 `TcpClient`，基于 `QTcpSocket` 支持连接、断开、发送 Packet、收包和错误信号。
- 新增 `ClientSession`，管理 seq_id、pending request 和登录态。
- 新增 `liteim_qt_client_tests`，覆盖 wire format、半包/粘包、连接错误、发送和收包信号。
- Qt-enabled CTest 增加 `LiteIMQtClient.Step46`。

### 本 Step 不做

- 不实现登录窗口、注册弹窗或表单校验。
- 不实现三栏主窗口、会话列表、消息气泡或未读数展示。
- 不实现心跳定时器、自动重连或断线状态 UI。
- 不修改服务端协议数字值、MySQL schema、Redis key 或业务服务。
- 不把 Qt 作为默认构建依赖。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `client_qt/CMakeLists.txt` | 修改 | 增加 `liteim_qt_client_core`、Qt Network/Test、AUTOMOC 和 Step 46 CTest |
| `client_qt/include/liteim_client/PacketCodec.hpp` | 新增 | Qt 客户端协议编解码接口 |
| `client_qt/src/PacketCodec.cpp` | 新增 | 复用 `encodePacket()`、`TlvCodec` 和 `FrameDecoder` |
| `client_qt/include/liteim_client/TcpClient.hpp` | 新增 | `QTcpSocket` 客户端接口和信号 |
| `client_qt/src/TcpClient.cpp` | 新增 | 连接、断开、发送、readyRead 解码和 socket 错误处理 |
| `client_qt/include/liteim_client/ClientSession.hpp` | 新增 | 客户端 seq_id、pending request、登录态 |
| `client_qt/src/ClientSession.cpp` | 新增 | 本地会话状态实现 |
| `client_qt/tests/qt_client_test.cpp` | 新增 | Step 46 行为测试 |
| `client_qt/tests/qt_client_test_main.cpp` | 新增 | 创建 `QCoreApplication` 后运行 GoogleTest |
| `README.md` / process 文件 | 更新 | 记录 Qt 协议网络层和验证方式 |

## 4. 核心接口与契约

### `PacketCodec`

```cpp
class PacketCodec {
public:
    static Status encode(const Packet& packet, QByteArray& output);
    static Status appendStringField(TlvType type, const QString& value, Packet& packet);
    static Status appendUint64Field(TlvType type, std::uint64_t value, Packet& packet);
    static Status parseFields(const Packet& packet, TlvMap& output);

    Status feed(const QByteArray& bytes, std::vector<Packet>& output);
    void reset();
};
```

契约：

- `encode()` 输出服务端可直接解析的完整 Packet 字节流。
- `appendStringField()` 把 `QString` 按 UTF-8 写入 TLV。
- `feed()` 可以多次喂入 TCP 字节流，支持半包和粘包。
- 解码失败时返回 `Status`，上层 `TcpClient` 负责发出错误信号。

### `TcpClient`

```cpp
class TcpClient final : public QObject {
    Q_OBJECT

public:
    void connectToHost(const QString& host, quint16 port);
    void disconnectFromHost();
    Status sendPacket(const Packet& packet);
    bool isConnected() const noexcept;

signals:
    void connected();
    void disconnected();
    void packetReceived(liteim::Packet packet);
    void errorOccurred(QString message);
};
```

契约：

- 只使用 `QTcpSocket`，不在 Qt 客户端里重写 epoll。
- `sendPacket()` 接收已经构造好的 `Packet`，UI 不直接写二进制。
- `readyRead` 中读取所有可用字节，交给 `PacketCodec` 还原 Packet。
- 收到完整 Packet 后发出 `packetReceived`。
- 连接失败、socket 错误或协议解码失败发出 `errorOccurred`。

### `ClientSession`

```cpp
class ClientSession {
public:
    std::uint64_t trackRequest(MessageType request_type);
    std::optional<PendingRequest> takePending(std::uint64_t seq_id);
    void markLoggedIn(std::uint64_t user_id, QString token, QString session_id);
    void reset();
};
```

契约：

- seq_id 从客户端本地递增生成。
- pending request 用 `seq_id -> request_type` 记录，响应回来后 `takePending()` 移除。
- 登录态只保存客户端当前 user_id、token 和 session_id。
- `reset()` 用于断线或重新连接时清空本地状态。

## 5. 运行流程

### 1. UI 发起请求

```text
LoginWindow
    -> ClientSession::trackRequest(LoginRequest)
    -> PacketCodec::appendStringField(Username)
    -> PacketCodec::appendStringField(Password)
    -> TcpClient::sendPacket(packet)
```

本 Step 还没有 `LoginWindow`，但 Step 47 会按这个流程接入。

### 2. 发送 Packet

```text
TcpClient::sendPacket()
    -> PacketCodec::encode()
    -> liteim::encodePacket()
    -> QTcpSocket::write()
    -> QTcpSocket::flush()
```

`flush()` 的作用是让短生命周期测试和即时发送场景更稳定，避免数据只留在 Qt 写缓冲里等待后续事件循环。

### 3. 接收 Packet

```text
QTcpSocket::readyRead
    -> readAll()
    -> PacketCodec::feed()
    -> liteim::FrameDecoder
    -> packetReceived(packet)
```

如果服务端一次发半个 Packet，`FrameDecoder` 会暂存；如果一次发多个 Packet，`FrameDecoder` 会按顺序输出多个 Packet。

## 6. 关键实现点

### 1. 不复制服务端协议实现

Qt 客户端和 C++ 服务端必须使用同一套数字协议。这里直接链接 `liteim_protocol`，让 `PacketCodec` 做 Qt 类型适配，而不是复制一份 Packet/TLV 编解码代码。

### 2. `liteim_qt_client_core` 提升可测试性

如果所有代码都塞进 `liteim_qt_client` 可执行文件，测试很难链接。现在拆出 `liteim_qt_client_core`：

- 可执行文件只负责 `main.cpp` 和资源。
- core library 承载窗口、协议、网络和会话状态。
- 测试 target 直接链接 core library。

### 3. Qt 信号使用值传递 Packet

`packetReceived(liteim::Packet packet)` 使用值传递，并注册 `Q_DECLARE_METATYPE(liteim::Packet)`。这样 `QSignalSpy` 和后续跨对象信号连接都能识别 Packet 类型。

### 4. Anaconda Qt 运行时库顺序

当前本机 Qt 来自 Anaconda。测试二进制如果优先加载 Anaconda 的旧 `libstdc++.so.6`，会缺少系统编译器需要的 `GLIBCXX_3.4.32`。因此 `LiteIMQtClient.Step46` 的 CTest 环境把 `/usr/lib/x86_64-linux-gnu` 放在 Anaconda Qt lib 目录前面。

## 7. 测试设计

| 风险 | 测试覆盖 |
| --- | --- |
| Qt 编码结果和服务端协议不一致 | `QtPacketCodecTest.EncodedPacketUsesServerWireFormat` 用服务端 `parseHeader()` / `parseTlvMap()` 解析 Qt 输出 |
| 服务端 Packet Qt 端不能解码 | `QtPacketCodecTest.DecodesServerPacketsAcrossHalfAndStickyFrames` 使用服务端 `encodePacket()` 生成输入 |
| TCP 半包/粘包处理错误 | 同一个 PacketCodec 测试先喂 5 字节，再喂剩余半包加第二个完整包 |
| pending request 泄漏或登录态不清 | `QtClientSessionTest.TracksPendingRequestsAndLoginState` 覆盖 track/take/reset |
| 连接失败不通知 UI | `QtTcpClientTest.EmitsErrorForConnectionFailure` 监听 `errorOccurred` |
| 发送 Packet 不符合服务端格式 | `QtTcpClientTest.SendsPacketToLoopbackServer` 用 Qt loopback server 收包后交给服务端 `FrameDecoder` |
| 服务端推送不能到达 UI | `QtTcpClientTest.EmitsPacketReceivedForServerPacket` 监听 `packetReceived` |

## 8. 验证命令

Qt-enabled 构建和 Step 46 测试：

```bash
cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-qt --target liteim_qt_client_tests -j2
ctest --test-dir build-qt -R LiteIMQtClient.Step46 --output-on-failure
cmake --build build-qt --target liteim_qt_client -j2
```

默认构建边界验证：

```bash
cmake -S . -B build
cmake --build build --target liteim_tests -j2
ctest --test-dir build -L unit --output-on-failure
```

Qt 空窗口启动验证：

```bash
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib \
QT_QPA_PLATFORM=offscreen \
timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124
```

## 9. 面试表达

> Step 46 我没有直接做登录 UI，而是先把 Qt 客户端的协议和网络层抽出来。`PacketCodec` 复用服务端的 `liteim_protocol`，保证 Qt 端和服务端共享同一套 Packet/TLV wire format；`TcpClient` 用 `QTcpSocket` 管理连接、发送、收包和错误信号；`ClientSession` 管理客户端本地 seq_id、pending request 和登录态。这样 Step 47 的登录注册窗口只需要调用这些接口，不需要直接拼 TLV 或处理 TCP 半包粘包。

展开说：

> 我还把这些代码放进 `liteim_qt_client_core` 静态库，而不是只放在可执行文件里。这样 Qt 客户端核心可以被 GoogleTest 测试，覆盖服务端 wire format 兼容、半包粘包、连接失败、发送 Packet 和服务端推送转信号。默认服务端构建仍然不依赖 Qt，只有打开 `LITEIM_BUILD_QT_CLIENT=ON` 才构建 Qt target。

## 10. 面试常见追问

### 为什么 Qt 客户端还要用服务端的 `liteim_protocol`？

因为 Packet header、MessageType、TlvType 和 TLV 编解码是跨端协议契约。复用同一个库能避免 Qt 端和服务端出现数字值或大小端不一致的问题。

### 为什么不用服务端的 `Session`？

服务端 `Session` 是 epoll Reactor 里的连接对象，生命周期属于 I/O loop。Qt 客户端应该用 `QTcpSocket` 和信号槽，不应该把服务端 Reactor 对象搬到客户端。

### 为什么要有 `ClientSession`？

客户端需要知道哪个请求还在等待响应、响应 seq_id 对应什么请求、当前是否已经登录。这个状态只属于客户端本地，不应该散落在每个窗口类里。

### 为什么测试里还要检查半包和粘包？

TCP 是字节流，不保证一次 `readyRead` 就是一个完整 Packet。客户端如果不处理半包/粘包，真实网络下登录响应、消息推送和历史返回都可能解析错。

### 为什么现在不做心跳和自动重连？

心跳、断线提示和自动重连属于 Step 52 的客户端 polish。Step 46 只建立连接、编解码和本地请求状态，避免把网络基础和用户体验策略混在一起。
