# LiteIM Findings

## 权威来源

- `/home/yolo/jianli/PROJECT_MEMORY.md` 是 LiteIM 和 PersonaAgent 的唯一总设计和长期路线来源。
- `/home/yolo/jianli/AGENTS.md` 和 `/home/yolo/jianli/CLAUDE.md` 是 agent 工作约束文件，不记录完成状态、实际提交 hash 或活动下一步。
- `LiteIM/README.md` 是对外说明文档，不记录过程进度、测试数量、提交历史或默认下一步。
- `LiteIM/docs/process/task_plan.md`、`LiteIM/docs/process/findings.md` 和 `LiteIM/docs/process/progress.md` 记录进度、发现、验证结果和过程记忆。
- 如果文档或源码与 `PROJECT_MEMORY.md` 的总路线冲突，按总路线修正；如果冲突点是完成状态或活动任务，按 planning files 的过程记录修正。

## 2026-05-19 Step 53 Final README Showcase Findings

当前采用的边界：

- Step 53 是文档和展示材料收口，不修改 C++ 服务端代码、TLV 协议、MySQL schema、Redis key、Qt 功能逻辑或 PersonaAgent 实现。
- README 作为公开展示入口，补齐技术栈、服务端架构图、线程模型图、TLV 协议摘要、MySQL 表结构摘要、Redis Key 摘要、Qt 截图、编译/运行/测试方式、压测结果、PersonaAgent 接入边界和面试说明。
- `docs/reports/qt_client_showcase.png` 由当前 Qt `MainWindow` / `ChatPage` / `MessageBubble` 代码路径渲染生成，不使用第三方 IM 产品品牌、logo、截图或素材。
- 第一轮截图生成暴露出临时截图工具误链旧位置 `build-qt/client_qt/libliteim_qt_client_core.a`，画面仍有旧 Agent sidebar；改为链接当前 `build-qt/client_qt/src/libliteim_qt_client_core.a` 后重新生成，截图显示 Step49-52 当前三栏 UI、会话列表、气泡、Offline/Reconnect 状态。
- `docs/reports/liteim_benchmark_report_2026-05-18.md` 保留为历史本机 benchmark 数据源；Step 53 不重新跑压测，不把 2026-05-18 的本机数据包装成生产容量声明。
- README 中原本较长的 Step22-Step52 流水账被压缩为当前运行时依赖和职责摘要；详细过程继续留在 tutorials/process。
- PersonaAgent 表述继续保持 future/planned/ordinary account 边界：C++ server 不识别 AI 身份，不嵌入 LLM/RAG/Safety。

验证关注：

- README 不再保留旧 `Current Status` / 教程提交章节等展示层噪声。
- Step53 教程必须保持固定 0-10 结构，最后主章节是 `面试常见追问`。
- Qt 截图文件必须存在且是 PNG。

## 2026-05-19 Step 52 Qt Heartbeat Reconnect Polish Findings

当前采用的边界：

- Step 52 不修改服务端协议、MySQL schema、Redis key、服务端登录态模型或 PersonaAgent 行为，只补 Qt 客户端稳定性和演示体验。
- `ClientRuntime` 是 Qt 端连接状态入口：保存 server endpoint，管理 `TcpClient`、`ClientSession`、心跳 `QTimer`、连接状态和一次自动重连。
- 登录成功后启动 30 秒默认心跳；测试可用更短 interval 验证 `HeartbeatRequest` wire packet。
- `HeartbeatResponse` 由 `ClientRuntime` 消费；`AuthController` 只消费 register/login pending response，`ChatController` 只消费聊天/好友/群组/history pending response。
- 这个 pending-response 归属修复避免真实 app 中 `AuthController` 在登录窗口关闭后仍监听 packet，并误取走聊天响应。
- 主窗口状态栏显示连接状态和 `Reconnect` 按钮；断线时停止心跳，状态变为 `Offline`，手动重连可重新建立 TCP 连接。
- 自动重连第一版只尝试一次，避免无限 reconnect loop。
- 登录窗口继续使用 `QSettings` 保存 host、port、recent username；不保存密码。
- 当前重连是 TCP 连接层恢复，不自动重新登录；完整 re-auth 需要后续协议设计。
- 发送失败时把最新 outgoing `Sending` 气泡改成 `Failed`，不做可靠投递、ACK、重试队列或本地缓存。
- 截图交付采用 README 中的运行截图说明，不额外引入截图脚本依赖。

TDD 记录：

- RED 在 `qt_client_test.cpp` 增加 Step52 测试，首次构建失败于 `ClientRuntime` 缺少 `setConnectionEndpoint()`、`startHeartbeat()`、`connectionStatusChanged`、`reconnect()` 等新接口。
- GREEN 初版后测试失败于 Qt 异步等待方式：心跳依赖 Qt event loop，断线测试也把 `Connecting...` / `Online` 误算成 `Offline`；修正测试等待条件后 `LiteIMQtClient.Step52` 通过。
- 新增回归测试 `AuthControllerLeavesChatResponsesForChatController`，覆盖登录层存在时聊天 response 仍由 `ChatController` 消费。

## 2026-05-19 Step 51 Qt Private Group Agent Contact Flow Findings

当前采用的边界：

- Step 51 不修改服务端协议、MySQL schema、Redis key 或 C++ 服务端业务逻辑，只把 Qt UI 信号接到现有 TLV request/response/push。
- 新增 `ClientRuntime`，把 Qt 端 `TcpClient` 和 `ClientSession` 组合成登录和主窗口共享的运行时状态。
- `AuthController` 改为持有 `ClientRuntime`；`LoginWindow` 暴露 runtime；`ClientApp` 在登录成功后创建 `MainWindow(login_window.runtime())`，避免主窗口重新建 socket 后丢失登录态。
- 新增 `ChatController`，负责构造 add-friend/create-group/join-group/private/group/history 请求，并把 private/group push 或 history response 转成 `ChatMessage`。
- `ConversationItem` / `ContactListItem` 增加目标 id 和 conversation id 元数据；UI 使用 `private:<user_id>` / `group:<group_id>` 字符串，协议发送时转换为服务端需要的整数字段。
- 点击 Bob 发送普通私聊历史请求和后续 `PrivateMessageRequest`；点击群组发送群历史请求和后续 `GroupMessageRequest`。
- PersonaAgent 仍是普通联系人：`target_id = 3001`、`conversation_id = private:3001`，不加入特殊 sidebar、不加入 C++ AI 身份、不做群聊 @ 触发。
- 当前好友/群组/PersonaAgent 列表仍是 Qt demo seed data；Step 51 覆盖协议发送链路和 push 展示，不实现完整远程列表刷新。

TDD 记录：

- RED 在 `qt_client_test.cpp` 中先引入 `liteim_client/app/ClientRuntime.hpp` 和 `liteim_client/chat/ChatController.hpp`，首次构建失败于缺少 `ClientRuntime.hpp`。
- GREEN 初版后 Step51 运行失败：`ContactListWidget` 同时响应 `currentRowChanged` 和 `itemClicked`，一次测试选择导致两次 `HistoryRequest`；修复为只在真实 `itemClicked` 时激活联系人/群组。
- Qt Step46-51 回归和 Qt offscreen startup 已通过；完整验证命令记录在 `progress.md`。

## 2026-05-19 Step 50 Qt Chat Page Bubble History Findings

当前采用的边界：

- Step 50 只实现 Qt 右侧聊天页 UI 行为和可测试信号边界，不修改服务端协议、MySQL schema、Redis key 或 PersonaAgent runtime。
- 新增 `ChatInputBar`，负责空消息禁用、发送按钮、Enter 发送和 Shift+Enter 换行。
- 新增 `MessageBubble`，负责左右气泡、文本自动换行、时间显示、outgoing 发送状态，以及群聊 incoming 消息的发送者昵称。
- `ChatPage` 从占位区升级为滚动消息区，支持 `openConversation()`、`setMessages()`、`appendMessage()` 和 `updateMessageStatus()`。
- 打开会话时发出 `historyRequested(conversation_id, 0)`，加载更早消息时用当前最早非零 `message_id` 作为 `before_message_id` 游标。
- 输入栏发送后先追加本地 outgoing `Sending` 气泡，再发出 `sendMessageRequested(conversation_id, text)`；真实私聊/群聊 packet 编码和历史响应解析留给 Step 51。
- 视觉交互参考常见微信式三栏 IM 的左右气泡体验，但不使用微信品牌、logo、名称、图标、截图或素材。

TDD 记录：

- RED 在 `qt_client_test.cpp` 中先引入 `liteim_client/ui/ChatInputBar.hpp` / `MessageBubble.hpp` 并新增 Step50 断言，首次构建失败于缺少 `ChatInputBar.hpp`。
- GREEN 增加 `ChatInputBar`、`MessageBubble`、聊天页滚动消息区、Step50 CTest、QSS 气泡/输入栏样式和文档。

## 2026-05-19 Step 49 Qt Conversation Contact Unread Findings

当前采用的边界：

- Step 49 只实现 Qt 客户端侧会话列表、联系人列表、群组列表和本地未读显示行为。
- 新增 `ConversationModel` 作为会话摘要数据源，`ConversationListWidget` 的 Messages 页面使用 `QListView` 绑定该 model，并用内部 delegate 绘制头像、摘要、时间和红色未读 badge，不再把消息列表文本散落在 widget 里。
- 新增 `ContactListWidget` 复用联系人式列表渲染好友和群组；好友 subtitle 表达 Online / Offline，群组 subtitle 表达成员数量。
- 当前联系人、群组和 PersonaAgent 条目都是 Qt 本地 demo seed data，不从 MySQL / Redis 加载。
- 当前未读数是本地临时计数：非当前会话 incoming message 加一，当前会话 incoming message 不加，`markConversationRead()` 清零。
- PersonaAgent 继续作为普通联系人/会话对象出现，不作为 `SideBar` 顶级分类，不引入 C++ 服务端 AI 身份。

TDD 记录：

- RED 在 `qt_client_test.cpp` 中先引入 `liteim_client/model/ConversationModel.hpp` 和 Step49 断言，首次构建失败于缺少该头文件。
- GREEN 增加 `ConversationModel`、`ContactListWidget`、会话 item delegate、中间栏 `QStackedWidget`、Step49 CTest、QSS 列表样式和文档。
- Qt Step46/47/48/49 plus Qt foundation、Qt offscreen startup、默认 unit 和 Docker-backed 全量 CTest 均已通过；完整命令记录在 `progress.md`。

## 2026-05-19 Qt Client Local Structure Refactor Findings

当前采用的边界：

- 本次是 `client_qt` 局部结构重构，不是新的功能 Step；不修改服务端协议、MySQL schema、Redis key、真实 Qt 数据模型、未读数、消息加载、push 更新或 PersonaAgent 行为。
- Qt 客户端当时的物理目录按职责分成 `app`、`auth`、`network`、`protocol`、`ui`：应用装配、认证流程、QTcpSocket/客户端会话、协议适配和 QWidget 组件各自归位；Step 49 后新增 `model` 目录承载 Qt 客户端数据模型。
- CMake 采用分级结构：`client_qt/CMakeLists.txt` 只负责 Qt 查找、AUTOMOC/AUTORCC 和公共 warning helper；`client_qt/src/CMakeLists.txt` 负责 `liteim_qt_client_core` / `liteim_qt_client`；`client_qt/tests/CMakeLists.txt` 负责 `liteim_qt_client_tests` 和 Step46/47/48 CTest。
- 保留一个 `liteim_qt_client_core` target，不拆成多个 Qt 子库；当前体量下目录分层已经足够，拆更多 target 会增加链接和 AUTOMOC 复杂度。
- 保留原有 target 名、CTest 名、Qt offscreen 测试环境和 Anaconda Qt `LD_LIBRARY_PATH` 规避逻辑；显式设置 Qt executable/test 的 `RUNTIME_OUTPUT_DIRECTORY`，让原来的 `build-qt/client_qt/liteim_qt_client` 运行命令继续可用。
- `tests/cmake/qt_client_foundation_test.sh` 已改为检查新的 `ui/MainWindow.hpp`、`src/ui/MainWindow.cpp`、`client_qt/src/CMakeLists.txt` 和 `client_qt/tests/CMakeLists.txt`。

## 2026-05-19 Step 48 Sidebar Agent Entry Cleanup Findings

当前采用的边界：

- 用户选择方案 A：未来 PersonaAgent 是普通账号对象，只出现在联系人列表或会话列表中，不作为 `SideBar` 顶级分类。
- Step 48 的顶级导航收敛为 messages / contacts / groups / settings；`agent` section、`navAgentButton` 和 `Agent` 占位列表全部移除。
- Qt 客户端视觉可以参考常见微信式三栏 IM 交互：左侧窄导航、中间列表、右侧聊天区；但项目仍不得使用微信品牌、logo、名称、图标、截图或素材。
- 本次不实现真实联系人模型、会话模型、未读数、消息加载、push 刷新、PersonaAgent BotClient 或服务端特殊 AI 身份。

TDD 记录：

- RED 修改 `QtMainWindowTest.StartsWithThreeColumnChatLayout`，要求不存在 `navAgentButton`，首次运行 `LiteIMQtClient.Step48` 失败于旧 `SideBar` 仍创建该按钮。
- GREEN 删除 `SideBar` 的 Agent 按钮、`ConversationListWidget` 的 `agent` 分支、`MainWindow` 的 Agent 标题映射和 QSS 中的 `navAgentButton` selector。

## 2026-05-19 Step 48 Qt Three-Column Main Window Findings

当前采用的边界：

- Step 48 只实现登录后的 Qt 主窗口布局，不实现 Step 49 的真实 `ConversationModel`、联系人列表、群列表、未读数、消息加载或 push 更新。
- 三栏布局使用 `QSplitter`：`SideBar` 固定窄宽度，`ConversationListWidget` 限制中等宽度，`ChatPage` 使用 stretch 占据剩余空间。
- `SideBar` 提供 messages / contacts / groups / settings 四个入口；后续 PersonaAgent 不进入顶级导航，而是作为普通联系人或普通会话出现。
- `ConversationListWidget` 目前只展示占位列表，目的是给 Step 49 的 model-driven 列表留出稳定挂载点。
- `ChatPage` 显示当前用户昵称和在线状态，同时保留聊天区和输入框占位；输入框禁用，避免提前实现发消息。
- 样式统一写在 `client_qt/resources/qss/app.qss`，C++ 代码只负责结构和信号连接。

TDD 记录：

- RED 测试覆盖主窗口三栏对象、左侧五个按钮、顶部用户状态、左侧切换驱动中间区域变化、resize 后列宽仍可用。
- RED 首次运行 `LiteIMQtClient.Step48` 失败于现有空 `MainWindow` 缺少 `mainSplitter` / `sideBar` / `conversationListWidget` / `chatPage`。
- GREEN 后 `LiteIMQtClient.Step48` 通过，随后 `LiteIMQtClient.Step46|Step47|Step48` 组合回归通过。

## 2026-05-18 Step 47 Qt Login and Register Window Findings

当前采用的边界：

- Step 47 只实现 Qt 登录/注册入口，不实现三栏主界面、好友/会话列表、消息气泡、历史加载、心跳重连或 PersonaAgent。
- UI 层不直接操作 `QTcpSocket`；`LoginWindow` 和 `RegisterDialog` 只收集输入、显示状态、响应 `AuthController` 信号。
- `AuthController` 复用 Step 46 的 `TcpClient`、`PacketCodec` 和 `ClientSession`，发送普通 `RegisterRequest` / `LoginRequest`，解析 `RegisterResponse` / `LoginResponse` / `ErrorResponse`。
- 登录成功后写入 Qt 客户端本地 `ClientSession` 登录态；服务端当前 `LoginResponse` 返回 `SessionId`，没有 token 字段，因此 Qt 客户端把 token 留空，把 `SessionId` 作为字符串保存在本地 session。
- `ClientApp` 是很薄的启动桥接层，只负责把 `LoginWindow::loginSucceeded` 连接到创建 `MainWindow` 并关闭登录窗口；该抽取是为了让“登录成功进入主窗口”能被单测覆盖。
- `QSettings` 只记住最近一次服务器地址、端口和用户名，不保存密码。

TDD 记录：

- RED 测试覆盖登录窗口空输入禁用、注册弹窗空输入禁用、注册成功后继续登录、错误响应显示服务端错误、登录成功打开主窗口。
- 首次 Qt 测试构建按预期失败于缺少 `liteim_client/auth/AuthController.hpp`。
- GREEN 后 `LiteIMQtClient.Step46` 和 `LiteIMQtClient.Step47` 均通过；默认 `build` 仍不进入 Qt 子目录。

## 2026-05-18 Step 46 Qt PacketCodec and TcpClient Findings

当前采用的边界：

- Step 46 只实现 Qt 客户端协议/网络基础，不实现登录注册 UI、三栏聊天界面、消息气泡、心跳重连或 PersonaAgent。
- Qt 客户端不复用服务端 `Session`；服务端 `Session` 属于 epoll Reactor，Qt 端使用 `QTcpSocket` 和信号槽。
- `PacketCodec` 不重新实现 wire format，而是复用 `liteim_protocol` 的 `Packet`、`TlvCodec`、`FrameDecoder`，只做 `QByteArray` / `QString` 适配。
- `TcpClient` 是 QObject，公开 `connected`、`disconnected`、`packetReceived`、`errorOccurred` 信号；`packetReceived` 使用值传递的 `liteim::Packet` 并注册 Qt metatype，便于 `QSignalSpy` 和后续 UI 连接。
- `ClientSession` 是 Qt 客户端本地状态，不访问 MySQL / Redis，不和服务端在线状态混用；它只管理 seq_id、pending request、user_id、token 和 session_id。
- 当前本机 Qt 来自 Anaconda Qt5。Qt test 二进制会通过 Anaconda Qt 引入旧 `libstdc++.so.6`，因此 `LiteIMQtClient.Step46` 的 CTest 环境把 `/usr/lib/x86_64-linux-gnu` 放在 Anaconda Qt lib 前，避免缺少 `GLIBCXX_3.4.32` 的假失败。

TDD 记录：

- RED 测试覆盖 Qt 编码后服务端可解析、服务端编码后 Qt 可解码、半包/粘包、pending/login 状态、连接失败 error 信号、发送 Packet 和 packetReceived 信号。
- 重新配置 Qt build 后，RED 按预期失败于缺少 `liteim_client/network/ClientSession.hpp`。
- GREEN 后 `ctest --test-dir build-qt -R LiteIMQtClient.Step46 --output-on-failure` 通过。

## 2026-05-18 Step 45 Qt Client Foundation Findings

当前采用的边界：

- 以 `/home/yolo/jianli/PROJECT_MEMORY.md` 为准，当前 Step 45 是 `Qt 客户端基础工程和资源规范`；旧 memory 中 Step 45=测试硬化的记录已经被后续路线重排覆盖。
- 本 Step 只创建可选 Qt Widgets 工程骨架、空窗口、QSS 和图标资源规范；不实现 `PacketCodec`、`QTcpSocket`、登录注册、三栏主窗口、消息气泡、心跳或 PersonaAgent 功能。
- 默认 CMake 构建必须不受 Qt 影响；只有 `-DLITEIM_BUILD_QT_CLIENT=ON` 才进入 `client_qt`。
- Qt 资源不得使用 WeChat / Weixin / 微信品牌、图标、名称或素材。
- 当前本机 `cmake --find-package Qt6/Qt5` 的快速探测没有找到系统 Qt，但实际 `-DLITEIM_BUILD_QT_CLIENT=ON` 配置能通过 Anaconda Qt5 CMake package，`Qt5Widgets_DIR=/home/yolo/anaconda3/lib/cmake/Qt5Widgets`；因此本 Step 可完成 Qt-enabled compile 验证，不需要安装系统包。

TDD 记录：

- 已新增 `LiteIMCMake.QtClientFoundation` CTest 元数据测试，首次重新配置后运行失败于 `missing LITEIM_BUILD_QT_CLIENT option`，这是 Step 45 RED。
- 实现后 `LiteIMCMake.QtClientFoundation` 通过；`cmake --build /tmp/liteim-qt-check --target liteim_qt_client -j2` 通过；`QT_QPA_PLATFORM=offscreen timeout 2s /tmp/liteim-qt-check/client_qt/liteim_qt_client || test $? -eq 124` 通过。

## 2026-05-17 Markdown Drift Sync Findings

本次用户要求同步所有 Markdown，避免旧路线漂移。扫描范围是 `/home/yolo/jianli` 下 54 个 Markdown 文件，排除 LiteIM build 输出。

采用结论：

- `PROJECT_MEMORY.md`、`AGENTS.md`、`CLAUDE.md`、`LiteIM/README.md`、`docs/tutorials/`、`docs/process/` 都必须统一表达：PersonaAgent 使用普通 LiteIM 用户账号接入，C++ 服务端不识别 AI/assistant 身份，不定义回复行文。
- 历史 process 记录可以保留 Step 时间线，但不能继续展开已经移除的 C++ 内置 assistant 方案细节；这些细节会让后续恢复上下文时误判当前路线。
- `BotClient` 是 Python 客户端组件名，允许保留；其他旧 assistant 叙述、旧固定账号、旧专用协议名、旧 C++ gateway/service 名都不再保留。
- 旧 assistant 教程已经删除，不再用新教程替代。
- 用户后续要求 Step 40 之后直接重排，因此当前路线改为：Step 41 CLI、Step 42 Python E2E、Step 43 benchmark、Step 44 gMock/ASan/UBSan、Step 45-52 Qt、Step 53 final docs。
- 当前 LiteIM 没有 Python BotClient 功能；`tests/e2e/liteim_e2e.py` 只是 Step 42 的黑盒测试 helper。PersonaAgent BotClient 属于后续项目二。

## 2026-05-17 C++ Assistant Route Retirement Findings

用户确认原来的 C++ 内置 assistant 路线不再需要，LiteIM 只保留普通账号消息系统。

当前采用的边界：

- LiteIM 不定义 AI/assistant 身份识别，不保留 C++ 内置 assistant gateway、assistant service、echo fallback 或 assistant options。
- LiteIM 不启用也不保留 assistant 专用 `MessageType` 或 `TlvType` 常量。
- `ChatService` 和 `GroupService` 按普通账号处理所有用户：在线则 push，离线则写 offline/unread，不因为账号未来可能由 LLM 控制而分支。
- MySQL seed 只保留普通开发用户和开发群，不再固定创建 assistant 账号或 assistant 群成员。
- PersonaAgent 后续通过 Python BotClient 使用普通账号注册/登录/收发私聊消息；LLM、RAG、风格、安全和回复策略全部属于项目二。

暂缓项：

- 后续已按用户要求重排 Step 40 之后的编号；旧 assistant 教程文件直接删除，不占用 Step。
- 不在 LiteIM C++ 服务端加入 Agent 配置、模型调用、群聊 @ 策略或 AI 回复模板。

## 2026-05-16 Post-Step44 Review Hardening Findings

本次根据外部评审和本地复核执行的是 Step 44 后的收口修复，不新开 Step 45，也不改变 Qt / PersonaAgent 路线。

当前采用的边界：

- CI 仍属于 Step 44 验证能力和仓库基础设施，不单独拆成 Step 45。
- 不修改 MySQL schema、Redis key、Qt 或 PersonaAgent；本条记录中的旧 C++ assistant 路线已在 2026-05-17 被移除。
- 对运行时正确性问题直接修复：离线消息、好友在线状态、输入边界、配置加载和测试稳定性问题按局部方案收敛。
- 对可靠性问题做局部收敛：离线消息 limit 下推到 SQL，好友在线状态和离线 unread 清理按 Redis best-effort 降级。
- 对输入边界做服务层前置校验：username/nickname 64 bytes，password 128 bytes，group name 128 bytes，message text 8192 bytes。
- 对可维护性做低风险重构：提取 `MessagePacketBuilder`，消除 Chat/Group/History/Offline 等消息 TLV 拼接重复。
- 对 CI 假绿/假红做修复：Python E2E 增加 strict 模式，ASan 下跳过 fd-exhaustion 这种受 sanitizer runtime fd 占用影响的单测。

暂缓项：

- 不抽 `containsMember()`，因为当前重复体量小，抽出新模块收益不如 `appendMessageFields()` 明确。
- 不改 history wire format 的 repeated TLV 结构，只在 README 写明 newest-first 和 UI 反转要求。
- 不做 ServerApp/RuntimeContext 大重构；本次只给 `liteim_server` 增加 `--config` 和默认 `config/liteim.conf` fallback。
- 不清理 backup 分支。删除分支属于 `.git` 操作，需用户单独确认。

## 2026-05-16 Documentation Layout Cleanup Findings

用户确认采用方案 A：删除原 GitHub Actions CI Step，把 Qt 客户端阶段前移，并把 LiteIM 的过程文件和教程统一收到 `docs/` 内。当前后续重排后的路线是 Step 45-52 Qt、Step 53 final docs。

当前采用的边界：

- 删除当时的 `.github/workflows/ci.yml` 和 GitHub Actions CI 教程，不保留 CI 作为 LiteIM 近期 Step。
- `docs/process/` 承载 `task_plan.md`、`findings.md`、`progress.md`。
- `docs/tutorials/` 承载 Step 教程；不创建教程 README。
- 保留 `docs/debug_cases/`，因为这些是仍有价值的内部复盘材料。
- 清理本地生成目录 `build-asan/`、`build-asan-plan/` 和 Python `__pycache__`，并补充 `.gitignore`。
- 不修改服务端协议、MySQL schema、Redis key、C++ 源码行为、Qt 或 PersonaAgent。
- 不回滚进入本任务前已有的用户侧源码注释/格式改动。

## 2026-05-16 Repository CI Infrastructure Findings

GitHub CI 对 LiteIM 有价值，但不需要拆成单独 Step。它的职责是仓库级基础设施：push / PR 后在干净 Ubuntu runner 上重新 configure、build、CTest，并给 README badge 提供可见状态。

当前采用的边界：

- 恢复 `.github/workflows/ci.yml`，但不恢复 GitHub Actions CI 教程，也不把 CI 写成独立 Step。
- workflow 分为 `unit`、`integration`、`sanitizers` 三个 job，复用 Step 44 已有 CTest labels 和 `LITEIM_ENABLE_SANITIZERS=ON`。
- `integration` 和 `sanitizers` job 使用仓库已有 `docker/docker-compose.yml` 启动 MySQL/Redis，避免在 workflow 中重复维护第二套服务配置。
- README 顶部增加 GitHub Actions badge，并新增 `Repository CI` 小节说明 CI 是 infra，不是编号 Step。
- 不修改 C++ 源码、服务端协议、MySQL schema、Redis key、Qt 或 PersonaAgent。
- 不暂存进入本任务前已有的用户侧源码改动。

## 2026-05-16 Step 44 Test Coverage, gMock, ASan/UBSan Findings

本次进入 `Step 44：补齐单元测试覆盖率 + gMock + ASan/UBSan`。`PROJECT_MEMORY.md` 的要求是补复杂模块测试覆盖、引入 gMock 边界测试、给 Docker 依赖测试打 CTest 标签，并增加 `LITEIM_ENABLE_SANITIZERS=ON` 构建。

当前采用的边界：

- 只补测试、CTest 注册和 sanitizer 构建入口，不修改 LiteIM TCP/TLV 协议、MySQL schema、Redis key、业务 service 语义、Qt 或压测工具行为。
- 保留已有 fake 测试，用 gMock 专门覆盖 service 与 `IStorage` / `ICache` / `OnlineService` 的依赖调用边界。
- Docker 依赖测试保留原有 skip 语义；MySQL/Redis 不可用时跳过依赖真实服务的测试，不影响普通 unit 标签测试。
- sanitizer 只作为可选构建，不改变默认构建类型、C++ standard 或生产依赖。

已经采用的设计：

- 新增 `tests/mocks/MockStorage.hpp` 和 `tests/mocks/MockCache.hpp`，用 `MOCK_METHOD` 覆盖 `IStorage` / `ICache` 纯虚接口。
- 新增 `tests/service/service_mock_boundary_test.cpp`，验证 Auth 登录限流/失败记录/成功清理绑定、Chat 在线/离线收件人边界、Group 群存在和成员校验后保存/未读、History 权限校验后才查询历史。
- 扩展 FrameDecoder、TlvCodec、ThreadPool、TimerHeap 边界测试，覆盖半包 split、粘包、多连续包、空 body、重复字段、非法长度、空队列 stop/restart、重复 cancel、未知 cancel 和相同 deadline。
- `tests/CMakeLists.txt` 链接 `GTest::gmock`，并拆分 CTest discovery。普通 GoogleTest 标记为 `unit`，MySQL/Redis/Docker/E2E 相关测试可用 `-L integration`、`-L mysql`、`-L redis`、`-L docker`、`-L e2e` 筛选。
- 根 `CMakeLists.txt` 增加 `LITEIM_ENABLE_SANITIZERS`。GNU/Clang 下添加 `-fsanitize=address,undefined`、`-fno-omit-frame-pointer`、`-fno-sanitize-recover=all`；其他编译器开启该选项直接 CMake fatal。
- ASan/UBSan 首轮全量测试暴露 5 个既有测试断言问题：`EXPECT_EQ(const char*, "...")` 比较的是地址而不是内容。已改为 `EXPECT_STREQ`，随后 ASan/UBSan 全量通过。

本次不采用/不改：

- 不引入 `lcov` / `gcovr` 覆盖率报告。
- 不把所有 fake 测试替换成 gMock。
- 不新增 death test 数量指标，不改变已有 owner-loop-only death 行为。
- 不修改 server runtime、业务协议、数据库结构、Redis key、Qt 或 PersonaAgent。

## 2026-05-16 Step 43 Benchmark Tool Findings

本次进入 `Step 43：实现自研压测工具`。`PROJECT_MEMORY.md` 的要求是实现 `bench/liteim_bench.cpp`，支持多长连接、可配置消息大小/发送间隔/持续时间、登录后私聊发送、统计连接成功数、QPS、平均延迟、p50/p95/p99、错误数、内存和 CPU 使用，并输出 JSON 或 Markdown 报告片段。

当前采用的边界：

- 压测工具作为独立本地客户端，不修改 `liteim_server` 协议、MySQL schema、Redis key 或业务 service 行为。
- 可测试逻辑拆进 bench helper 库：参数解析、用户名生成、payload 生成、延迟统计、资源采样和报告渲染。
- 可执行入口 `bench/liteim_bench.cpp` 负责并发连接、注册/登录、私聊请求、响应匹配、资源采样和报告输出。
- 第一版复用普通 `RegisterRequest` / `LoginRequest` / `PrivateMessageRequest`，每个连接注册唯一用户名，所有 worker 把私聊发给同一个接收用户。
- README 只记录本机真实小规模验证命令和输出字段，不写未经验证的夸张 QPS。

本次不采用/不改：

- 不实现分布式压测、连接复用到多进程、复杂场景脚本、图表生成、默认测试压测、sanitizer 构建或 Qt。
- 不把压测默认加入全量 CTest，避免本地依赖和耗时污染普通测试；只保留 helper 单元测试和手动运行命令。

实现确认：

- `liteim_bench` 使用 1 个 receiver 和 `connections - 1` 个 sender；`connections` 表示总长连接数，最小为 2。
- receiver 后台读取 push，避免普通吞吐压测被慢接收端 backpressure 行为污染。
- 延迟分位数采用 nearest-rank：`rank = ceil(q * count)`。
- 本地 smoke 在 2026-05-16 运行：`--connections 4 --message-size 64 --interval-ms 20 --duration-sec 1 --format json`，结果 `connection_success=4/4`、`request_success=114`、`error_count=0`、`p99_us=9403`。

## 2026-05-16 Step 42 Python E2E Findings

本次进入 `Step 42：实现 Python 端到端测试`。用户确认 MySQL 和 Redis 在 Docker 环境中运行，因此 Step 42 直接沿用 LiteIM 默认 Docker 端口：

- MySQL：`127.0.0.1:33060`
- Redis：`127.0.0.1:63790`
- `liteim_server`：默认监听 `0.0.0.0:9000`

当前采用的 E2E 边界：

- Python 测试作为黑盒客户端，不链接 C++ 库，不绕过 TCP/TLV 协议。
- Python 侧实现最小 Packet/TLV 编解码、阻塞 socket client、server 启停 helper 和常用 IM 操作 helper。
- CTest 负责调用 Python `unittest` 文件，并通过 `LITEIM_SERVER_BIN=$<TARGET_FILE:liteim_server>` 指向当前构建产物。
- 每个 E2E test module 自己启动一个 `liteim_server` 进程；CTest 使用同一个资源锁串行运行这些模块，避免默认端口 `9000` 冲突。
- 测试数据使用唯一用户名注册，不依赖 seed 用户 `alice` / `bob` 的 dev hash，因为 seed 密码不是 AuthService 真实 PBKDF2 hash。

本次不采用/不改：

- 不修改 C++ 协议枚举、TLV 字段、MySQL schema、Redis key 或业务 service 行为。
- 不给 `liteim_server` 新增配置文件参数或动态端口参数；Step 42 第一版按默认配置运行。
- 不安装新的全局 Python 依赖；E2E 使用 Python 标准库 `unittest` / `socket` / `subprocess`。

实现确认：

- `tests/e2e/liteim_e2e.py` 中的 Python Packet header 使用 `!IBBHQI`，对应 C++ 20 字节 header：magic、version、flags、msg_type、seq_id、body_len。
- Python TLV body 使用 `!HI`，对应 C++ `type(2) + len(4) + value`。
- `LiteIMClient.request()` 通过 `seq_id` 匹配 response；收到其他包时先缓存为 push，后续由 `expect_push()` 消费。
- `LiteIMServer` 默认启动 `LITEIM_SERVER_BIN`，也支持 `LITEIM_E2E_USE_EXISTING_SERVER=1` 连接已运行 server。
- Backpressure E2E 不读取接收端 push，连续发接近 MySQL `TEXT` 上限的消息，让真实 server 输出缓冲积压并观察 slow receiver 被关闭。

## 2026-05-15 Step 38 GroupService Findings

本次按 `PROJECT_MEMORY.md` 进入 `Step 38：实现 GroupService 群聊`。长期边界已经确认：

- 做：`CreateGroupRequest`、`JoinGroupRequest`、`ListGroupsRequest`、`GroupMessageRequest`、群成员查询、群消息 MySQL 保存、在线成员 `GroupMessagePush`、离线成员 `offline_messages` 和 Redis unread +1。
- 不做：复杂群权限、群公告、群禁言、群消息已读回执、广播优化、可靠 ACK、跨节点路由。

代码核对结果：

- `MessageType` 已经有 `CreateGroupRequest/Response`、`JoinGroupRequest/Response`、`ListGroupsRequest/Response`、`GroupMessageRequest/Response` 和 `GroupMessagePush`。
- `TlvType` 已经有 `GroupId` 和 `GroupName`，消息字段可复用 `ConversationType`、`ConversationId`、`MessageId`、`SenderId`、`ReceiverId`、`MessageText` 和 `TimestampMs`。
- MySQL schema 和 DAO 基础已经存在：`chat_groups`、`group_members`、`GroupDao::createGroup()`、`addGroupMember()`、`getGroupMembers()`、`findGroupById()`。
- `IStorage` 原本已经暴露建群、加群、移除成员和查群成员，但还没有“按 user_id 列出我的群”的接口；`ListGroupsRequest` 不能在不破坏 service/storage 边界的情况下完整实现。
- 用户已确认采用方案 A：扩展 `GroupDao` / `IStorage` / `MySqlStorage`，不让 `GroupService` 直接依赖具体 DAO。

已经采用的设计：

- 扩展 `GroupDao` / `IStorage` / `MySqlStorage`，新增 `getGroupsForUser(std::uint64_t user_id, std::vector<GroupRecord>& groups)`。
- 同时暴露 `findGroupById(std::uint64_t group_id, GroupRecord& group)` 到 `IStorage`，让 `GroupService` 能区分 group 不存在与其他失败。
- `GroupService` 继续只依赖 `IStorage`、`ICache` 和 `OnlineService`，不直接依赖 DAO。
- `ListGroupsResponse` 第一版返回重复的 `GroupId` 和 `GroupName`；不新增协议字段。
- `JoinGroupRequest` 先 `findGroupById()`，再 `addGroupMember()`，重复加入保持 DAO 幂等语义。
- `GroupMessageRequest` 先校验当前 session 登录、group 存在、发送者在 `group_members` 中；发送者不再额外收到 push，只收到 `GroupMessageResponse`。
- 群消息使用 `ConversationType::kGroup`，`conversation_id == group_id`，`receiver_id == group_id`。
- 离线群成员在 MySQL message/offline row 已保存后执行 Redis unread +1；如果 Redis unread 失败，只记录 warning，不把发送方 response 变成失败，避免客户端重试产生重复消息。

本次不采用/不改：

- 不新增 TLV 字段，不修改 MySQL schema。
- 不实现复杂群权限、群公告、群禁言、群已读回执、大群广播优化、可靠 ACK、跨节点路由。
- 不更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的完成状态；该文件只保留长期设计路线和边界。

## 2026-05-15 Pre-Step38 Critical Hardening Findings

本次不是新功能 Step，也不命名为 `Step 36.1`。范围是 Step 38 群聊前的独立 runtime correctness hardening，只处理两个已确认问题：

- session close 后需要清理 `SessionManager` 和 Redis online state。原代码里 `TcpServer` close callback 只删除 `TcpServer::sessions_`，不会通知 `OnlineService`。
- 私聊离线分支里，MySQL `messages` 和 `offline_messages` 已成功保存后，如果 Redis unread 递增失败，不应给发送方返回失败；否则客户端重试会制造重复消息。

已经确认并采用的设计：

- `TcpServer` 新增轻量 `SessionCloseCallback`，close callback 顺序固定为先 `removeSession(session_id)`，再调用外部 callback。
- `server/main.cpp` 把 close cleanup 投递到 business `ThreadPool`，业务任务调用 `OnlineService::unbindSession(session_id)`；I/O close callback 不直接做 Redis 阻塞调用。
- `SessionManager::getBoundUserBySession(session_id, user_id)` 只查绑定表，不因为 `Session::closed()` 清理映射，专门服务 close cleanup。
- `OnlineService::unbindSession(session_id)` 先反查 user id，再复用 `unbindUser(user_id, session_id)`，继续保留旧 session 不能误删新 Redis online key 的语义。
- `ChatService` 保持 MySQL 为消息事实来源。离线消息保存成功后，`ICache::incrUnread()` 失败只记录 warning，仍向发送方返回 `PrivateMessageResponse`。

本次明确不做：

- 不实现可靠 ACK、pending delivery、好友关系强制校验、HeartbeatService Redis TTL 刷新、群聊、schema 变更或 client message id 去重。
- 不把 `sendPacket()` 返回值当成可靠送达判断；当前跨线程发送只是排队到 owner loop，真正写出和后续关闭发生在之后。
- 不扩大 `PROJECT_MEMORY.md` 体量；本次只同步 README、相关教程和 planning 文件的当前行为边界。

## 2026-05-15 Step 37 OfflineMessageService Findings

本次进入 `Step 37：实现 OfflineMessageService`，采用用户确认的方案一：登录仍只返回 `LoginResponse`，客户端登录成功后主动发送 `OfflineMessagesRequest` 拉取离线消息。

已经确认并采用的设计：

- `OfflineMessageService` 位于 `liteim_service`，依赖 `IStorage`、`ICache` 和 `OnlineService`，不直接依赖具体 DAO 或 Redis 组件。
- `OfflineMessagesRequest` 通过 `MessageRouter::DispatchMode::BusinessThread` 执行，MySQL / Redis 阻塞调用不进入 Reactor I/O 线程。
- 当前登录用户身份来自 `OnlineService::getUserBySession(session_id)`，请求 body 不信任客户端传入的 `UserId`。
- 请求 body 可选 `TlvType::Limit`；未提供时使用默认上限，超过 Step 37 上限时截断，第一版每次最多返回 100 条。
- `OfflineMessagesResponse` 使用重复 TLV 字段返回每条消息的 `MessageId`、`ConversationType`、`ConversationId`、`SenderId`、`ReceiverId`、`MessageText` 和 `TimestampMs`。
- 本批响应构造成功后，先清理本批涉及会话的 Redis 未读计数，再把本批 message_id 标记 delivered，避免 delivered 成功后还有后续失败路径导致客户端只收到错误响应。

本次不采用/不改：

- 不修改 `AuthService` 登录响应模型，不让登录 handler 额外发送 `OfflineMessagesResponse`。
- 不修改 `MessageRouter` 支持多 response 或 follow-up response。
- 不实现可靠投递 ACK、ACK 重试、离线消息删除、历史分页、群聊、跨节点路由。

## 2026-05-14 Step 36 ChatService Findings

本次进入 `Step 36：实现 ChatService 私聊`，只实现单进程私聊发送闭环，不推进群聊、离线消息拉取、历史查询、跨节点路由、可靠 ACK、好友策略校验。

已经确认并采用的设计：

- `ChatService` 位于 `liteim_service`，依赖 `IStorage`、`ICache` 和 `OnlineService`，不直接依赖具体 DAO 或 Redis cache 组件。
- `PrivateMessageRequest` body 只读取 `ReceiverId` 和 `MessageText`；发送者身份来自 `OnlineService::getUserBySession(session_id)`，不信任客户端传入的 `SenderId`。
- handler 通过 `MessageRouter::DispatchMode::BusinessThread` 执行，MySQL / Redis 阻塞调用不进入 Reactor I/O 线程。
- 私聊 `conversation_id` 由服务端根据发送方和接收方生成；seed 用户 `1001` / `1002` 保持示例值 `10011002`，更大用户 id 使用稳定 pair 规则。
- 保存顺序固定为先 `IStorage::saveMessageWithOfflineRecipients()`，再根据接收方在线状态 push 或递增未读。
- 在线投递只使用当前进程 `SessionManager` 绑定拿到的 `Session`；第一版不做 Redis 在线状态到跨节点路由的转换。
- 接收方离线时，offline recipient 写 `{receiver_id}`，MySQL 保存成功后再调用 `ICache::incrUnread()`。
- 成功 response 和 push 都写入 `MessageId`、`ConversationType`、`ConversationId`、`SenderId`、`ReceiverId`、`MessageText`、`TimestampMs`。

本次不采用/不改：

- 不实现群聊、离线消息拉取、历史查询、消息撤回、已读回执、可靠 ACK、跨节点转发、好友关系强制校验。
- 不新增协议类型或 TLV 字段，不修改 MySQL schema。
- 不删除有价值的 process/debug Markdown 历史；只修正当前会误导后续 agent 的标题和明显重复措辞。

## 2026-05-14 Markdown Audit Findings

本次按用户要求检查当前 Markdown 文件：

- 当前-facing README、AGENTS、CLAUDE 和 docs/tutorials 没有旧 `Current Status` / `当前状态` / 教程提交章节 / `#Lxx` 行号锚点残留。
- 教程 `step00` 到 `step36` 都符合固定 0-10 模板，最后主章节都是 `## 10. 面试常见追问`，且都保留真实数据例子小节。
- 发现并修复 `step34_auth_service.md` 和 `step35_friend_service.md` 中的重复措辞：`不实现 本 Step 不实现...`。
- `docs/process/task_plan.md` 中旧 `Current Decision` 区块虽然属于过程历史，但标题容易误导，已改为 `Historical Route Snapshot`。
- `docs/process/task_plan.md` 中旧 Step0 kept-files 列表残留了已经删除的 `docs/architecture.md`、`docs/project_layout.md`、`docs/tutorials/README.md`，已从该列表移除。
- 没有删除 `docs/debug_cases/`、planning 历史或其他仍有查证价值的 Markdown 过程记录。

## 2026-05-14 Step Tutorial Markdown Compact Lecture Rewrite Findings

本次是 Markdown-only 教程重构，范围是 `docs/tutorials/step00_reset.md` 到 `docs/tutorials/step35_friend_service.md`，不修改 C++、SQL、CMake、测试源码或协议行为。

确认并采用的新教程模板：

- 每篇教程固定使用 0-10 个二级章节：`本 Step 结论`、`为什么需要这个 Step`、`本 Step 边界`、`文件变化`、`核心接口与契约`、`运行流程`、`关键实现点`、`测试设计`、`验证命令`、`面试表达`、`面试常见追问`。
- `本 Step 边界` 提前到第 2 节，拆成“本 Step 做”和“本 Step 不做”，避免边界信息被放到文末。
- `文件变化` 改为三列表格：`文件 | 变化 | 作用`。
- `测试设计` 按“风险 -> 测试如何覆盖”组织，再保留必要测试名。
- `面试表达` 固定为“一句话 / 展开说 / 容易被追问”。
- `面试常见追问` 继续作为最后一个主章节，不新增教程提交信息章节。
- 保留“该项目代码在实际应用中的具体数据例子”，继续使用 LiteIM 真实对象和值，例如 `user_id=1001`、`session_id=42`、`seq_id=7`、`message_id=5001`、`online:user:1002`。

过程注意点：

- 当前工作区进入任务前已有未提交 C++ 改动和 `docs/tutorials/step32_session_manager_online_service.md` 改动；本次教程重构基于当前工作区内容处理 Step 32，未回滚这些用户侧改动。
- 本次同步 `/home/yolo/jianli/PROJECT_MEMORY.md`、`AGENTS.md` 和 `CLAUDE.md` 只更新教程约束文字，不写完成状态或活动下一步。
- 不创建 `docs/tutorials/README.md`，不把本次进度写进 `README.md`。

## 2026-05-14 Step 35 FriendService Findings

本次进入 `Step 35：实现 FriendService`，只实现好友添加和好友列表业务，不推进私聊、群聊、好友申请审批、黑名单、备注名、离线消息、历史消息、HeartbeatService。

已经确认并采用的设计：

- 用户已选择协议方案一：新增 `TlvType::OnlineStatus`，用 `uint64` 表示好友在线状态，`1` 为在线，`0` 为离线。
- `FriendService` 位于 `liteim_service`，依赖 `IStorage`、`ICache` 和 `OnlineService`，不直接依赖具体 DAO 或 Redis cache 组件。
- `AddFriendRequest` 使用 `TargetUserId` 表达要添加的用户；`AddFriendResponse` 返回好友公开资料和在线状态。
- `ListFriendsRequest` 不需要额外 body；当前登录用户来自 `OnlineService::getUserBySession()`。
- `ListFriendsResponse` 对每个好友重复写入 `FriendId`、`Username`、`Nickname` 和 `OnlineStatus`，按字段出现顺序一一对应。
- 添加好友前先确认当前 session 已登录，再确认目标用户存在，再通过 `IStorage::getFriends()` 检查重复好友，重复时返回 `AlreadyExists`。
- 在线状态通过 `ICache::isUserOnline(friend_id, online)` 查询；Redis/cache 错误不被静默吞掉。
- 所有 MySQL / Redis 调用都通过 `MessageRouter` 的 `BusinessThread` handler 执行，不进入 Reactor I/O 线程。

本次不采用/不改：

- 不做好友申请审批、黑名单、备注名、删除好友或搜索用户。
- 不实现私聊、群聊、离线消息、历史消息、HeartbeatService。
- 不把 MySQL / Redis 阻塞调用放进 Reactor I/O 线程。
- 不修改 MySQL schema。

## 2026-05-14 Step 34 AuthService Findings

本次进入 `Step 34：实现 AuthService 注册登录`，只实现注册/登录业务闭环，不推进 FriendService、ChatService、离线消息、历史消息、HeartbeatService、客户端。

已经确认并采用的设计：

- `AuthService` 位于 `liteim_service`，依赖 `IStorage`、`ICache` 和 `OnlineService`，不直接依赖具体 DAO/cache 组件。
- `RegisterRequest` / `LoginRequest` 通过 `AuthService::registerHandlers()` 注册到 `MessageRouter`，dispatch mode 固定为 `BusinessThread`。
- 密码哈希使用 OpenSSL `PBKDF2-HMAC-SHA256`，注册时生成 16 字节随机 salt，MySQL 保存 hex salt 和 hex hash。
- 注册请求读取 `Username`、`Password`，`Nickname` 可选；缺失 nickname 时默认使用 username。
- 注册成功返回 `RegisterResponse`，body 写入 `UserId`、`Username`、`Nickname`。
- 登录前先通过 `ICache::allowLoginAttempt()` 检查 Redis 登录失败限制。
- 用户不存在和密码错误都返回统一的 `invalid username or password`，并记录一次登录失败，避免通过错误消息枚举用户。
- 登录成功先清理 Redis 登录失败计数，再调用 `OnlineService::bindUser()` 写在线状态并绑定 `SessionManager`。
- 登录成功返回 `LoginResponse`，body 写入 `UserId`、`Username`、`Nickname`、`SessionId`。
- 当前 `Session` 不暴露 peer IP，本 Step 不改网络层公开接口；`LoginAttemptKey.remote_ip` 暂时使用 `AuthServiceOptions::default_remote_ip`。
- `server/main.cpp` 现在启动 MySQL / Redis pool，注入 `MySqlStorage` / `RedisCache` / `OnlineService` / `AuthService`，并继续保持 `SignalWatcher::start()` 早于业务线程池启动。

本次不采用/不改：

- 不修改 MySQL schema，不新增协议消息类型或 TLV 字段。
- 不实现 JWT、OAuth、短信、邮箱验证码、生产级账号安全体系或真实 peer IP 获取。
- 不把 MySQL / Redis 阻塞调用放进 Reactor I/O 线程。
- 不接入好友、私聊、群聊、离线消息、历史消息。

## 2026-05-14 Step 33 MessageRouter Findings

本次进入 `Step 33：实现 MessageRouter 异步分发框架`，只实现业务入口骨架和 `TcpServer` 运行时接入，不实现 AuthService、ChatService、好友、群聊、历史消息。

已经确认并采用的设计：

- `MessageRouter` 位于 `liteim_service`，`TcpServer` 不持有 Router，避免 `net` 模块反向依赖 `service`。
- `MessageRouter::route(Session::Ptr, Packet)` 是 `TcpServer::setMessageCallback()` 的接入点。
- Router 先校验 `msg_type` 是否为 request，再统一解析 TLV body；TLV parse 失败直接返回 `ErrorResponse`。
- Handler 注册接口为 `registerHandler(MessageType, Handler, DispatchMode)`；`DispatchMode` 只有 `Inline` 和 `BusinessThread`。
- `Handler` 接收 `RouterRequest { session, packet, fields }`，填写响应 `Packet`，返回 `Status`。
- 默认 `HeartbeatRequest` 是 inline 轻量 handler，只返回 `HeartbeatResponse`，不访问 Redis；完整 HeartbeatService 留到 Step 40。
- BusinessThread handler 通过 Step 17 的固定 `ThreadPool::submit()` 执行；submit 失败会回 `ErrorResponse`。
- Router 强制响应 `seq_id` 等于请求 `seq_id`，不信任 handler 自己填写的 seq。
- 异步任务捕获 `weak_ptr<Session>`，任务开始前和发送前都检查 session 是否还存在且未关闭；Router 不直接读写 fd、`Buffer` 或 `Channel`。
- `server/main.cpp` 现在启动 `ThreadPool business_pool(config.business_threads)`，创建 `MessageRouter router(business_pool)`，并通过 `server.setMessageCallback()` 接入。
- `SignalWatcher::start()` 必须早于 `ThreadPool::start()`，否则业务线程不会继承阻塞的 SIGINT/SIGTERM 掩码，`LiteIMServerSignalTest` 中的 SIGTERM 可能落到业务线程并按默认动作杀进程。

本次不采用/不改：

- 不实现注册、登录、密码哈希、登录限流、好友、私聊、群聊、离线消息、历史消息。
- 不把 MySQL / Redis 阻塞调用放进 Reactor I/O 线程。
- 不修改 Step32 现有未提交的 `SessionManager`、`OnlineService`、`ThreadPool` 注释改动。
- `/home/yolo/jianli/PROJECT_MEMORY.md` 只修正 PersonaAgent 依赖段的旧 Step 编号漂移，主路线 Step33/34 设计不变。

## 2026-05-14 Step 32 SessionManager / OnlineService Findings

本次进入 `Step 32：实现 SessionManager 和 OnlineService`，只实现登录态绑定和在线状态同步基础能力，不实现 AuthService、MessageRouter、ChatService 或 `TcpServer` 运行时协议接入。

已经确认并采用的设计：

- 重复登录策略采用用户确认的“踢旧保新”：同一个 `user_id` 绑定新 session 时，内存绑定切到新 session，旧 session 在锁外关闭，Redis 在线状态指向新 session。
- `SessionManager` 维护 `user_id -> weak_ptr<Session>` 和 `session_id -> user_id` 两张表；查询时如果 weak_ptr 过期或 session 已关闭，会清理 stale binding 并返回 `NotFound`。
- `SessionManager::unbindUser(user_id, session_id, removed)` 只删除当前匹配的绑定；旧 session 延迟 close 回调不会误删新 session 的绑定。
- `OnlineService` 在登录绑定时先写 `ICache::setUserOnline()`，再更新内存绑定；心跳刷新要求 `(user_id, session_id)` 仍是当前绑定，防止旧 session 刷新新登录的 Redis TTL。
- `OnlineService::unbindUser()` 只有在 `SessionManager` 确认删除了当前绑定时才调用 `ICache::setUserOffline()`，避免旧 session close 回调删除新 session 的 `online:user:<user_id>`。
- 新增 `liteim_service` 模块，链接 `liteim_net` 和 `liteim_cache`；它仍然是阻塞 cache 调用的上层 service，后续应由 business 线程调用，不进入 Reactor I/O 线程。

本次不采用/不改：

- 不实现注册、登录密码校验、登录限流业务组合、消息路由、私聊/群聊 service、客户端协议响应。
- 不把 `OnlineService` 接入 `TcpServer::setMessageCallback()`、`Session` close callback 或 server main。
- 不做跨进程转发；Redis 在线状态只为状态展示和后续多进程扩展保留信息。

## 2026-05-13 Step 31 Route Documentation Alignment Findings

本次是 Markdown-only 路线调整，不修改 C++ 逻辑，不回滚现有 dirty diff。

已经确认并采用的调整：

- 原独立存储/缓存契约小重构适合作为正式 Step 31，因为它正好收束 Step 21 的 `IStorage` / `ICache` 抽象和 Step 23-30 的 MySQL / Redis 具体组件。
- Step 31 的教学重点是 `MySqlStorage : IStorage` 和 `RedisCache : ICache` 如何实现抽象接口，不再把这部分只写成 Pre-Step。
- 原 Step 31 `SessionManager and OnlineService` 后移为 Step 32；后续业务服务、CLI/测试、Qt 客户端和最终文档 Step 编号整体顺延。
- Step 31 仍不接入 `TcpServer` runtime，不实现 AuthService、ChatService、`SessionManager` 或 `OnlineService`；阻塞 MySQL / Redis 调用仍然只能给后续 business 线程使用。

本次同步范围：

- `/home/yolo/jianli/PROJECT_MEMORY.md` 的全局优先级、Step 31 内容、Phase 5-8 Step 编号。
- README、`docs/process/task_plan.md`、`docs/process/findings.md`、`docs/process/progress.md`。
- Step 21 / Step 23 / Step 26 相关教程引用。
- 新增 `docs/tutorials/step31_storage_cache_adapters.md`。

## 2026-05-12 Markdown Contract Alignment Findings

本次是 Markdown-only 同步修复，不修改 C++ 逻辑，不回滚现有 dirty diff，也不把它记成 Step 31。

确认并修正的文档契约：

- Step 教程主结构不再以“提交信息”收尾；教程只讲概念、接口、运行流程、测试和“面试常见追问”。计划 commit message 只保留在 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 路线和 planning 文件中。
- 每个教程“作用场景和运行流程”的第 5 小节统一为“该项目代码在实际应用中的具体数据例子”，内容必须使用 LiteIM 真实对象和数值，例如 `user_id=1001`、`conversation_id=10011002`、`message_id=5001`、`seq_id=7`、`session_id=42`、`online:user:1002`。
- MySQL history 索引字段名统一按当前 schema 表达为 `(conversation_type, conversation_id, message_id)`；旧 `(conv_type, conv_id, id)` 属于历史/简写漂移，不再用于当前设计说明。
- HeartbeatService 不直接负责刷新 `Session::last_active_time`。完整、合法的入站 Packet 已经在 `Session` 读路径刷新连接活跃时间；HeartbeatService 只返回心跳响应，并为已登录用户刷新 Redis 在线 TTL。
- `MySqlStorage::saveMessageWithOfflineRecipients()` 当前契约是：先对重复离线用户去重；如果 `queryLastInsertedMessage()` 成功后、`COMMIT` 前插入离线记录失败，会 `ROLLBACK` 并清空 `saved_message` 输出参数，避免半成功结果被上层误用。
- 当前完整测试总数应记录为 `254/254`，新增通过项来自 `SaveMessageWithOfflineRecipientsDeduplicatesOfflineUsers`。

## 2026-05-12 Step 31 Storage / Cache Adapter Layer Findings

本次现在纳入正式 `Step 31：MySqlStorage / RedisCache 聚合适配层`，不开启 `SessionManager`、`OnlineService`、AuthService 或 ChatService。

已经确认的重构目标：

- Step21 的 `IStorage` / `ICache` 需要真实 MySQL / Redis 聚合适配层，否则 Step 32 之后业务层会被迫依赖具体 DAO/cache 组件。
- `FriendDao::getFriends()` 不应该返回带 `password_hash` / `password_salt` 的 `UserRecord`；好友列表应返回公开资料 DTO。
- MySQL schema 使用 `BIGINT UNSIGNED`，`PreparedStatement` 需要提供 `bindUInt64()`，避免 DAO 长期用 signed bind 检查绕过类型不一致。
- 私聊/群聊业务需要一个 MySQL 事务边界覆盖 `messages` + `offline_messages`，Redis 未读数仍放在业务层提交后处理，不做跨 MySQL/Redis 事务。
- `UnreadCounter::incrUnread()` 的 `delta` 需要限制在 Redis 有符号 64 位整数范围内。
- `LoginRateLimiter` 当前是“允许检查”和“失败记录”分离的粗粒度滑动失败窗口；这轮先明确语义，不在小重构里实现完整登录原子判定脚本。
- 这条结论覆盖早期 Step27 记录中的“`getFriends()` 返回完整 `UserRecord`”；当前接口已经改成 `UserProfileRecord`。

本次明确不做：

- 不推进 Step 32，不接入 `TcpServer` runtime，不实现 `SessionManager` / `OnlineService`。
- 不实现完整 AuthService、ChatService、业务协议处理或 Python/Qt 客户端。
- 不引入 ORM、Redis 分布式锁、Pub/Sub、Streams 或跨资源事务。

## 2026-05-11 Step 30 UnreadCounter / LoginRateLimiter Findings

本次进入 `Step 30：实现 UnreadCounter 和 LoginRateLimiter`，只实现 Redis 未读计数和登录失败限流，不实现 AuthService、ChatService、SessionManager、OnlineService 或网络层运行时接入。

已经确认并采用的设计：

- `UnreadCounter` 建在 `RedisPool` 之上，负责 `incrUnread()`、`getUnread()` 和 `clearUnread()`。
- 未读计数 key 使用 user id + conversation type + conversation id 组成，确保私聊、群聊和不同用户之间互不影响。
- `incrUnread()` 使用 Redis `EVAL` 执行 `INCRBY`，保持 `delta` 递增是单条 Redis 原子操作，同时不扩展 Step 28 的 `RedisClient` public API。
- `getUnread()` 对不存在 key 返回 0；如果 Redis value 不是合法无符号整数，返回 `ParseError`，避免损坏缓存被当成正常未读数。
- `clearUnread()` 使用 `DEL`，不存在 key 也视为成功。
- `LoginRateLimiter` 使用 Redis failure key + TTL 表达短期登录失败窗口，负责 `allow()`、`recordFailure()` 和 `clear()`。
- 第一版 `allow()` 读取当前失败次数并和 `max_failures` 比较；`recordFailure()` 使用 Redis `EVAL` 在同一脚本内执行 `INCR` 和 `EXPIRE`。
- Redis API 仍然是阻塞 API，只能给后续 business `ThreadPool` 使用；Reactor I/O 线程仍然不能直接调用。

本次不采用/不改：

- 不实现 AuthService 登录校验、ChatService 消息投递、未读数 runtime 递增、登录失败 runtime 记录或登录成功 runtime 清理。
- 不新增 Redis pipeline、token bucket 复杂算法、分布式锁、Pub/Sub、Streams 或 Cluster。
- 不修改 MySQL schema、Docker Compose、seed 数据或网络层运行时。

## 2026-05-11 Step 21-29 Tutorial Format Alignment Findings

本次是 Markdown-only 教程格式修正，不修改 C++、SQL、CMake、README 或 `PROJECT_MEMORY.md`。

确认并采用的修正文档规则：

- `docs/tutorials/step21_storage_cache_interfaces.md` 到 `docs/tutorials/step29_online_status_cache.md` 都按固定教学结构补齐：概念、新增/修改文件、详细接口或契约说明、作用场景和运行流程、后续边界、测试设计、验证命令、面试说法和面试常见追问。
- `作用场景和运行流程` 不再只写函数流水线；每个 Step 都拆成 5 个小节：在 LiteIM 里的具体使用场景、上下层调用连接、整体运行链路、自身内部运行流程、小例子和边界。
- `hpp 接口说明` 需要覆盖 public API、关键成员变量、private helper、失败语义、线程边界和所有权边界；Step 22 没有 C++ header，所以写等价的 Docker Compose / SQL 脚本契约说明。
- 本次明确避开已有未提交的 `docs/tutorials/step20_backpressure.md`、net 源码/header 和 `scripts/seed_test_data.sql` 改动，避免把用户侧或其他任务改动混进本轮教程修正。

## 2026-05-11 Step 29 OnlineStatusCache Findings

本次进入 `Step 29：实现 OnlineStatusCache`，只实现 Redis 在线状态缓存，不实现未读计数、登录失败限制、业务 service、SessionManager、OnlineService 或网络层运行时接入。

已经确认并采用的设计：

- `OnlineStatusCache` 是 `RedisPool` 之上的窄 cache 组件，负责 `setUserOnline()`、`refreshUserOnline()`、`setUserOffline()`、`isUserOnline()` 和 `getOnlineSession()`。
- 在线状态 key 固定为 `online:user:<user_id>`，符合 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 29 设计。
- value 保存 `user_id`、`session_id`、`server_id` 和 `last_active_time_ms`，带简单版本前缀和 `server_id` 长度，避免 server id 中包含冒号时被错误切分。
- `setUserOnline(user_id, server_id, session_id, ttl)` 自动填充当前毫秒时间；`setUserOnline(OnlineSession, ttl)` 在 `last_active_time_ms <= 0` 时也会自动补当前时间。
- 上线写入使用 `RedisClient::setex()`，心跳刷新使用 `RedisClient::expire()`，下线删除使用 `RedisClient::del()`，读取状态使用 `RedisClient::get()`。
- `refreshUserOnline()` 对不存在 key 返回 `NotFound`；`setUserOffline()` 对不存在 key 视为成功，依赖 Redis TTL 兜底最终一致性。
- `isUserOnline()` 只有在 key 存在且 value 可解析时返回 online=true；损坏 value 返回 `ParseError`，避免把错误缓存静默当成有效在线状态。
- Redis API 仍然是阻塞 API，只能给后续 business `ThreadPool` 使用；Reactor I/O 线程仍然不能直接调用。

本次不采用/不改：

- 不实现 Step 30 的 `UnreadCounter`、`LoginRateLimiter` 或对应 Redis key。
- 不实现 Step 32 的 `SessionManager`、`OnlineService`、登录成功写 Redis、心跳 runtime 续期或断开连接 runtime 清理。
- 不接入 `ThreadPool`、`TcpServer`、`Session`、AuthService、ChatService 或任何运行时业务流程。
- 不实现 Redis Cluster、Pub/Sub、Streams、分布式锁、pipeline 或异步 Redis。
- 不修改 Docker Compose、Redis 初始化数据、MySQL schema、seed 数据或 Step 21 `ICache` 接口。

## 2026-05-11 Step 28 RedisClient / RedisPool Findings

本次进入 `Step 28：实现 RedisClient 和 RedisPool`，只实现 Redis 阻塞客户端和固定连接池，不实现在线状态 cache、未读数、登录失败限制、业务 service 或网络层运行时接入。

已经确认并采用的设计：

- `liteim_cache` 从 header-only interface target 升级为静态库，并通过 `pkg-config hiredis` 链接系统 hiredis。
- `RedisClient` 拥有一个 `redisContext*`，负责 `connect()`、`ping()`、`setex()`、`get()`、`del()`、`incr()`、`expire()`、`eval()`、`close()` 和析构释放。
- `connect()` 使用 `RedisConfig` 的 host、port、password、db；默认连接 Step 22 Docker Redis `127.0.0.1:63790`，密码 `6`。
- Redis 命令使用 `redisCommandArgv()`，避免拼接命令字符串时把 key/value/script 内容误解释成命令片段。
- `get()` 和 `eval()` 用 `std::optional<std::string>` 输出 Redis NIL；`incr()` 输出 `std::int64_t`；`del()` 输出删除数量；`expire()` 输出是否刷新到已有 key。
- `RedisPool` 保存固定数量 `RedisClient`，`start()` 一次性连接，`acquire(timeout, guard)` 等待空闲连接，超时返回 `IoError`。
- `RedisConnectionGuard` 是 move-only RAII 借还对象，析构或 `reset()` 时归还连接；`RedisPool::release()` 是显式 release 包装。
- 借出连接前先 `ping()`；失败时关闭旧 context 并按原 `RedisConfig` 重连。
- Redis API 是阻塞 API，只能给后续 business `ThreadPool` 使用；Reactor I/O 线程仍然不能直接调用。

本次不采用/不改：

- 不实现 `OnlineStatusCache`、未读计数 cache、登录失败 limiter 或 Redis key 命名规范。
- 不接入 `ThreadPool`、`TcpServer`、`Session`、AuthService、ChatService 或任何运行时业务流程。
- 不实现 Redis Cluster、Pub/Sub、Streams、分布式锁、pipeline 或异步 Redis。
- 不修改 Docker Compose、Redis 初始化数据、MySQL schema、seed 数据或 Step 21 `ICache` 接口。

## 2026-05-11 Step 27 FriendDao / GroupDao Findings

本次进入 `Step 27：实现 FriendDao 和 GroupDao`，只实现 MySQL `friendships`、`chat_groups` 和 `group_members` DAO，不实现 Redis、业务 service、网络层运行时接入或 schema 变更。

已经确认并采用的设计：

- `FriendDao` 负责 `addFriendship()` 和 `getFriends()`。
- `GroupDao` 负责 `createGroup()`、`addGroupMember()`、`removeGroupMember()`、`getGroupMembers()` 和 `findGroupById()`。
- DAO 继续沿用 Step 25/26 的 `Status + output parameter` 风格，通过 `MySqlPool::acquire()` 借连接，通过 `PreparedStatement` 参数绑定 SQL。
- 好友关系第一版没有申请审批；`addFriendship(user_id, friend_id)` 在一个事务里写入 `(user_id, friend_id)` 和 `(friend_id, user_id)` 两个方向。
- 重复添加好友使用 `ON DUPLICATE KEY UPDATE` 的 no-op 分支保持幂等，不更新已有关系时间，也不产生重复行。
- `getFriends()` join `users` 返回完整 `UserRecord`，按 `user_id ASC` 排序，便于测试和后续接口稳定。
- `createGroup()` 在一个事务里插入 `chat_groups`，再把 owner 插入 `group_members`，避免出现“群已创建但 owner 不在成员表”的半成品。
- 第一版群权限只区分 owner 和 normal member；owner 身份由 `chat_groups.owner_id` 表达，`group_members` 表不新增 role 列。
- `addGroupMember()` 对重复成员幂等；`removeGroupMember()` 删除 normal member，并拒绝移除 owner，避免群主仍在 `chat_groups.owner_id` 但不在成员表。
- `findGroupById()` 是 `GroupDao` 的具体 DAO 查询能力，不修改 Step 21 的 `IStorage` 接口；后续 service 如果需要统一抽象再扩展。

本次不采用/不改：

- 不实现好友申请、审批、拉黑、备注或删除好友双向策略之外的业务流程。
- 不实现群管理员、禁言、公告、群资料编辑或群权限系统。
- 不接入 `ThreadPool`、`TcpServer`、`Session` 或运行时 IM service。
- 不实现 Redis 在线状态、未读数、登录失败限制或群成员缓存。
- 不修改 MySQL schema、seed 数据或 Step 21 `IStorage` 接口。

## 2026-05-11 Step 26 MessageDao / OfflineMessageDao Findings

本次进入 `Step 26：实现 MessageDao 和 OfflineMessageDao`，只实现 MySQL `messages` / `offline_messages` DAO，不实现 Redis、业务 service、网络层运行时接入或 schema 变更。

已经确认并采用的设计：

- `MessageDao` 负责 `savePrivateMessage()`、`saveGroupMessage()` 和 `getHistoryByConversation()`。
- `OfflineMessageDao` 负责 `saveOfflineMessage()`、`getOfflineMessages()` 和 `markOfflineDelivered()`。
- DAO 继续沿用 Step 25 的 `Status + output parameter` 风格，通过 `MySqlPool::acquire()` 借连接，通过 `PreparedStatement` 参数绑定 SQL。
- `savePrivateMessage()` / `saveGroupMessage()` 接收 `MessageRecord`，由调用方提供 `ConversationKey`，本 Step 不在 DAO 内定义私聊 conversation id 生成策略。
- 群聊消息如果输入 `receiver_id == 0`，DAO 落库时用 `conversation.id` 作为 `receiver_id`，保持和 Step 22 seed 数据一致。
- 历史消息按 `message_id DESC` 返回；`before_message_id` 作为游标查询更旧消息。
- 历史查询 `limit` 最大 50，超过 50 时截断为 50；`limit == 0` 作为无效参数处理。
- 离线消息查询只返回 `delivered = 0` 的 pending 记录，并 join `messages` 填充完整 `MessageRecord`。
- `savePrivateMessage()` / `saveGroupMessage()` 在同一连接上的事务里完成 insert + `LAST_INSERT_ID()` 查回，避免 DAO 返回失败但消息行已经落库的半成品。
- `markOfflineDelivered()` 对多个 message id 使用同一连接上的事务，避免部分标记成功。
- MySQL 不支持把 `START TRANSACTION` 放进 prepared statement 协议；事务控制语句通过 `MySqlConnection::executeSimple()` 走 `mysql_query()`，普通业务 SQL 仍然使用 prepared statement 参数绑定。

本次不采用/不改：

- 不实现 AuthService、ChatService、HistoryService 或 user-session 绑定。
- 不接入 `ThreadPool`、`TcpServer`、`Session` 或运行时消息路由。
- 不实现 Redis 未读计数、在线状态或登录失败限制。
- 不修改 MySQL schema、seed 数据或 Step 21 `IStorage` 接口。

## 2026-05-11 Step 25 UserDao / AuthDao Findings

本次进入 `Step 25：实现 UserDao 和 AuthDao`，只实现 users 表 DAO，不实现 MessageDao、业务服务、Redis client 或运行时 server 集成。

已经确认并采用的设计：

- `UserDao` 负责 `createUser()`、`findUserByUsername()` 和 `findUserById()`。
- `AuthDao` 当前只负责 `usernameExists()`，给后续 AuthService 的注册/登录流程使用。
- DAO 方法内部通过 `MySqlPool::acquire()` 临时借连接，使用 `PreparedStatement` 执行参数化 SQL，方法返回时由 `ConnectionGuard` 自动归还连接。
- 新增 `ErrorCode::AlreadyExists` 表达唯一约束冲突；`users.username` 重复不再作为普通 `IoError` 暴露给上层。
- `PreparedStatement::lastErrorNumber()` 暴露 `mysql_stmt_errno()`，DAO 用 MySQL duplicate key errno `1062` 结构化识别用户名唯一约束冲突。
- 查询不存在用户返回 `ErrorCode::NotFound`，而不是返回默认 `UserRecord` + ok。
- `password_hash` 和 `password_salt` 仍按 Step 22 schema 存字符串字段；本 Step 不做密码 hash 计算或验证。
- 集成测试用 `step25_` 用户名前缀并在 SetUp/TearDown 清理测试用户，避免污染 seed 用户。

本次不采用/不改：

- 不实现 `MessageDao`、`OfflineMessageDao`、好友/群组 DAO。
- 不实现 AuthService、ChatService、注册/登录业务判断或密码校验。
- 不接入 `ThreadPool`、`TcpServer`、`Session` 或运行时 user-session 绑定。
- 不实现 Redis 登录失败限制、在线状态或未读计数。
- 不修改 MySQL schema 或 seed 数据。

## 2026-05-11 Step 24 MySqlPool / ConnectionGuard Findings

本次进入 `Step 24：实现 MySqlPool 和 ConnectionGuard`，只实现 MySQL 固定连接池和 RAII 借还边界，不实现 DAO、业务服务、Redis client 或运行时 server 集成。

已经确认并采用的设计：

- `MySqlPool` 保存 `MySqlConfig`，`start()` 一次性创建 `pool_size` 个 `MySqlConnection` 并连接 MySQL。
- `ConnectionGuard` 是 move-only RAII 对象，析构或 `reset()` 时归还借出的连接，避免调用方忘记 release。
- `acquire(std::chrono::milliseconds timeout, ConnectionGuard& guard)` 使用 `std::condition_variable` 等待空闲连接；连接耗尽时返回超时错误，不永久阻塞。
- 借出连接前先 `ping()`；失败时关闭旧连接并按原 `MySqlConfig` 重连，覆盖 MySQL 连接失效/服务重启后的常见恢复路径。
- `close()` 标记连接池关闭，唤醒等待线程；空闲连接立即关闭，已借出的连接归还时关闭，不重新进入空闲队列。
- `ConnectionGuard` 持有连接池共享状态，避免 pool 对象析构后已借出 guard 归还时访问悬空 `this`。
- 连接池 API 是阻塞 API，只能给后续 business `ThreadPool` 使用；Reactor I/O 线程仍然不允许直接 acquire MySQL 连接。

本次不采用/不改：

- 不实现 `UserDao`、`AuthDao`、`MessageDao` 或任何 SQL 业务方法。
- 不接入 `TcpServer`、`Session`、`ThreadPool`、AuthService 或 ChatService。
- 不实现 Redis client / RedisPool。
- 不新增后台保活线程、动态扩缩容、连接最大寿命或 prepared statement 缓存。
- 不新增 public raw-pointer `release()`，归还连接统一通过 `ConnectionGuard::reset()` 和析构完成。

## 2026-05-11 Step 23 MySqlConnection / PreparedStatement Findings

本次进入 `Step 23：实现 MySqlConnection 和 PreparedStatement`，只封装 MySQL C API 的连接和 prepared statement，不实现连接池、DAO、业务服务或 Redis。

已经确认并采用的设计：

- `liteim_storage` 从 header-only interface target 升级为静态库，继续暴露 `IStorage` / DTO，同时新增真实 MySQL C API 封装。
- 通过 `pkg-config mysqlclient` 查找系统 MySQL client 头文件和库，避免依赖 `mysql_config` 当前指向的 Anaconda 路径。
- `MySqlConnection` 拥有一个 `MYSQL*`，负责 `connect()`、`ping()`、`close()` 和析构关闭；对象不可拷贝，可移动。
- `PreparedStatement` 拥有一个 `MYSQL_STMT*`，负责 `prepare()`、`bindInt64()`、`bindString()`、`executeUpdate()`、`executeQuery()` 和析构关闭。
- 查询结果用 `MySqlQueryResult` 输出参数承载，字段值用 `std::optional<std::string>` 表达 SQL NULL；继续沿用项目当前 `Status + output parameter` 风格，不引入 `Result<T>`。
- 所有用户输入都通过 `MYSQL_BIND` 参数绑定，不拼接 SQL 字符串。
- MySQL 连接对象不加锁，不跨线程共享；后续 Step 24 由连接池控制连接借出和归还。
- 集成测试使用 `Config::defaults()` 连接 Docker MySQL；本地 MySQL 不可用时测试 skip，避免普通无 Docker 环境直接失败。

本次不采用/不改：

- 不实现 `MySqlPool` / `ConnectionGuard`。
- 不实现 `UserDao`、`MessageDao`、好友/群组 DAO。
- 不接入 `ThreadPool`、`TcpServer`、AuthService 或 ChatService。
- 不实现 Redis client 或 RedisPool。

## 2026-05-11 Step 22 Local Dev Dependency Alignment Findings

本次在 Step 22 已完成基础上调整本地开发依赖口径，不进入 Step 23，也不实现 C++ MySQL / Redis client。

已经确认并采用的设计：

- 开发 Compose 的 MySQL 镜像从 `mysql:8.4` 改为 `mysql:8.0`，避免 MySQL Workbench 8.0 对 8.4 服务端弹兼容性警告。
- 保持宿主机 MySQL 端口 `127.0.0.1:33060`，该端口映射到容器内经典 MySQL 协议 `3306`，不是 MySQL X Protocol。
- MySQL `liteim` 用户、MySQL root 用户、Redis 认证密码统一使用本地开发密码 `6`。
- Redis 继续不初始化业务数据，但开发容器开启 `requirepass`，后续 `RedisClient` / `RedisPool` 实现必须带密码连接。
- `Config::defaults()` 同步到 Docker 开发端点：MySQL `127.0.0.1:33060`、Redis `127.0.0.1:63790`、密码 `6`，让后续 Step 23/28 默认连本地 Compose 环境。
- 已用 `docker compose down -v` 重建 LiteIM 开发数据卷，避免把 MySQL 8.4 数据目录直接降级给 MySQL 8.0 使用。

本次不采用/不改：

- 不卸载系统 MySQL、系统 Redis、Anaconda mysql client 或其他宿主机工具。
- 不修改 SQL schema / seed 数据内容。
- 不接入 DAO、连接池、Redis client 或业务服务。

## 2026-05-10 Step 22 Docker Compose / MySQL Init SQL Findings

本次进入 `Step 22：Docker Compose and MySQL init SQL`，只实现本地开发依赖环境和 MySQL schema，不实现 C++ MySQL/Redis 客户端、DAO、连接池或业务服务接入。

已经确认并采用的设计：

- 新增 `docker/docker-compose.yml`，一条 `docker compose -f docker/docker-compose.yml up -d` 启动 MySQL 8.0 系列和 Redis 7.2。
- 默认本地端口使用 `127.0.0.1:33060` 和 `127.0.0.1:63790`，避免占用系统常见 `3306` / `6379`。
- 默认本地开发密码统一为 `6`：MySQL `liteim` 用户、MySQL root 用户、Redis 认证密码一致。
- 为避免 MySQL Workbench 8.0 对 MySQL 8.4 弹出兼容性警告，开发 Compose 镜像固定为 `mysql:8.0`。
- 宿主机 `33060` 只是映射到容器内经典 MySQL `3306` 的开发端口，不表示 MySQL X Protocol。
- Compose 不固定 `container_name`，方便用 `-p` 启动临时验证环境，不和默认 dev project 冲突。
- MySQL 第一次创建数据卷时按顺序执行 `scripts/init_mysql.sql` 和 `scripts/seed_test_data.sql`。
- `init_mysql.sql` 创建 `users`、`friendships`、`chat_groups`、`group_members`、`messages`、`offline_messages`。
- SQL 字段对齐 Step 21 DTO：id 使用 `BIGINT UNSIGNED`，时间使用毫秒 `BIGINT`，`conversation_type + conversation_id + message_id` 支撑历史消息查询。
- `offline_messages` 通过 `(user_id, delivered, offline_message_id)` 索引支撑后续“查询某用户待投递离线消息”。
- seed 数据固定 id，写入 `alice`、`bob`、`dev_group`、示例消息和待投递离线消息；脚本可重复执行。
- Redis 第一版不初始化数据，但开发容器开启密码认证；后续 RedisPool / ICache 实现再定义 key。

本次不采用/不改：

- 不接入 MySQL C API、prepared statement、连接池、DAO 或 service。
- 不实现 Redis client、RedisPool、key 命名或 TTL 逻辑。
- 不改 `TcpServer`、`Session`、`ThreadPool` 或任何网络层运行时逻辑。
- 不把 MySQL / Redis 集成测试加入默认 CTest，避免普通单元测试依赖 Docker daemon。

## 2026-05-10 Step 21 Storage / Cache Interface Findings

本次进入 `Step 21：定义 IStorage / ICache 抽象`，只定义业务层面对存储和缓存的接口边界，不实现真实 MySQL / Redis。

已经确认并采用的设计：

- 新增 `liteim_storage` header-only interface target，包含 `StorageTypes.hpp` 和 `IStorage.hpp`。
- 新增 `liteim_cache` header-only interface target，包含 `CacheTypes.hpp` 和 `ICache.hpp`。
- `IStorage` 覆盖用户、好友、群组、消息、离线消息和历史查询接口。
- `ICache` 覆盖在线状态、未读计数和登录失败限制接口。
- 接口沿用当前项目简单风格：返回 `Status`，查询结果通过输出参数返回，不引入 `Result<T>`、future 或异步接口。
- `CacheTypes.hpp` 复用 `StorageTypes.hpp` 里的 `ConversationKey`，避免未读计数和历史消息使用两套会话类型。
- 本 Step 测试使用本地 `FakeStorage` / `NullCache` 证明接口可替换，不依赖真实 MySQL / Redis，也不引入 gMock 复杂度。

本次不采用/不改：

- 不实现 MySQL C API、连接池、prepared statement、DAO 或 SQL schema。
- 不实现 Redis client、RedisPool、key 字符串格式或 token bucket。
- 不接入 `TcpServer`、`ThreadPool`、AuthService、ChatService 或任何运行时业务逻辑。
- 不引入 SQLite 或 `InMemoryStorage` 主线。

## 2026-05-10 Markdown Documentation Alignment Findings

本次只做 Markdown 文档对齐，不改 C++ 源码、CMake 或测试。

已经确认并采用的文档口径：

- 当前面向读者的教程和总结必须以当前 header、实现代码和 `/home/yolo/jianli/PROJECT_MEMORY.md` 为准。
- 历史记录保留历史事实，不为了减少 `rg` 命中而大规模改写；但旧实现不能写成当前事实。
- `Acceptor::close()` 当前是 owner-loop-only；非 owner 线程直接调用会 `std::terminate()`，跨线程关闭应由上层先投递回 base loop。
- `EventLoop::isStopped()` 和 `loop_exited_` 已删除；Reactor-owned 对象通过 owner-loop-only stop / close / destroy 契约保证生命周期。
- public `Session::fd()`、`TcpServer::sendToUser()` 占位接口、`kSessionOutputHighWaterMark` 兼容别名已删除。
- `Session` 当前用 `SessionState { kNew, kStarted, kClosing, kClosed }` 表达生命周期。
- `TcpServer::sendToSession()` 对找不到 session、空 session、调用当下已关闭 session 都返回 `NotFound`。
- `Session` 读路径是 socket read 后直接喂 `FrameDecoder`；不再经过 `Session::input_buffer_`。
- `last_active_time` 只表示收到完整且合法的入站 Packet 的时间；服务端出站写不刷新活跃时间。

本次复查结论：

- `README.md`、`docs/tutorials/step12_event_loop.md`、`docs/tutorials/step13_acceptor.md`、`docs/tutorials/step20_backpressure.md`、`/home/yolo/jianli/AGENTS.md`、`/home/yolo/jianli/CLAUDE.md` 和 `/home/yolo/jianli/PROJECT_MEMORY.md` 当前口径不需要修改。
- `docs/tutorials/step09_reactor_interfaces.md`、`docs/tutorials/step14_session.md`、`docs/tutorials/step16_tcp_server.md` 和 `docs/debug_cases/net_lifecycle_review_hardening.md` 有当前读者容易误解的旧表述，需要对齐。

## 2026-05-10 Network Lifecycle/API Cleanup Findings

本次执行独立网络层 cleanup/refactor，不命名为 Step 20.1，不进入 Step 21。

已经确认并采用的设计：

- `Acceptor::close()` 改为 owner-loop-only；非 owner 线程直接调用会 `std::terminate()`，跨线程停止由上层先投递回 base loop。
- 删除 `EventLoop::isStopped()`、`loop_exited_` 和相关测试；`EventLoop` 不再为跨线程资源清理暴露 stopped 查询。
- 删除 `kSessionOutputHighWaterMark` 兼容别名，只保留 `kSessionDefaultOutputHighWaterMark`。
- 删除 public `Session::fd()`，业务身份统一使用逻辑 `Session::id()`。
- 删除 `TcpServer::sendToUser()` 占位 API；等登录态和 user-session 绑定表实现时再引入真实接口。
- `Session` 使用 `SessionState { kNew, kStarted, kClosing, kClosed }` 收敛启动/关闭状态，保留 `closed_` 作为跨线程只读关闭标记，保留 `channel_registered_` 表示 epoll 注册状态。
- `sendToSession()` 对查表失败、空 session、调用时已关闭 session 都返回 `NotFound`；返回 `ok` 只表示调用时找到未关闭 session 并完成投递，不保证异步写出一定发生。

新增/更新测试：

- `AcceptorTest.CloseFromNonOwnerThreadTerminates`
- 删除旧的跨线程 `Acceptor::close()` 成功返回测试。
- 删除 `EventLoopTest.*isStopped*` 相关测试。
- `ReactorInterfaceTest.SessionHeaderIsSelfContained` 确认 `SessionState` 存在且 public `fd()` 已删除。
- `ReactorInterfaceTest.TcpServerHeaderIsSelfContained` 不再检查 `sendToUser()`。
- `TcpServerTest.SlowClientAccumulatedSmallPacketsTriggerHighWaterMark`

本次不采用/不改：

- 不进入 Step 21 存储/缓存抽象。
- 不引入用户登录态、user-session 绑定表或真实 `sendToUser()`。
- 不改变 heartbeat 语义：只有完整、合法、成功解码的入站 Packet 刷新活动时间。
- 不改变 `Session::pendingOutputBytes()` owner-loop-only 契约。

## 2026-05-09 Optional Step 18.6 Session Input Path Simplification Findings

本次执行 Optional Step 18.6，只简化 `Session` 输入路径，不进入 Step 21，也不合并 `Session` 状态。

已经确认并采用的设计：

- `FrameDecoder` 内部已经保存未消费字节，足以处理 TCP 半包 / 粘包。
- `Session::input_buffer_` 只是在 socket read 和 `FrameDecoder::feed()` 之间做一次临时中转，当前没有独立状态职责。
- `handleRead()` 可以直接把栈上临时数组读到的字节喂给 `FrameDecoder`。
- `decoder_.feed()` 失败时继续关闭连接。
- `decoder_.feed()` 成功输出 packets 后，`Session` 按顺序逐个刷新 `last_active_time` 并触发 `message_callback_`。
- callback 过程中如果关闭了 session，后续 packet 不再继续派发。
- `closeInLoop()` 不需要再清理输入 buffer；`FrameDecoder` 跟随 `Session` 生命周期销毁。

新增测试：

- `SessionTest.SplitPacketAcrossReadsInvokesCallbackAfterSecondRead`
- `SessionTest.MalformedPacketClosesSession`

本次不采用/不改：

- 不改 output buffer。
- 不改 Step 20 输出高水位回压。
- 不改 `pendingOutputBytes()` owner-loop-only 契约。
- 不改 Packet/TLV wire format。
- 不重构 `FrameDecoder` 内部缓存结构。
- 不把 `Session` 多个状态 bool 合并为状态机。

## 2026-05-09 Step 20 Slow-Client Backpressure Findings

本次进入 `Step 20: 完善慢客户端回压保护`，只处理输出缓冲区高水位，不混入 `Session` 输入路径简化或状态机重构。

已经确认并采用的设计：

- `Session` 默认输出高水位继续保持 4MB，并新增实例级 `output_high_water_mark_`。
- `sendEncodedInLoop()` 在 append 到 `output_buffer_` 前检查阈值；超限时记录 warning 日志并关闭连接。
- 超限日志记录 `session id`、当前 pending bytes、本次 incoming bytes 和 limit，便于后续定位慢客户端或异常 push。
- `Session::setOutputHighWaterMark()` owner-loop-only，拒绝 0。
- `TcpServer::setSessionOutputHighWaterMark()` 必须在 base loop 线程、`start()` 前调用，拒绝 0。
- `TcpServer::createSessionInLoop()` 在目标 I/O loop 中创建 `Session` 后传入高水位，保持 `Session` 内部状态只在 owner loop 变更。
- `Config` 新增 `session_output_high_water_mark` 字段和 `server.output_high_water_mark_bytes` key。
- `server/main.cpp` 从 `Config` 把阈值传给 `TcpServer`。
- `pendingOutputBytes()` 仍只允许 owner-loop 查询；外部线程如果要观测 pending bytes，后续必须投递到连接所属 loop。

新增/更新测试：

- `ConfigTest.ZeroHighWaterMarkFails`
- `SessionTest.DefaultOutputHighWaterMarkIsFourMegabytes`
- `SessionTest.RejectsZeroOutputHighWaterMark`
- `SessionTest.CloseWhenPendingOutputExceedsConfiguredHighWaterMark`
- `TcpServerTest.RejectsZeroSessionOutputHighWaterMark`
- `TcpServerTest.SessionOutputHighWaterMarkMustBeSetBeforeStart`
- `TcpServerTest.NormalClientDoesNotTriggerConfiguredHighWaterMark`
- `TcpServerTest.SlowClientIsClosedWhenOutputExceedsHighWaterMark`
- `TcpServerTest.ClosedSlowClientIsRemovedFromSessionTable`
- Session / TcpServer header static assertions for new API.

本次不采用/不改：

- 不暂停读。
- 不做低水位恢复。
- 不做消息优先级丢弃。
- 不做群聊广播优化。
- 不删除 `Session::input_buffer_`。
- 不把 `Session` 多个状态 bool 合并为状态机。
- 不改变 P0 修复后的 heartbeat 语义：服务端出站写不刷新 `last_active_time`。

## 2026-05-09 Documentation Boundary Correction Findings

本次只做文档职责边界纠正，不改 C++ 源码、CMake 或测试代码。

已经确认并采用的边界：

- `PROJECT_MEMORY.md` 收敛为总思路设计和长期路线设计，保留 Step 目标、边界、测试要求和计划 commit message。
- Step 完成状态、完成验证命令、实际 commit hash、活动下一步和过程发现统一留在 `docs/process/task_plan.md`、`docs/process/findings.md`、`docs/process/progress.md`。
- `AGENTS.md` / `CLAUDE.md` 保持同一职责：读取顺序、工程约束、agent 行为规则，不写当前进度。
- `README.md` 保持 GitHub 对外介绍职责，只描述项目能力、模块、架构、构建运行、目录布局和路线概览。
- `planning-with-files` 三件套是之后恢复进度和过程上下文的来源。

## 2026-05-09 Session Activity Semantics Fix Findings

本次只修 P0：`last_active_time` 只能表达客户端入站完整 Packet 活跃，不能被服务端出站写刷新。

已经确认并采用的设计：

- `Session::feedInputBuffer()` 成功解出完整入站 Packet 后继续调用 `updateLastActiveTime()`。
- `Session::sendEncodedInLoop()` 只是把服务端待发送数据加入 output buffer，不刷新活跃时间。
- `Session::handleWrite()` 只是把 output buffer 写到 fd，不刷新活跃时间。
- `TcpServer` heartbeat 仍按 `now - session->lastActiveTimeMilliseconds() >= timeout` 判断 idle session。

新增回归测试：

- `TcpServerTest.ServerWritesDoNotRefreshHeartbeatActivity`：客户端只发一次入站 Packet 后保持沉默，测试线程持续 `sendToSession()` 主动推送；修复前 session 不超时，修复后即使服务端持续写也会被 heartbeat 关闭。

本次不采用/不改：

- 不进入 Step 20 慢客户端回压完善。
- 不配置 high water mark。
- 不删除 `Session::input_buffer_`。
- 不重构 `Session` 状态机。

## 2026-05-09 Step 19 Signalfd Graceful Shutdown Findings

本次进入 `Step 19: signalfd graceful shutdown`，目标是在不引入传统 signal handler、不放松 owner-loop 生命周期约束的前提下，让 `liteim_server` 收到 `SIGINT` / `SIGTERM` 后优雅退出。

已经确认并采用的设计：

- 新增 `SignalWatcher`，头文件放在 `include/liteim/net/`，实现放在 `src/net/`，因为它直接依赖 `EventLoop`、`Channel` 和 `UniqueFd`。
- `SignalWatcher` 用 `pthread_sigmask(SIG_BLOCK, ...)` 阻塞指定信号，再用 `signalfd(SFD_NONBLOCK | SFD_CLOEXEC)` 创建 signal fd，并通过 `Channel` 注册到 owner `EventLoop`。
- signal callback 只在 owner loop 线程执行；`server/main.cpp` 把它绑定为 `server.stop(); loop.quit();`。
- `SignalWatcher` 在 `server.start()` 前启动，让后续 I/O 线程继承被阻塞的 `SIGINT` / `SIGTERM` mask，避免信号被子线程按默认动作处理。
- `SignalWatcher::stop()` 和析构同样 owner-loop-only；非 owner 线程直接 `stop()` 会 `std::terminate()`，不引入 `queueInLoop([this])` 异步清理。
- 真实 `liteim_server` 的 SIGTERM 退出通过 CTest 脚本验证，要求退出码为 0。

新增测试：

- `ReactorInterfaceTest.SignalWatcherHeaderIsSelfContained`
- `SignalWatcherTest.SignalfdDispatchesSignalInOwnerLoop`
- `SignalWatcherTest.StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis`
- `LiteIMServerSignalTest.TerminatesOnSigterm`

本次不采用/不改：

- 不使用传统 `std::signal()` / `sigaction()` handler 做资源清理。
- 不接入业务 `ThreadPool`、MySQL 或 Redis 的退出编排。
- 不删除 `Session::input_buffer_`。
- 不合并 `Session` 多 bool 状态。
- 不把 `TimerManager` 改成动态 rearm 的通用 `TimerQueue`。

## 2026-05-09 PROJECT_MEMORY Markdown Alignment Findings

本次按新版 `/home/yolo/jianli/PROJECT_MEMORY.md` 同步其余 Markdown，不改代码。

已经确认并采用的文档边界：

- `/home/yolo/jianli/PROJECT_MEMORY.md` 是唯一总设计文件；不再保留第二份总设计文件。
- `/home/yolo/jianli/AGENTS.md` 是未来 agent 的紧凑约束文件，需要显式写入 Step 18.5 已完成、Step 19 默认下一步、owner-loop 生命周期、禁止 `queueInLoop([this])` 异步清理、`UniqueFd` fd 所有权交接、`pendingOutputBytes()` owner-loop-only 等硬规则。
- `/home/yolo/jianli/CLAUDE.md` 也需要同步同一口径，避免继续要求维护 broad `LiteIM/docs/` 或使用过期行数。
- `docs/debug_cases/` 是保留的内部复盘目录；其中 `net_lifecycle_review_hardening.md` 应改为中文，方便后续教学和面试复盘。
- `docs/architecture.md`、`docs/project_layout.md`、`docs/roadmap.md` 不恢复；`docs/tutorials/README.md` 不恢复。
- README 继续作为 GitHub 对外介绍，不加入 `Current Status` / `当前状态` 这种过程状态标题。

本次没有修改 C++ 源码、CMake 或测试代码。

## 2026-05-09 Muduo-style Lifecycle Hardening Findings

本次处理外部 review 中仍能在当前代码确认的生命周期和所有权隐患，不启动 Step 19，不接入 MySQL / Redis，也不删除 `Session::input_buffer_` 或重做状态机。

已经确认并采用的设计：

- `TcpServer` 是 base loop 拥有的 Reactor 对象，`stop()` 和析构必须在 base loop 线程执行；非 owner 线程直接调用 `stop()` 会 `std::terminate()`，不再捕获裸 `this` 异步排队。
- `TimerManager` 同样是 owner loop 内部对象，`stop()` 和析构必须在 owner loop 线程执行；非 owner 线程直接调用 `stop()` 会 `std::terminate()`。
- `Session` 构造函数改为 `Session(EventLoop*, UniqueFd, id)`，fd 所有权从 `Acceptor` 到 `TcpServer` 再到 `Session` 全程由 RAII 类型表达，不再依赖裸 fd `release()` 串联。
- `Session::pendingOutputBytes()` 只允许 owner loop 线程调用，并去掉 `noexcept`；它读取的是 `output_buffer_`，不能跨线程直接观察。
- `server/main.cpp` 不再是 scaffold，当前会创建 `EventLoop`、启动真实 `TcpServer` echo server 并进入 `loop()`；`signalfd` graceful shutdown 仍留给 Step 19。
- 当前 `TimerManager` 定位为 heartbeat fixed tick timer，不是完整 muduo `TimerQueue`；动态 rearm 到最近 expiration 可作为后续独立重构。

新增/更新测试：

- `ReactorInterfaceTest.SessionHeaderIsSelfContained` 校验 `Session` 只接收 `UniqueFd` 构造，且 `pendingOutputBytes()` 不是 `noexcept`。
- `SessionTest.PendingOutputBytesRequiresOwnerLoopThread`
- `TcpServerTest.StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis`
- `TimerManagerTest.StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis`

本次不采用/不改：

- 不删除 `Session::input_buffer_`。
- 不把 `Session` 的多个状态 bool 合并成 enum 状态机。
- 不把 `TimerManager` 改成动态 rearm 的通用 `TimerQueue`。
- 不实现 signalfd graceful shutdown。
- 不接入 MySQL / Redis / 业务服务层。

## 2026-05-09 Step 18 TimerManager Findings

本次进入 `Step 18: implement TimerManager + timerfd heartbeat timeout`，目标是在不实现登录心跳包、`HeartbeatService`、MySQL、Redis 或 signalfd 的前提下，先补齐 timerfd 驱动的服务端 idle session 清理能力。

已经确认并采用的设计：

- 新增 timer 模块，头文件放在 `include/liteim/timer/`，实现放在 `src/timer/`；当前 `TimerManager` 源码编进 `liteim_net`，避免和 `EventLoop` / `Channel` 形成库循环依赖。
- `TimerHeap` 使用小根堆保存 one-shot timer，返回自增 `TimerId`，`cancel()` 只标记取消，真正删除发生在 `popExpired()` / `nextExpirationMilliseconds()` 清理堆顶时，避免线性扫描。
- `TimerManager` 绑定一个 `EventLoop*`，用 `timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)` 创建 fd，再用 `Channel` 注册读事件。
- 第一版 `TimerManager` 使用固定 tick interval，生产默认 5 秒；测试允许用更短 interval 缩短运行时间。
- `TimerManager` 的回调只在 owner loop 线程执行，避免 timer callback 跨线程直接操作 `Session`。
- `TcpServer` 第一版只在 base loop 创建一个 `TimerManager`，符合 Step 18 “注册到主 EventLoop 或每个 I/O EventLoop” 的边界，同时避免 timer 生命周期和子 I/O loop join 顺序变复杂。
- 每轮 timer tick 扫描线程安全 session 快照；如果 `now - last_active_time >= 90s`，调用 `session->close()`，实际关闭仍回到 Session 所属 loop。
- `Session` 收到完整入站 `Packet` 后刷新 `last_active_time`，让应用层心跳包或任意有效业务包都能续期连接；服务端出站写不能续期。

本次不采用/不改：

- 不新增协议层 heartbeat message。
- 不实现 `HeartbeatService`、用户在线状态、Redis TTL 或登录态绑定。
- 不把 timer callback 投递到业务线程池。
- 不实现复杂可变 tick、cron、重复 timer API、优先级、跨线程取消同步等待或 signalfd 退出。

## 2026-05-09 Network Review Hardening Findings

本次处理 Step 17 后的外部 review，不启动 Step 18，只修复当前网络/并发层中已能证实的问题。

已经确认并采用的设计：

- `EventLoopThread::stop()` 自线程调用只请求 `EventLoop::quit()` 并返回，不 detach 自己，不清空 `loop_` / `running_`；状态清理放到 `threadFunc()` 退出路径。
- `EventLoopThread` 使用保存的 `thread_id_` 判断 self-stop，并用 `join_started_` 避免多个外部 stop 并发 join 同一个 `std::thread`。
- `Session` 输出缓冲区设置 4MB 高水位；超过上限直接关闭连接，这是第一版慢客户端保护。
- `TcpServer` 的 session 表 key 改为自增逻辑 id，fd 只用于 socket I/O，避免 fd 复用误删新 session。
- `Session` 暴露 `id()`，`TcpServer::sendToSession()` 改为接收 `std::uint64_t session_id`。
- `Channel::handleEvent()` 遇到 `EPOLLERR | EPOLLIN` 时先跑 error callback，再继续 read callback，避免吞掉 socket 缓冲中的剩余数据。
- `Acceptor::close()` 保留跨线程 close，但在 close task 排队后 loop 先退出的竞态下会检测 `isStopped()` 并走 fallback，避免 `future.wait()` 永久阻塞。

新增/更新测试：

- `ChannelTest.ErrorWithReadableEventInvokesErrorThenRead`
- `AcceptorTest.CloseFromOtherThreadWhileLoopExitsWithQueuedCloseDoesNotBlock`
- `EventLoopThreadTest.OwnerStopWaitsAfterStopIsRequestedInsideLoop`
- `SessionTest.CloseWhenPendingOutputExceedsHighWaterMark`
- `TcpServerTest.SendToSessionFromOtherThreadDeliversPacket`
- `ReactorInterfaceTest.TcpServerHeaderIsSelfContained`

本次不采用/不改：

- 不采纳 “`Acceptor::close()` 只能 owner loop 调用” 的直接重写建议；本次保留当前已经测试过的跨线程 close 契约，只修补 queued close 永久等待竞态。
- 不采纳 `ThreadPool::stop()` swap-and-join 替换当前 `stop_mutex_` 的建议；当前实现已用测试覆盖并发外部 stop，worker 内部析构自身对象不是支持场景。
- 不做大范围变量合并或 `FrameDecoder` 重构，避免把风格重构和真实 bugfix 混在一起。

## 2026-05-08 Step 17 Business ThreadPool Findings

本次进入 `Step 17: implement business ThreadPool`，目标是在不接入 MySQL、Redis、登录态或 MessageRouter 的前提下，先补齐业务线程池基础设施。

已经确认并采用的设计：

- 新增 `liteim_concurrency` 模块，头文件放在 `include/liteim/concurrency/`，实现放在 `src/concurrency/`。
- `ThreadPool` 使用固定 worker 数，构造时确定，不做动态扩缩容、优先级队列或 work stealing。
- `submit()` 接收 `std::function<void()>`，返回 `Status`，不返回 `future`，避免 I/O 线程同步等待业务结果。
- `mutex + condition_variable + deque<Task>` 作为第一版任务队列。
- `start()` 拒绝 0 worker 和重复启动。
- `ThreadPool` 内部使用单一 `running_` 状态表达是否运行和是否接受新任务，`start()` 置 true，`stop()` 置 false。
- `submit()` 拒绝空任务和未运行的线程池。
- `stop()` 停止接收新任务，唤醒所有 worker，并等待已经入队的任务执行完再退出。
- worker 内部调用 `stop()` 时不能 join 自己；当前实现通过 worker-local 标记识别 self-stop，只发出停止请求并返回，直到 owner 线程后续调用 `stop()` 或析构完成 join 和清理。
- 外部多线程并发调用 `stop()` 时通过 `stop_mutex_` 串行化 join/cleanup，避免多个线程同时 join 同一个 `std::thread`。
- `pendingTaskCount()` 只统计仍在队列中等待 worker 取走的任务，不统计正在执行的任务。
- 单个任务异常会被 worker 捕获，避免一个业务任务杀死 worker 线程；业务错误后续应转换为 `Status` 或响应 Packet。

新增测试：

- `ConcurrencyInterfaceTest.ThreadPoolHeaderIsSelfContained`
- `ThreadPoolTest.StartRejectsZeroWorkers`
- `ThreadPoolTest.SubmitExecutesTask`
- `ThreadPoolTest.MultipleWorkersRunConcurrently`
- `ThreadPoolTest.StopRejectsNewTasks`
- `ThreadPoolTest.StopCalledFromWorkerRequiresOwnerCleanupBeforeRestart`
- `ThreadPoolTest.ConcurrentStopCallsAreSerialized`
- `ThreadPoolTest.DestructorWaitsForQueuedTasks`
- `ThreadPoolTest.PendingTaskCountTracksQueuedTasks`

本次不采用/不改：

- 不接入 `TcpServer` 的 message callback。
- 不实现 MySQL、Redis、登录、注册、聊天、历史消息或 MessageRouter。
- 不让业务线程直接修改 `Session`；后续业务响应仍必须回到连接所属 I/O loop。
- 不实现 `future`、cancel、deadline、任务优先级、动态扩缩容或 work stealing。

## 2026-05-08 Step 16 TcpServer Findings

本次进入 `Step 16: implement TcpServer multi-Reactor version`，目标是在不引入业务线程池、MySQL、Redis 或登录态的前提下，把 `Acceptor`、`EventLoopThreadPool` 和 `Session` 串成第一个多 Reactor echo server。

已经确认并采用的设计：

- `TcpServer` 是网络层协调器，不替代 `Acceptor`、`Session` 或 `EventLoopThreadPool` 的职责。
- base `EventLoop` 持有 `Acceptor`，只负责 listen fd 事件和 accept。
- `Acceptor` 通过 `NewConnectionCallback(UniqueFd, peer)` 把 accepted fd 所有权 move 给 `TcpServer`。
- `TcpServer::handleNewConnection()` 在 base loop 中选择一个 I/O loop，然后通过 `queueInLoop()` 把 `Session` 创建投递到目标 loop。
- `Session` 在所属 I/O loop 中创建和启动，避免跨 loop 注册 `Channel`。
- `sessions_` 使用 mutex 保护，覆盖 I/O loop close callback 删除、外部线程 `sendToSession()` 查询和测试诊断 `sessionCount()`。
- 未设置业务 callback 时，`TcpServer` 默认 echo 收到的 `Packet`，用于验证网络底座。
- `sendToSession()` 可以从其他线程调用，但真实 socket 写入仍由 `Session::sendPacket()` 投递回 session 所属 I/O loop。
- 当时曾保留用户发送占位接口；2026-05-10 cleanup 已删除该占位 API，等 user-session 绑定表实现时再引入真实用户路由。

新增测试：

- `ReactorInterfaceTest.TcpServerHeaderIsSelfContained`
- `TcpServerTest.EchoesPacketToClient`
- `TcpServerTest.DistributesConnectionsAcrossIoLoops`
- `TcpServerTest.RemovesSessionAfterClientDisconnects`
- `TcpServerTest.SendToSessionFromOtherThreadDeliversPacket`
- `TcpServerTest.SendToUnknownUserReturnsNotFound`

本次不采用/不改：

- 不实现 business `ThreadPool`，它属于 Step 17。
- 不实现登录态、用户路由、`MessageRouter`、MySQL、Redis、心跳或慢客户端高水位策略。
- 不把 I/O loop 线程用于数据库或缓存阻塞调用。

## 2026-05-08 Pre-Step 16 Code Cleanup Findings

本次清理在 Step 16 `TcpServer` 之前完成，不实现 `TcpServer`，只处理会影响 Step 16 ownership / one-loop-per-thread 边界的代码卫生问题。

已经确认并采用的设计：

- 新增 `include/liteim/protocol/ByteOrder.hpp`，统一提供 `appendUint16BE()`、`appendUint32BE()`、`appendUint64BE()`、`readUint16BE()`、`readUint32BE()`、`readUint64BE()`。
- `Packet.cpp` 和 `TlvCodec.cpp` 删除各自重复的大端读写 helper，统一复用 `ByteOrder.hpp`；协议 wire format 不变。
- `Epoller::owner_loop_` 不再闲置。`updateChannel()` / `removeChannel()` 会拒绝 `Channel::ownerLoop()` 与 `owner_loop_` 不一致的 channel，维护 one-loop-per-thread 注册边界。
- `Acceptor::NewConnectionCallback` 从 `std::function<void(int, const sockaddr_in&)>` 改为 `std::function<void(UniqueFd, const sockaddr_in&)>`，accepted fd 所有权通过 move-only RAII 类型表达。
- `Acceptor::handleRead()` 不再在 callback 成功返回后手动 `release()`，而是把 `UniqueFd` move 给 callback；没有 callback 或 callback 抛异常时，`UniqueFd` 自动关闭 accepted fd。
- `Acceptor::listening_` 删除，`listening()` 直接由 `listen_fd_` 是否有效推导，避免重复状态。
- `tests/net/acceptor_test.cpp` 和 `tests/net/socket_util_test.cpp` 删除测试专用 `FdGuard`，改用生产 `liteim::UniqueFd`。
- 生产源码中过长的教学注释已移到教程语境，源码只保留必要契约说明。

新增/更新测试：

- `ByteOrderTest.AppendsUnsignedIntegersAsBigEndianBytes`
- `ByteOrderTest.ReadsUnsignedIntegersFromBigEndianBytes`
- `EpollerTest.RejectsChannelOwnedByDifferentEventLoop`
- `ReactorInterfaceTest.AcceptorHeaderIsSelfContained` 更新为 `UniqueFd` callback 签名。
- `AcceptorTest` callback 用例全部改为接收 `UniqueFd`。

本次不采用/不改：

- 不删除 `Config::defaults()`，保留教学语义。
- 不改短期诊断有用的 `EventLoopThreadPool::loops()` 和 `EventLoopThread::running()`，等 Step 16 `TcpServer` 完成后再评估。
- 不改 `MessageType` 分类函数实现，避免本轮混入无关协议语义改动。
- 不把 `Byte` / `Bytes` 换成 `std::byte`。

## 2026-05-08 Pre-Step 15 Byte/Bytes API Cleanup Findings

进入 Step 15 之前，先收口原始字节类型，避免后续 `EventLoopThreadPool` / `TcpServer` 继续扩散 `std::vector<char>`、`std::vector<std::uint8_t>` 和 `std::string_view` 公共接口。

已经确认并采用的设计：

- 新增 `include/liteim/base/Types.hpp`，只放轻量公共别名：`using Byte = std::uint8_t;` 和 `using Bytes = std::vector<Byte>;`。
- 协议和网络层的 wire data 统一使用 `Byte` / `Bytes`：`Packet::body`、`encodePacket()` output、`parseHeader()` input、`TlvValue`、`parseTlvMap()` input、`FrameDecoder` internal buffer、`Buffer` storage、`Session` read/write path 和相关测试都已切换。
- `Buffer` 公共接口不再暴露 `char*`、`std::uint8_t*` 重载和 `std::string_view`；保留 `append(const Byte*, len)`、`append(const Bytes&)`、`append(const std::string&)`。
- `Buffer::ensureWritableBytes()` 改为私有内部细节，调用方只通过 `append()` 触发空间整理和扩容。
- `ErrorCode::toString()` 改为 `const char* noexcept`，`Logger::parseLogLevel()` 改为接收 `const std::string&`，避免基础公共接口继续引入 `std::string_view`。

本次清理不改变协议二进制格式，不改变 Packet/TLV 网络字节序，也不启动 Step 15 线程池实现。

## 2026-05-08 Step 15 EventLoopThreadPool Findings

本次进入 `Step 15: implement EventLoopThread and EventLoopThreadPool`，目标是建立 one-loop-per-thread 的子 Reactor 线程基础，不实现 `TcpServer`、连接分发、业务线程池、MySQL 或 Redis。

已经确认并采用的设计：

- `EventLoopThread` 在工作线程内部构造局部 `EventLoop`，保证 `EventLoop::thread_id_` 绑定到真正的 I/O 线程；`startLoop()` 等待 loop 初始化完成后返回观察指针。
- `EventLoopThread::stop()` 在持有自身 mutex 时调用 `loop_->quit()`，避免 loop 线程刚退出时裸指针失效；随后 join 线程，析构时自动 stop。
- `EventLoopThreadPool` 持有多个 `EventLoopThread`，`start()` 启动指定数量的子 loops，`getNextLoop()` 用 round-robin 返回下一个子 loop。
- `thread_count == 0` 时，线程池不创建子线程，`getNextLoop()` 返回 base loop，作为后续 `TcpServer` 的单 Reactor fallback。
- `EventLoopThreadPool::getNextLoop()` 要求先 `start()`，避免误把未启动的多 Reactor 配置静默退化成 base loop。

新增测试：

- `ReactorInterfaceTest.EventLoopThreadHeaderIsSelfContained`
- `ReactorInterfaceTest.EventLoopThreadPoolHeaderIsSelfContained`
- `EventLoopThreadTest.StartLoopCreatesLoopOnWorkerThread`
- `EventLoopThreadTest.StopWithoutStartIsNoop`
- `EventLoopThreadTest.DestructorStopsRunningLoop`
- `EventLoopThreadPoolTest.StartCreatesRequestedNumberOfLoops`
- `EventLoopThreadPoolTest.GetNextLoopUsesRoundRobinOrder`
- `EventLoopThreadPoolTest.ZeroThreadsReturnsBaseLoop`
- `EventLoopThreadPoolTest.ChildLoopsRunTasksOnDistinctThreads`

TDD RED 已确认：新增测试后构建失败于 `fatal error: liteim/net/EventLoopThread.hpp: No such file or directory`，证明测试覆盖了 Step 15 缺失接口。

## 2026-05-07 Step 14 Session Findings

本次进入 Step 14，实现单个已连接 fd 的 `Session` 生命周期，不启动 Step 15 多 Reactor 线程池，也不实现 `TcpServer`。

已经确认并采用的设计：

- `Session` 是连接 owner：持有 fd、`Channel`、输入 `Buffer`、输出 `Buffer` 和 `FrameDecoder`。
- `Session` 必须由 `std::shared_ptr` 管理，并继承 `std::enable_shared_from_this`；`start()` 时用 `Channel::tie()` 把 owner 生命周期锁到事件分发期间。
- `handleRead()` 循环 `read()` 到 `EAGAIN` / `EWOULDBLOCK`，把读到的字节追加到输入 `Buffer`，再交给 `FrameDecoder` 产出完整 `Packet`。
- 半包不会触发 message callback；粘包会按顺序触发多次 message callback。
- `sendPacket()` 先调用 `encodePacket()`；跨线程调用时只把发送任务投递回所属 `EventLoop`，不在调用线程直接操作 fd 或 output buffer。
- `handleWrite()` 只在 loop 线程中写 output buffer，写完后关闭写兴趣，未写完的字节保留在 output buffer。
- `closeInLoop()` 会先从 `Epoller` 删除 `Channel` 并关闭 fd；`Channel` 对象本身延迟到当前事件回调栈帧之后释放，避免在 `Channel::handleEvent()` 运行期间销毁当前 `Channel`。
- 当前 Step 只记录 `pendingOutputBytes()`；慢客户端高水位回压策略留给后续专门 Step。

新增回归测试：

- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `SessionTest.CompletePacketInvokesMessageCallback`
- `SessionTest.HalfPacketDoesNotInvokeMessageCallback`
- `SessionTest.StickyPacketsInvokeCallbackForEachPacket`
- `SessionTest.PeerCloseInvokesCloseCallback`
- `SessionTest.SendPacketFromOtherThreadDeliversEncodedPacket`
- `SessionTest.LargePacketLeavesPendingOutputWhenPeerDoesNotRead`
- `SessionTest.LastActiveTimeIsInitialized`

## 2026-05-07 Step 13 Hardening Round 3 Findings

本次继续处理 Step 13 hardening round 2 后的代码审阅反馈，不启动 Step 14 `Session`。

已经确认并修复的点：

- `EventLoop::isStopped()` 不能用 `!looping_` 或 `quit_ && !looping_` 推断。`looping_ == false` 同时覆盖“刚构造但还没启动”和“已经退出”两种状态，`quit()` 也可能在第一次 `loop()` 前被调用。修复后新增 `loop_exited_`，只有 `loop()` 真正进入并返回后 `isStopped()` 才返回 true。
- `quit()` 早于第一次 `loop()` 时，`loop()` 仍必须先执行已经排队的 pending task，然后再退出；否则跨线程 close 已经排队但 loop 一启动就退出，会让等待方永久阻塞。
- `Acceptor::close()` 在 loop 尚未启动但还会启动时，必须继续投递到 owner loop 并等待清理，不能直接在调用线程释放 listen `Channel`，否则 `Epoller::channels_` 会保留旧 fd 到旧 `Channel*` 的映射。
- `Acceptor` 的 `handleAcceptError()` / fd 用尽 helper 保持 `noexcept`，但 warn 日志必须走 no-throw wrapper，避免 `spdlog` 在异常 fd 状态下抛出后触发 `std::terminate()`。
- `Channel` 不复制 callback 后，必须把契约写清楚：callback 不得在执行中销毁当前 `Channel` 或重置正在执行的 callback；如果 owner 可能在 callback 中释放，必须先调用 `tie()`。

新增回归测试：

- `EventLoopTest.IsStoppedIsFalseBeforeLoopEverStarts`
- `EventLoopTest.IsStoppedIsFalseAfterQuitBeforeLoopEverStarts`
- `EventLoopTest.LoopRunsQueuedTaskWhenQuitWasRequestedBeforeStart`
- `AcceptorTest.CloseFromOtherThreadBeforeLoopStartsStillRemovesChannel`

## 2026-05-07 Step 13 Hardening Round 2 Findings

本次继续处理 Step 13 后的外部审阅反馈，不启动 Step 14 `Session`。

已经确认并修复的点：

- `Logger::get()` 不能改变全局日志级别。修复后只有 `Logger::init()` 和 `Logger::setLevel()` 会显式设置级别，`get()` 只负责保证 logger 存在。
- `Epoller` 构造失败不能留下半初始化对象。修复后 `epoll_create1(EPOLL_CLOEXEC)` 失败会直接抛出 `std::runtime_error`。
- `EventLoop` 的 wakeup fd 改由 `UniqueFd` 持有，构造或注册 wakeup channel 过程中出现异常时也有明确 fd 清理边界。
- `EventLoop::loop()` 用 RAII guard 管理 `looping_`，异常路径也会复位；活跃 `Channel` 回调和 pending task 的异常会被捕获并写日志，单个业务回调不会直接杀死 loop 线程。
- `EventLoop::isStopped()` 表示 `loop()` 已经进入并返回，供 `Acceptor::close()` 在 loop 已退出后选择 fallback 清理路径。
- `Acceptor::handleRead()` 对 `ECONNABORTED`、`EMFILE`、`ENFILE` 和未知 errno 做区分处理；fd 用尽时使用 idle fd 套路拒绝一个 pending connection，避免 LT 模式下反复 `EPOLLIN` 触发 busy loop。
- `Acceptor::close()` 跨线程调用时，如果 loop 仍运行则投递回 owner loop；如果 loop 已停止，则不再 `future.wait()`，直接释放 `Channel`、listen fd 和 idle fd。
- `Channel::handleEvent()` 不再拆出单点 private helper，也不再每次事件复制四个 `std::function`；owner 生命周期由 `Channel::tie()` 的 `weak_ptr` / `shared_ptr` 保证，事件分发只保留 `revents_` 快照。未 `tie()` 的轻量 `Channel` 必须保证 callback 不会销毁自身或重置当前 callback。

已经评估但未在本轮采用的点：

- 不把 `EventLoop::updateChannel()` / `removeChannel()` 改为返回 `Status`。当前 `Channel::update()` 调用链依赖 void 接口，系统调用失败属于本地编程/系统错误，继续以异常暴露即可；把它改成 `Status` 会扩大接口迁移范围，不是本轮 hardening 的必要条件。
- `FrameDecoder::feed()` 的 `vector::erase()` 仍留到 Step 14 `Session` 输入面基于 `Buffer::peek()` / `retrieve()` 接入时处理。

## 2026-05-07 Step 13 Review Hardening Findings

本次处理的是 Step 13 完成后的外部评审反馈核对，不启动 Step 14 `Session`。

已经确认需要立即修复的点：

- `Acceptor::close()` 的非 loop 线程调用边界有真实风险：当前实现会在非 loop 线程 reset `listen_channel_` 并关闭 listen fd，但不会调用 `EventLoop::removeChannel()`，`Epoller` 的 `fd -> Channel*` 映射可能保留悬空指针。
- `Acceptor::handleRead()` 把 accepted fd 以裸 `int` 交给 callback；如果 callback 在接管所有权前抛异常，accepted fd 可能泄漏。需要引入轻量 RAII fd 包装或等价的局部保护。
- `Channel` 目前没有 `tie()` 机制。虽然当前 Step 13 还没有 `Session`，但后续 `Session` / `TcpConnection` 必须能用 `weak_ptr` 防止事件回调期间 owner 已销毁或在回调中被销毁。

已经确认暂不直接改的点：

- `EventLoop` 的异常策略已经在 Step 13 hardening round 2 收口：活跃 `Channel` 回调和 pending task 的异常会被捕获并记录，不再留到 Step 14。
- `FrameDecoder` 目前内部 `vector::erase()` 功能正确；Step 14 的 `Session` 应优先基于 `Buffer::peek()` / `retrieve()` 组织输入字节，避免长期在高吞吐路径依赖反复搬移。
- 公开 README 不应链接仓库外的 `../PROJECT_MEMORY.md`；应改为仓库内 `docs/roadmap.md`，但本地规划文件仍保留对权威总方案的说明，供开发流程使用。

## Step 0 清理结论

本次 Step 0 的目的不是实现功能，而是清掉旧路线，留下最小起点。

已经删除的旧内容类型：

- 旧 `include/`、`src/`、`server/` 实现。
- 旧 `tests/` 单元测试。
- 旧 `docs/tutorials/step01-step15` 教程。
- 旧 `docs/` 文档。
- 旧 `sql/` SQLite / 初始化脚本目录。
- 旧 `client_qt/` 临时结构。
- 旧 `build/` 构建产物。
- 空的 `.codex` 临时文件。
- 未来 Step 才会用到的空目录和 `.gitkeep`。

当前保留内容：

- `.gitignore`。
- `LICENSE`。
- 空 CMake 骨架。
- README / task_plan / findings / progress。
- `docs/architecture.md` 和 `docs/project_layout.md`。
- `docs/tutorials/README.md` 和 `docs/tutorials/step00_reset.md`。

## 关于 .gitkeep

`.gitkeep` 不是 Git 必需文件。Git 不跟踪空目录，所以 `.gitkeep` 只是社区常用占位文件。

本项目不保留 `.gitkeep`，原因是：

- 用户希望项目从 Step 0 开始逐步建立。
- 空目录提前出现会让教程边界不清楚。
- 每个目录应该在真正需要它的 Step 中创建，并在教程里解释为什么需要。

## 核心架构结论

- 先搭高性能网络底座，再做业务、MySQL、Redis、Qt 和 Agent 接入。
- 不再走旧的单 Reactor 业务 baseline。
- 最终 LiteIM 不使用 SQLite。
- `InMemoryStorage` 不能作为主线存储实现；后续最多作为测试 double / mock。
- 服务端使用 C++17、CMake、Linux nonblocking socket、epoll LT、eventfd、timerfd、signalfd 和自定义 TLV 协议。
- 使用 one-loop-per-thread：每个 I/O 线程拥有一个 `EventLoop`。
- 主 Reactor 负责 `accept`，子 Reactor 负责连接读写事件。
- MySQL / Redis 阻塞调用必须进入业务 `ThreadPool`，不能在 I/O 线程执行。
- 业务线程不能直接修改 `Session`；响应必须通过 `EventLoop::queueInLoop()` 或 `EventLoop::runInLoop()` 投递回连接所属 I/O 线程。
- `Session` / `TcpConnection` 生命周期使用 `shared_ptr` / `weak_ptr` 管理，避免跨线程长期持有裸指针。
- 慢客户端保护必须显式实现，通过输出缓冲区高水位触发关闭或限流。

## Step 1 约束

Step 1 只做第一层工程初始化：

- 只创建 Step 1 真正需要的 `server/` 和 `tests/` 目录。
- 添加真正的 CMake target：`liteim_server` 和 `liteim_tests`。
- 根 CMake 用 `FetchContent` 接入 GoogleTest v1.14.0。
- `tests/CMakeLists.txt` 链接 `GTest::gtest_main` 并使用 `gtest_discover_tests`。
- 添加 `server/main.cpp`。
- 添加最小 GoogleTest 用例 `TEST(SmokeTest, GoogleTestWorks)`。
- 保持 `include/`、`src/`、Qt、MySQL、Redis、协议、Reactor 都不提前实现。

Step 1 不允许恢复旧 Step 1-15 文件。旧代码里的知识可以参考，但文件本身不作为新路线起点。

## Step 1 实现结论

- 根 `CMakeLists.txt` 只接入 `server/` 和 `tests/`。
- `server/CMakeLists.txt` 生成 `liteim_server`。
- `tests/CMakeLists.txt` 生成 `liteim_tests`，链接 `GTest::gtest_main`，并通过 `gtest_discover_tests` 注册 CTest。
- `server/main.cpp` 只打印启动信息。
- `tests/test_main.cpp` 使用 `TEST(SmokeTest, GoogleTestWorks)` 验证 C++17 编译环境和 GoogleTest/CTest 链路。
- Step 1 没有创建 `.gitkeep`，也没有提前创建未来目录。

## Step 2 约束

Step 2 只实现基础公共模块，不进入协议、socket、Reactor、MySQL、Redis 或 Qt。

本 Step 允许新增：

- `include/liteim/base/`
- `src/base/`
- `tests/base/`
- `src/CMakeLists.txt`
- `src/base/CMakeLists.txt`

本 Step 不允许提前新增：

- `include/liteim/protocol/`
- `include/liteim/net/`
- `include/liteim/storage/`
- `include/liteim/cache/`
- `client_cli/`
- `client_qt/`
- `bench/`
- `scripts/`
- `docker/`

## Step 2 实现结论

- `Config` 使用默认值 + 简单 `key=value` 文件加载，先覆盖 server、MySQL、Redis、Qt 默认连接配置，为后续 Step 保留统一入口。
- `Config::loadFromFile()` 返回 `Status`，而不是直接抛异常或返回裸 `bool`，方便调用方区分 `NotFound`、`ParseError` 和 `InvalidArgument`。
- 当前配置解析器只支持本项目需要的扁平 key，不做 YAML/JSON/TOML，避免在基础阶段引入额外复杂度。
- `Logger` 通过 `spdlog` 建立 `liteim` logger，统一输出格式；本 Step 不自研异步日志。
- `Logger` 内部用 `std::mutex` 保护全局 logger 初始化，避免多处调用 `init()` / `get()` 时重复创建。
- `Logger::get()` 只保证 logger 存在，不重置日志级别；只有 `init()` 和 `setLevel()` 会显式改变级别。
- `ErrorCode` 和 `Status` 是后续模块的统一错误表达：简单场景返回 `Status`，复杂数据结果以后可以再引入 `Result<T>`。
- `Timestamp` 提供毫秒时间戳和 UTC ISO-8601 字符串，后续消息时间、日志字段、压测统计都可以复用。
- `liteim_server` 仍然只是 scaffold，没有真实监听 socket；它现在用默认 `Config` 初始化日志并打印 `0.0.0.0:9000`。
- `liteim_base` 暴露公共 include root：`${PROJECT_SOURCE_DIR}/include`，后续模块统一使用 `#include "liteim/base/Config.hpp"` 这种项目限定路径。
- Step 2 没有恢复 SQLite、`InMemoryStorage`、旧 `server/net` 或旧 `server/protocol` 路线。

## Step 3 约束

Step 3 只实现协议类型定义，不进入二进制 Packet 编解码或 TCP 流式解码。

本 Step 允许新增：

- `include/liteim/protocol/MessageType.hpp`
- `include/liteim/protocol/Tlv.hpp`
- `src/protocol/CMakeLists.txt`
- `src/protocol/MessageType.cpp`
- `src/protocol/Tlv.cpp`
- `tests/protocol/message_type_test.cpp`
- `tests/protocol/tlv_type_test.cpp`

本 Step 不允许提前新增：

- `PacketHeader`
- `Packet`
- `encodePacket()`
- `parseHeader()`
- `FrameDecoder`
- `Buffer`
- socket / epoll / Reactor 相关代码

## Step 3 实现结论

- `MessageType` 使用 `std::uint16_t` 作为底层类型，和后续 Packet header 的 `msg_type` 字段匹配。
- 消息类型按范围分组：心跳、认证、好友、私聊、群聊、离线/历史、预留扩展和错误响应。
- 私聊和群聊都保留 `Push` 类型，用于后续服务端向接收方主动推送消息；`Push` 既不是 request，也不是 response，但需要通过 `isPushType()` 显式识别，避免和 `Unknown` 混在一起。
- `isRequestType()` 只返回客户端或 BotClient 主动请求类型。
- `isResponseType()` 只返回服务端对请求的响应类型，`ErrorResponse` 归为 response，方便后续统一错误返回。
- `isPushType()` 只返回 `PrivateMessagePush` 和 `GroupMessagePush`。
- `ListGroupsRequest` / `ListGroupsResponse` 已放入 4xx 群聊类型段，避免 Step 37 再回头修改协议编号。
- 未知 `MessageType` 和 `TlvType` 都返回 `UNKNOWN`，后续解析到未注册类型时可以安全记录日志而不是崩溃。
- `TlvType` 先覆盖登录、用户、好友、群组、会话、消息、错误和 PersonaAgent 接入需要的字段类型。
- 本 Step 没有定义 TLV value 的二进制格式、长度编码、网络字节序或 Packet header；这些属于 Step 4 和 Step 5。

## Step 4 约束

Step 4 只实现 fixed Packet header 编解码和校验，不进入 TLV body 字段编解码或 TCP 流式解码。

本 Step 允许新增：

- `include/liteim/protocol/Packet.hpp`
- `src/protocol/Packet.cpp`
- `tests/protocol/packet_test.cpp`

本 Step 不允许提前新增：

- `TlvCodec`
- `FrameDecoder`
- `Buffer`
- socket / epoll / Reactor 相关代码

## Step 4 实现结论

- `PacketHeader` 固定 20 字节，字段顺序为 `magic`、`version`、`flags`、`msg_type`、`seq_id`、`body_len`。
- `magic` 固定为 `0x4C494D31`，对应 ASCII `LIM1`。
- `version` 当前固定为 `1`，`flags` 当前固定为 `0`。
- `body_len` 上限为 1MB，避免异常 header 让后续解码器无限缓存。
- Header 多字节字段手动按网络字节序写入和读取，不直接 `memcpy` 整个结构体，避免结构体 padding、对齐和本机字节序影响 wire format。
- `validateHeader()` 只校验 header 级别约束，不检查 TLV body、登录态或业务权限。
- `encodePacket()` 会用 `packet.body.size()` 重新设置 `body_len`，不信任调用方传入的 `packet.header.body_len`。
- `parseHeader()` 只解析 fixed header，不解析 body；完整包拼接和半包/粘包处理属于后续 `FrameDecoder`。
- `liteim_protocol` 因为使用 `Status` / `ErrorCode`，需要链接 `liteim_base`。

## Step 5 约束

Step 5 只实现 TLV body 字段编解码，不进入 TCP 流式解码或网络层。

本 Step 允许新增：

- `include/liteim/protocol/TlvCodec.hpp`
- `src/protocol/TlvCodec.cpp`
- `tests/protocol/tlv_codec_test.cpp`

本 Step 不允许提前新增：

- `FrameDecoder`
- `Buffer`
- socket / epoll / Reactor 相关代码

## Step 5 实现结论

- TLV wire format 固定为 `type(2 bytes) + len(4 bytes) + value(len bytes)`。
- `type` 使用 `TlvType` 的 `std::uint16_t` 编号，`len` 使用 `std::uint32_t`。
- TLV header 和整数 value 都按网络字节序编码。
- `TlvMap` 使用 `std::unordered_map<TlvType, std::vector<Bytes>>`，支持重复字段。
- `parseTlvMap()` 负责通用格式校验和边界检查，不判断业务必需字段。
- `getString()`、`getUint64()`、`getRepeatedString()` 和 `getRepeatedUint64()` 表达必需字段读取；缺失字段返回 `ErrorCode::NotFound`。
- `getUint64()` 要求 value 长度必须是 8 字节，否则返回 `ErrorCode::ParseError`。
- 主动编码 `TlvType::Unknown` 返回 `ErrorCode::InvalidArgument`，避免本端生成无意义字段。
- 当前 TLV 工具层只保留 `String` 和 `Uint64` 两套读写 API；后续确有 signed 字段需求时，再成对添加 `appendInt64()` / `getInt64()`。

## Step 6 约束

Step 6 只实现 socket-agnostic 的 TCP 字节流解包器，不进入 socket、epoll、Reactor、网络 `Buffer` 或 `Session`。

本 Step 允许新增：

- `include/liteim/protocol/FrameDecoder.hpp`
- `src/protocol/FrameDecoder.cpp`
- `tests/protocol/frame_decoder_test.cpp`

本 Step 不允许提前新增：

- `include/liteim/net/`
- `src/net/`
- socket / epoll / Reactor 相关代码

## Step 6 实现结论

- `FrameDecoder` 内部用 `Bytes` 保存未消费字节，后续 Step 7 的网络 `Buffer` 会独立实现。
- `feed()` 每次追加新字节后，可能输出 0 个、1 个或多个完整 `Packet`。
- header 不足 20 字节时只缓存，不返回错误。
- header 足够后调用 `parseHeader()`，复用 Step 4 的 magic/version/body_len 校验。
- 完整帧长度为 `kPacketHeaderSize + header.body_len`，body 不足时继续缓存。
- 错误 magic、version、body_len 超限会让 decoder 进入 error 状态。
- error 状态下后续 `feed()` 直接返回 `ParseError`，等待上层关闭连接或显式 `reset()`。
- `FrameDecoder` 不解析 TLV body，不知道用户名、消息文本或业务类型含义。

## Step 7 约束

Step 7 只实现网络层通用字节缓冲区，不进入 socket、epoll、Reactor 或 `Session`。

本 Step 允许新增：

- `include/liteim/net/Buffer.hpp`
- `src/net/CMakeLists.txt`
- `src/net/Buffer.cpp`
- `tests/net/buffer_test.cpp`

本 Step 不允许提前新增：

- `SocketUtil`
- `Epoller`
- `Channel`
- `EventLoop`
- `Acceptor`
- `Session`
- `TcpServer`

## Step 7 实现结论

- `Buffer` 是 socket-agnostic 字节缓冲区，只负责保存可读数据和可写空间。
- `read_index_` / `write_index_` 划分已读、可读、可写区域。
- `append()` 会在空间不足时调用内部 `ensureWritableBytes()`，避免调用方直接管理底层字节数组。
- `ensureWritableBytes()` 优先复用已经 `retrieve()` 掉的前部空间；当前部空间不够时才扩容。
- `retrieve()` 只移动读指针或在读完时重置索引，不缩小底层 `vector`，避免频繁释放和重新分配。
- `retrieveAllAsString()` 返回所有可读字节并清空可读区域，适合测试和后续一次性取出文本 payload。
- `retrieve()` 越界返回 `InvalidArgument`，保持缓冲区原样，避免服务端因异常输入直接崩溃。
- `append(nullptr, nonzero)` 返回 `InvalidArgument`；`append(nullptr, 0)` 作为空追加成功。
- `liteim_net` 链接 `liteim_base`，因为错误路径使用 `Status` / `ErrorCode`。

## Step 8 约束

Step 8 只实现 Linux socket 常用工具函数，不进入 epoll、Reactor 或连接生命周期管理。

本 Step 允许新增：

- `include/liteim/net/SocketUtil.hpp`
- `src/net/SocketUtil.cpp`
- `tests/net/socket_util_test.cpp`

本 Step 不允许提前新增：

- `Epoller`
- `Channel`
- `EventLoop`
- `Acceptor`
- `Session`
- `TcpServer`
- `bind()` / `listen()` / `accept()` 封装

## Step 8 实现结论

- `SocketUtil` 是系统调用薄封装，不拥有 fd 生命周期之外的连接语义。
- `createNonBlockingSocket()` 使用 `AF_INET`、`SOCK_STREAM`、`SOCK_NONBLOCK` 和 `SOCK_CLOEXEC`，保证创建出的 socket 从一开始就是非阻塞并带 close-on-exec。
- `setNonBlocking()` 使用 `fcntl(F_GETFL)` 读取现有 flag，再通过 `fcntl(F_SETFL)` 加上 `O_NONBLOCK`。
- `setReuseAddr()`、`setReusePort()`、`setTcpNoDelay()` 和 `setKeepAlive()` 统一走 `setsockopt()`，并用 `Status` 返回错误。
- `closeFd(int& fd)` 关闭前保存当前 fd，然后立刻把原变量置为 `kInvalidFd`，让同一变量重复调用 `closeFd()` 成为 no-op。
- `closeFd()` 不能保护 fd 整数副本，后续 `Session` / fd owner 仍需要 RAII 管理所有权。
- `getSocketError()` 读取 `SO_ERROR`，后续非阻塞连接、写事件和连接异常处理会复用。
- 负 fd 统一返回 `InvalidArgument`，系统调用失败返回 `IoError`。

## Step 9 约束

Step 9 只定义 Reactor 核心接口，不实现实际 `epoll` 行为和事件循环。

本 Step 允许新增：

- `include/liteim/net/Epoller.hpp`
- `include/liteim/net/Channel.hpp`
- `include/liteim/net/EventLoop.hpp`
- `tests/net/channel_header_test.cpp`
- `tests/net/epoller_header_test.cpp`
- `tests/net/event_loop_header_test.cpp`

本 Step 不允许提前新增：

- `src/net/Epoller.cpp`
- `src/net/Channel.cpp`
- `src/net/EventLoop.cpp`
- `epoll_create1()` / `epoll_ctl()` / `epoll_wait()` 的真实封装实现
- `Channel::handleEvent()` 回调分发实现
- `EventLoop::loop()` 的阻塞循环实现
- `eventfd` 跨线程唤醒
- `Acceptor`、`Session`、`TcpServer`

## Step 9 实现结论

- `Channel.hpp` 只声明 fd 事件代理接口，不拥有 fd，不关闭 fd。
- `Channel` 保存 `owner_loop_`、`fd_`、关注事件 `events_`、本轮实际事件 `revents_` 和四类回调入口。
- `Channel::kReadEvent` 对应 `EPOLLIN | EPOLLPRI`，`Channel::kWriteEvent` 对应 `EPOLLOUT`，本项目当前仍按 LT 模式推进，不在 Step 9 暴露 ET 策略。
- `Channel::update()` 是私有接口，后续由 `enableReading()` / `disableWriting()` 等事件变更函数触发，再交给所属 `EventLoop` 更新 epoll。
- `Epoller.hpp` 只声明 epoll 封装边界，保留 `ChannelList`、`poll()`、`updateChannel()` 和 `removeChannel()` 接口。
- `Epoller` 私有状态预留 `epoll_fd_`、事件数组和 fd 到 `Channel*` 的映射，真实系统调用实现留给 Step 10。
- `EventLoop.hpp` 只声明 Reactor 调度层接口，提供 `loop()`、`quit()`、`updateChannel()`、`removeChannel()` 和线程归属检查。
- `EventLoop` 通过 `std::unique_ptr<Epoller>` 表达“一个 loop 拥有一个 epoller”，但不在 Step 9 实现阻塞循环、任务队列或 `eventfd` 唤醒。

## Step 10 约束

Step 10 只实现 `Epoller` 系统调用封装。

本 Step 允许新增：

- `src/net/Epoller.cpp`
- `src/net/Channel.cpp`
- `tests/net/epoller_test.cpp`

本 Step 允许调整：

- `Epoller` 接口从 `void`/直接返回列表改为 `Status` 返回错误，并通过输出参数返回 active channel list。

本 Step 不允许提前新增：

- `src/net/EventLoop.cpp`
- `Channel::handleEvent()` 回调分发实现
- `Channel::update()` 自动投递到 `EventLoop`
- `EventLoop::loop()` 阻塞循环
- `eventfd` 跨线程唤醒
- `Acceptor`、`Session`、`TcpServer`

## Step 10 设计结论

- `Epoller` 使用 `epoll_create1(EPOLL_CLOEXEC)` 创建 epoll fd，创建失败直接抛异常，避免半初始化对象继续存在；析构里关闭 epoll fd。
- 第一版只使用 LT 模式，不设置 `EPOLLET`。
- `epoll_event.data.ptr` 保存 `Channel*`，`poll()` 返回时把它取回，并把事件写入 `Channel::setRevents()`。
- `Epoller` 维护 `fd -> Channel*` 映射，用于判断 `EPOLL_CTL_ADD`、`EPOLL_CTL_MOD` 和 `EPOLL_CTL_DEL`。
- `epoll_wait()` 遇到 `EINTR` 时返回空 active list 和 `Status::ok()`，不把普通信号中断当成致命错误。
- 无效 `Channel*`、负 fd、重复删除等无效操作返回 `Status::error(...)`，不直接退出进程。
- `Channel.cpp` 在本 Step 只实现构造、fd/event/revent 访问、事件 mask 修改和回调 setter；`handleEvent()` 与自动 `update()` 留给后续 Step。

## Step 11 约束

Step 11 只实现 `Channel` 事件分发和关注事件更新链路。

本 Step 允许新增：

- `tests/net/channel_test.cpp`
- `src/net/EventLoop.cpp`
- `docs/tutorials/step11_channel.md`

本 Step 允许调整：

- `src/net/Channel.cpp`
- `src/net/CMakeLists.txt`
- `tests/CMakeLists.txt`

本 Step 不允许提前新增：

- `EventLoop::loop()` 阻塞循环
- `eventfd` 跨线程唤醒
- `runInLoop()` / `queueInLoop()`
- `Acceptor`、`Session`、`TcpServer`

## Step 11 设计结论

- `Channel` 仍然不拥有 fd，不关闭 fd，只保存 fd 值、关注事件、实际事件和回调入口。
- `enableReading()`、`disableReading()`、`enableWriting()`、`disableWriting()` 和 `disableAll()` 修改 `events_` 后会调用私有 `update()`。
- `Channel::update()` 在 `owner_loop_ != nullptr` 时调用 `EventLoop::updateChannel(this)`；`owner_loop_ == nullptr` 时不做 epoll 更新，方便纯状态测试和直接 `Epoller` 测试。
- `handleEvent()` 根据 `revents_` 分发回调：`EPOLLIN` / `EPOLLPRI` / `EPOLLRDHUP` 触发 read callback，`EPOLLOUT` 触发 write callback，`EPOLLHUP` 触发 close callback，`EPOLLERR` 触发 error callback。
- `EPOLLHUP` 且没有 `EPOLLIN` 时优先 close 并返回；Step 17 后 review hardening 已调整为 `EPOLLERR` 优先 error 但不直接返回，如果同一轮还有可读事件会继续 read callback。
- `handleEvent()` 只保存 `revents_` 快照，不复制 callback；Step 13 的 `Channel::tie()` 已经用 `weak_ptr` / `shared_ptr` 接管 owner 生命周期保护。未 `tie()` 的 `Channel` 只能用于 callback 不销毁自身、不重置自身 callback 的场景。
- `EventLoop.cpp` 当前只实现构造 `Epoller`、`quit()`、`updateChannel()`、`removeChannel()`、`isInLoopThread()` 和 `assertInLoopThread()`；完整事件循环和 `eventfd` 任务队列留给 Step 12。

## Step 12 约束

Step 12 只实现 `EventLoop` 事件循环和 `eventfd` 任务投递。

本 Step 允许调整：

- `include/liteim/net/EventLoop.hpp`
- `src/net/EventLoop.cpp`
- `tests/net/event_loop_header_test.cpp`
- `tests/CMakeLists.txt`

本 Step 允许新增：

- `tests/net/event_loop_test.cpp`
- `docs/tutorials/step12_event_loop.md`

本 Step 不允许提前新增：

- `Acceptor`
- `Session`
- `TcpServer`
- `EventLoopThread`
- `EventLoopThreadPool`
- 业务线程池、MySQL、Redis

## Step 12 设计结论

- `EventLoop` 是 Reactor 调度层，持有 `Epoller`，在 `loop()` 中阻塞等待 fd 事件并调用活跃 `Channel::handleEvent()`。
- `EventLoop` 构造时创建 `eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)`，交给 `UniqueFd` 管理，并用内部 `Channel` 注册读事件，用于跨线程 wakeup。
- `runInLoop()` 在调用线程就是 loop 所属线程时立即执行任务，否则转入 `queueInLoop()`。
- `queueInLoop()` 使用 mutex 保护任务队列；跨线程调用或当前正在执行 pending tasks 时会写 eventfd 唤醒 loop。
- `loop()` 每轮 poll 前后都执行 `doPendingTasks()`，避免 loop 启动前已有任务或事件回调后追加任务被长期滞留。
- `quit()` 设置原子退出标志；跨线程调用时写 eventfd，唤醒阻塞在 `epoll_wait()` 的 loop。
- `loop()` 用 RAII guard 管理 `looping_` 和 `loop_exited_`，并捕获活跃 `Channel` 回调和 pending task 异常；`isStopped()` 只在 `loop()` 已经进入并返回后为 true。
- `updateChannel()` 和 `removeChannel()` 继续要求在 loop 所属线程调用，用 `assertInLoopThread()` 保持 one-loop-per-thread 边界。
- 本 Step 的 `eventfd` 是内部 wakeup fd，不代表客户端连接；后续 `Acceptor` / `Session` 才会把网络 fd 注册到 `EventLoop`。

## Step 13 约束

Step 13 只实现 `Acceptor` 非阻塞监听器。

本 Step 允许新增：

- `include/liteim/net/Acceptor.hpp`
- `src/net/Acceptor.cpp`
- `tests/net/acceptor_header_test.cpp`
- `tests/net/acceptor_test.cpp`
- `docs/tutorials/step13_acceptor.md`

本 Step 允许调整：

- `src/net/CMakeLists.txt`
- `tests/CMakeLists.txt`

本 Step 不允许提前新增：

- `Session`
- `TcpServer`
- `EventLoopThread`
- `EventLoopThreadPool`
- 业务线程池、MySQL、Redis

## Step 13 设计结论

- `Acceptor` 只负责 listen socket、bind/listen、accept 新连接和 callback 通知上层，不创建 `Session`，不解析协议，不做业务路由。
- `Acceptor` 必须绑定一个有效 `EventLoop*`，构造、注册 listen channel 和关闭操作都应在所属 loop 线程执行。
- review hardening 后，`Acceptor::close()` 可以从非 loop 线程发起，但 `removeChannel()` 和 listen fd 关闭会通过 `queueInLoop()` 回到所属 loop 线程执行，避免 `Epoller` 保留 stale `Channel*`。
- hardening round 3 后，如果 `EventLoop` 已停止，`Acceptor::close()` 不再等待无法执行的 queued task，而是直接释放 listen channel、listen fd 和 idle fd。刚构造但尚未进入 `loop()` 的 `EventLoop` 不算 stopped，跨线程 close 仍会投递回 owner loop 清理。
- 新增 `UniqueFd` 表达 fd 独占所有权；`Acceptor` 用它保护 listen fd 和 accepted fd，callback 抛异常时 accepted fd 会自动关闭。
- `Channel::tie()` 使用 `weak_ptr` 锁定 owner；owner 已释放时跳过事件回调，owner 存在时用局部 `shared_ptr` 保证回调期间生命周期稳定。
- listen socket 使用 `createNonBlockingSocket()` 创建，继承 `SOCK_NONBLOCK` 和 `SOCK_CLOEXEC`。
- 构造时设置 `SO_REUSEADDR` / `SO_REUSEPORT`，然后执行 `bind()` 和 `listen(SOMAXCONN)`。
- 端口传 0 时让系统分配临时端口，再通过 `getsockname()` 记录真实端口，便于测试和后续服务启动日志。
- listen fd 可读时使用 `accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)` 循环接收连接，直到 `EAGAIN` / `EWOULDBLOCK`。
- `EINTR` 只表示系统调用被信号打断，应继续 accept，不当作 fatal error。
- `ECONNABORTED` 记录后继续；`EMFILE` / `ENFILE` 使用 idle fd 套路拒绝一个 pending connection，避免 LT 模式下 fd 用尽导致 busy loop；未知 errno 记录 warn 后退出本轮 accept。fd 用尽 helper 是 `noexcept`，日志写入异常会被内部 wrapper 吞掉。
- 新连接 fd 的所有权通过 `NewConnectionCallback` 交给上层；没有 callback 时立即关闭 accepted fd，callback 抛异常时由局部 `UniqueFd` 自动关闭，避免 fd 泄漏。
- `close()` 从 `EventLoop` 移除 listen channel 并关闭 listen fd；关闭后 `listenFd()` 返回 `kInvalidFd`，`listening()` 返回 false。

## PersonaAgent 最新路线结论

- PersonaAgent 作为项目二独立推进，不嵌入 C++ LiteIM 服务端。
- PersonaAgent 后续通过 Python BotClient 使用同一套 TLV 协议登录 LiteIM，作为普通用户账号收消息、发消息。
- AgentService 使用 FastAPI，对 BotClient 暴露 `/chat`。
- LangGraph 第一版收敛为 6 个核心节点：`dialogue_policy`、`retrieve`、`tool_router`、`generate_reply`、`safety_check`、`send_message`。
- `retrieve` 节点内部统一调度 Knowledge RAG、Memory RAG 和 Authorized Style RAG，不再拆成十几个薄节点。
- Authorized Style RAG 是项目二核心卖点，不是普通 few-shot。样本入库前必须有 consent manifest、来源、用途、脱敏、active 状态和撤回机制。
- Style RAG 只能用于授权风格模拟，SafetyGuard 必须拦截冒充真人、越权模仿、隐私泄露和替真人做现实承诺。
- 默认单元测试使用 `MockLLMClient`，真实 GPT / embedding / Chroma 只在集成或演示模式使用。
- Evaluation 必须覆盖 Retrieval Hit@5、Style Similarity、Persona Consistency、Safety Violation Rate、Human Review Trigger Rate、延迟、token cost 和 LiteIM 接入成功率。

## 测试要求

- Step 0 验证 CMake 空骨架可 configure/build。
- Step 1 开始，每个行为变化都要配 GoogleTest 测试。
- Step 2 使用 `tests/base/*_test.cpp` 覆盖默认配置、配置文件覆盖、缺失配置保留默认值、缺失文件、未知 key、非法端口、错误码字符串、`Status` 成功/失败状态、日志级别解析、logger 初始化和时间戳格式。
- Step 3 使用 `tests/protocol/*_test.cpp` 覆盖消息类型字符串、未知类型回退、请求/响应/Push 分类、群列表消息类型和 TLV 字段字符串。
- Step 4 使用 `tests/protocol/packet_test.cpp` 覆盖普通 Packet 编解码、空 body、UTF-8 body、网络字节序、错误 magic、错误 version、body_len 超限、encode 超大 body、不完整 header 和空指针输入。
- Step 5 使用 `tests/protocol/tlv_codec_test.cpp` 覆盖单字段、多字段、UTF-8 字符串、重复字符串字段、重复 `uint64` 字段、`uint64` 网络字节序、TLV len 越界、不完整 TLV header、缺失字段、错误 `uint64` 长度和 Unknown 类型编码。
- Step 6 使用 `tests/protocol/frame_decoder_test.cpp` 覆盖完整包、半包、粘包、半包后接粘包、错误 magic、错误 version、body_len 超限、error 状态拒绝继续解析和空指针输入。
- Step 7 使用 `tests/net/buffer_test.cpp` 覆盖默认状态、append、字符串输入、`Byte*` 输入、retrieve、retrieveAll、retrieveAllAsString、自动扩容、前部空间复用、越界 retrieve 和空指针 append。
- Step 8 使用 `tests/net/socket_util_test.cpp` 覆盖非阻塞 socket 创建、plain socket 设置非阻塞、常用 socket option、无效 fd 错误返回、重复关闭保护和 `SO_ERROR` 查询。
- 协议、Buffer、FrameDecoder 等底层模块优先写确定性的 GoogleTest 单元测试。
- 后续业务层测试优先使用 gMock mock `IStorage` / `ICache`，避免单元测试依赖真实 MySQL / Redis。
- 网络行为先写 smoke test，等 CLI / Python 客户端出现后再补 E2E。
- MySQL / Redis 区分纯单元测试和依赖 Docker Compose 的集成测试。
- README 和报告里的 QPS、p99、内存占用只能来自真实压测结果，不能写虚构数字。

## 2026-05-09 文档边界纠正

- `docs/debug_cases/` 是有效的内部复盘文档目录，应该保留。当前保留的复盘包括网络生命周期 hardening 和 ThreadPool worker stop 并发问题。
- 顶层 `docs/architecture.md`、`docs/project_layout.md`、`docs/roadmap.md` 暂不恢复；GitHub 对外说明使用 `README.md`，长期路线使用 `/home/yolo/jianli/PROJECT_MEMORY.md`。
- `docs/tutorials/README.md` 不再维护；`docs/tutorials/` 只保留每个 Step 的教程文件。
- `docs/process/task_plan.md`、`docs/process/findings.md`、`docs/process/progress.md` 是过程记忆，应尽量保留历史内容；纠正记录只追加，不用摘要覆盖。
- 主 README 不写 `Current Status` / `当前状态` 标题，也不把 planning 三件套当成对外介绍内容。

## 2026-05-16 Step 39 HistoryService Findings

- Step 39 不需要新建 SQL schema 或 DAO 方法：`IStorage::getHistory(const HistoryQuery&, std::vector<MessageRecord>&)` 已存在，`MySqlStorage` 已委托到 `MessageDao::getHistoryByConversation()`。
- `MessageDao::getHistoryByConversation()` 已按 `conversation_type`、`conversation_id`、可选 `message_id < before_message_id` 查询，并按 `message_id DESC LIMIT ?` 返回；service 层只需要传正确的 `HistoryQuery`。
- 协议层已有 `MessageType::HistoryRequest` / `HistoryResponse`，也已有 `TlvType::ConversationType`、`ConversationId`、`MessageId`、`Limit`、消息字段 TLV；Step 39 可复用 TLV body，不新增 JSON 模式。
- `HistoryRequest` 的 `before_message_id` 复用请求体中的 `TlvType::MessageId` 表示；响应里的每条消息也继续重复使用 `TlvType::MessageId`。
- 群聊历史权限需要在 service 层调用 `IStorage::findGroupById()` 和 `IStorage::getGroupMembers()` 验证；否则知道 `group_id` 的非成员可以读取历史。
- 私聊历史第一版可通过当前 `ChatService` 的 conversation id 生成规则判断当前用户是否参与该私聊会话，避免任意用户凭 `conversation_id` 拉取别人私聊历史。

## 2026-05-16 Step 40 HeartbeatService Findings

本次进入 `Step 40：实现 HeartbeatService`。用户已确认采用方案 B：Redis TTL 刷新失败时记录降级 warning，仍返回 `HeartbeatResponse`。

已经确认并采用的设计：

- `HeartbeatService` 位于 `liteim_service`，只依赖 `OnlineService`，不直接依赖 `ICache` 或 Redis 具体组件。
- `HeartbeatService::registerHandlers()` 注册 `HeartbeatRequest` 到 `MessageRouter` 的 `BusinessThread` dispatch，覆盖 `MessageRouter` 构造时的默认 inline 心跳 handler。
- 未登录连接发合法 `HeartbeatRequest` 时返回 `HeartbeatResponse`，不写 Redis。
- 已登录连接先通过 `OnlineService::getUserBySession(session_id)` 取得当前 user id，再调用 `OnlineService::refreshUserOnline(user_id, session_id)` 刷新 Redis 在线 TTL。
- Redis TTL 刷新失败只记录 warning，不返回 `ErrorResponse`，不关闭连接，不清理 `SessionManager` 绑定。
- `HeartbeatResponse` 只表示服务端成功收到并处理合法心跳包，不保证 Redis 在线状态 TTL 一定刷新成功。
- `HeartbeatService` 不直接更新 `Session::last_active_time`；完整合法入站 packet 已经在 `Session` 读路径刷新连接活跃时间。

本次不采用/不改：

- 不新增协议类型或 TLV 字段。
- 不新增 metrics 模块；当前第一版通过 warning 日志暴露 Redis TTL 刷新失败，后续可接入指标和告警。
- 不实现客户端重连策略、断线提示 UI、跨节点在线状态路由。


## 2026-05-16 Step 41 CLI Client Findings

本次进入 `Step 41：实现 CLI 测试客户端`。

当前采用的 CLI 边界：

- 新增 `client_cli/` 作为命令行测试客户端入口，不把 CLI helper 暴露为 LiteIM 服务端公共模块。
- CLI 第一版是协议调试工具，不做 curses/TUI、不做联系人列表 UI、不做本地持久化。
- 默认连接 `127.0.0.1:9000`，支持 `--host` / `--port` 覆盖。
- 从交互 stdin 或管道 stdin 读取命令，构造普通 TLV `Packet` 后发送。
- 后台接收线程持续读取服务器 response / push 并打印解码后的字段。
- 后台心跳线程每 30 秒发送 `HeartbeatRequest`；手动 `heartbeat` 命令也可直接发一次。
- 命令覆盖 register/login/add-friend/friends/private/create-group/join-group/groups/group/history/offline/heartbeat/help/quit。

TDD RED：

- 新增 `tests/client_cli/cli_protocol_test.cpp`。
- 更新根 `CMakeLists.txt` 增加 `add_subdirectory(client_cli)`，更新 `tests/CMakeLists.txt` 链接 `liteim_client_cli` 并编译 CLI 测试。
- `cmake --build build --target liteim_tests -j2` 按预期失败于 `add_subdirectory given source "client_cli" which is not an existing directory`。

实现确认：

- `liteim_client_cli` 是 CLI helper 库，不被服务端 runtime 依赖；`liteim_cli` 是单独可执行入口。
- `history private|group <conversation_id> [limit] [before_message_id]` 复用现有 `HistoryRequest` TLV：`ConversationType`、`ConversationId`、可选 `Limit`、可选 `MessageId`。
- `offline [limit]` 复用 `OfflineMessagesRequest`，只在传入 limit 时写 `TlvType::Limit`。
- `ProtocolClient` 第一版使用阻塞 socket；这是 CLI 调试工具的实现选择，不改变服务端非阻塞 Reactor 路线。
- loopback 发送测试最初失败不是生产发送逻辑问题，而是测试在 server accept/read 线程 join 前读取 `packet_`；修正为先 `server.wait()` 再断言。
- Step 41 不增加新的协议枚举、TLV 字段、MySQL schema、Redis key 或服务端 handler。
