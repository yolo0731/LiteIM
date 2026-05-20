# LiteIM Progress

## 2026-05-20 Post-Step58 Reliability And Cleanup Follow-up

用户确认 GPT Pro / Claude Code 审阅中提到的四个较大项也必须修正，本次在第一版审计基础上继续收口。

修复内容：

- `Session::sendPacket()` 对已关闭 session、未启动/不可写 session、单包或 pending 输出超过 high-water mark 的可同步失败返回错误，不再全部静默 OK。
- `ChatService` 在线私聊 push 遇到同步 `sendPacket()` 失败时，调用 `saveOfflineMessage()` 补 offline fallback row，并只在新 row 成功插入时增加 unread；`AlreadyExists` 作为幂等成功处理。
- 重复 `(sender_id, client_msg_id)` 不再只是返回已有 `message_id`，还会查询 receiver delivery status：已 delivered/read 不重投，未 delivered 才按当前接收方在线状态重试 push 或补 offline fallback。已有 offline row 不重复加 unread。
- `MessageDao` 暴露事务内 `insertMessageInTransaction()` / `findByIdInTransaction()`，`MySqlStorage::saveMessageWithOfflineRecipients()` 复用 MessageDao 的消息插入/查回逻辑，避免两处 messages INSERT SQL 分叉。
- 删除旧 `AuthDao` 头文件、实现、CMake 编译项和测试依赖；当前 users 表访问只保留 `UserDao`。
- `FriendDao::createFriendRequest()` 拒绝反向 pending 好友申请，避免 Alice->Bob 和 Bob->Alice 同时存在两条 pending。
- README、Step25、Step34、Step53、Step54、Step58 和 process 文档同步当前行为：`Token` 是预留字段，`pushed` 状态当前不持久化，离线 ACK 是幂等语义，`max_messages_per_pull` 不会丢弃剩余 pending 消息。

当前验证：

- RED：新增 ChatService fallback / duplicate retry 测试后，首次 `ctest -R ChatService` 按预期 3 个用例失败。
- RED：新增 `DuplicateClientMessageIdSkipsFallbackWhenAlreadyDelivered` 后，重建测试二进制再跑 ChatService，按预期失败于已 delivered 的 duplicate retry 仍补 offline row 和 unread。
- GREEN：实现 `sendPacket()` 同步失败语义和 ChatService fallback 后，`ctest --test-dir build -R "ChatService|SessionTest.*HighWater|TcpServerTest.*HighWater|TcpServerTest.SlowClient|TcpServerTest.ClosedSlowClient" --output-on-failure` 通过，27/27。
- RED：新增反向 pending 好友申请集成测试后，首次 targeted storage 测试失败于反向申请仍被接受。
- GREEN：修正 `FriendDao` 后，`ctest --test-dir build -R "FriendGroupDaoIntegrationTest.ReversePendingFriendRequestIsRejected|MessageDao|MySqlStorage|UserDao" --output-on-failure` 通过，23/23。
- GREEN：新增 `IStorage::findDeliveryStatus()` 和 `MySqlStorage` 查询实现后，`ctest --test-dir build -R "ChatService|MySqlStorage" --output-on-failure` 通过，28/28。
- `cmake --build /tmp/liteim-review-build --target liteim_tests liteim_server liteim_bench -j2`：`-Wall -Wextra -Wpedantic` review build 通过，无项目源码 warning。
- `cmake --build build --target liteim_tests liteim_server liteim_bench -j2`：通过。
- `docker compose -f docker/docker-compose.yml up -d --wait && ctest --test-dir build --output-on-failure`：MySQL / Redis healthy，默认 CTest 419/419 通过。
- `cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2 && ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMQtClient.Step50|LiteIMQtClient.Step51|LiteIMQtClient.Step52|LiteIMCMake.QtClientFoundation" --output-on-failure`：Qt 8/8 通过。
- `git diff --check`：通过。
- 教程标题脚本检查：所有 `docs/tutorials/step*.md` 保持 0-10 结构，最后主章节是 `面试常见追问`。
- 当前-facing `AuthDao` 扫描：只剩 Step25 教程中“已删除 AuthDao”的历史说明；源码、测试、CMake 和 README 不再引用旧 DAO。
- 旧路线路径和临时文件扫描：没有真实 `server/net`、`server/protocol`、`*SQLite*`、`*InMemory*`、`*BotService*`、`*BotGateway*`、`__pycache__`、`*.pyc`、`*.tmp`、`*.bak`、`*.orig` 或 `.DS_Store` 残留。

## 2026-05-20 Post-Step58 Final Project Audit

本次在 Step58 完成后审阅项目第一版完成态，范围包括代码、Markdown、协议/handler 对齐、未使用表面、生成物和文件夹清洁度。

当前发现和修复：

- 发现 `MessageType::LogoutRequest` / `LogoutResponse` 已在协议层定义、字符串化和 request/response 分类中存在，但服务端没有注册 logout handler，导致真实 E2E 会返回 `ErrorResponse(no message handler registered)`。
- 采用最小修复：新增 `AuthService::handleLogout()`，通过 `OnlineService::unbindSession(session_id)` 解除当前 session 绑定并清理 Redis 在线态，返回带 `SessionId` 的 `LogoutResponse`。
- `AuthService::registerHandlers()` 现在注册 Register / Login / Logout 三个 business-thread handler。
- CLI 新增 `logout` 命令，Python E2E helper 新增 `logout()`，`test_auth` 验证 logout 后需要登录态的请求会失败。
- README、Step34、Step41、process 文件已同步当前 auth/CLI 行为。

当前验证：

- RED：`cmake --build build --target liteim_tests -j2` 先按预期失败于缺少 `AuthService::handleLogout()`。
- GREEN：`cmake --build build --target liteim_tests -j2` 已通过。
- `ctest --test-dir build -R "AuthService|ClientCliCommandTest|LiteIME2E.test_auth|MessageType" --output-on-failure` 首次失败于旧 `liteim_server` 二进制未重建，E2E 返回 `no message handler registered`。
- 重建 `liteim_server` 后 `ctest --test-dir build -R "LiteIME2E.test_auth" --output-on-failure` 已通过。
- `cmake -S . -B /tmp/liteim-review-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic' && cmake --build /tmp/liteim-review-build --target liteim_tests liteim_server liteim_bench -j2`：通过，无项目源码 warning。
- `cmake --build build --target liteim_tests liteim_server liteim_bench -j2`：通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `ctest --test-dir build --output-on-failure`：通过，414/414。
- `cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2`：通过。
- `ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMQtClient.Step50|LiteIMQtClient.Step51|LiteIMQtClient.Step52|LiteIMCMake.QtClientFoundation" --output-on-failure`：通过，8/8。
- `python3 -m compileall -q tests/e2e`：通过；随后删除生成的 `tests/e2e/__pycache__/`。
- 教程标题脚本检查：所有 `docs/tutorials/step*.md` 维持 0-10 结构，最后主章节是 `面试常见追问`。
- 旧路线路径和临时文件扫描：没有真实 `server/net`、`server/protocol`、`*SQLite*`、`*InMemory*`、`*step15_sqlite*`、`*.tmp`、`*.bak`、`*.orig`、`.DS_Store` 或 editor swap 文件。
- `git diff --check`：通过。
- `git status --ignored --short`：只剩本次修改文件和已忽略的 `build/`、`build-qt/`、`build-asan/` 构建目录。

## 2026-05-20 Step 58 Final README Showcase Materials

本次完成 `Step 58：最终 README、架构图、Qt 截图、面试说明和压测报告`。

恢复路线：

- 用户确认原 Step53 final README/showcase 应删除并整体移动到 Step58，作为 LiteIM 第一阶段最后一个结尾 Step。
- Step53-57 已补离线 ACK、`client_msg_id` 幂等、私聊 delivery ACK、业务线程池队列上限、真实 IP 登录限流验证、好友申请和私聊好友权限。
- Step58 不新增服务端行为，只让 README、报告、截图、教程和 process 文档统一反映当前项目边界。

实施记录：

- README 刷新当前能力说明，补充 `Delivery Semantics`，明确 `server-stored`、`delivered`、`read` 三种状态。
- README benchmark 改指向 `docs/reports/liteim_benchmark_report_2026-05-20.md`，并更新 smoke / baseline / stress 三组本机结果。
- README MySQL/Redis 说明补齐 `friend_requests`、`message_deliveries`、`054_delivery_ack.sql`、`055_client_msg_id.sql` 和 `057_friend_requests.sql`。
- README `Interview Notes` 增加 ACK / `client_msg_id` / delivered / read 区分；新增 `Known Limitations And Future Work`，主动列出单机、无多设备、无 TLS/token、无完整群 ACK、无 read receipt 和 PersonaAgent 尚未实现。
- 新增 `docs/reports/liteim_benchmark_report_2026-05-20.md`，记录当前本机闭环压测环境、命令、结果和面试口径。
- 刷新 `docs/reports/qt_client_showcase.png`，截图由当前 Qt Widgets `MainWindow` / `ConversationListWidget` / `ChatPage` / `MessageBubble` 代码路径渲染生成，画面改为 Step58/ACK/好友权限展示文案。
- 新增 `docs/tutorials/step58_final_docs_showcase.md`，按固定 0-10 教程模板收口。
- 同步 `docs/process/task_plan.md` 和 `docs/process/findings.md`。

当前 benchmark 数据：

- Smoke：4 连接，64B，20ms，1 秒，`qps=110.558`，`p99=9.537ms`，`error_count=0`。
- Baseline：10 连接，128B，10ms，10 秒，`qps=561.682`，`p99=11.006ms`，`error_count=0`。
- Stress sample：30 连接，128B，5ms，10 秒，`qps=712.749`，`p99=49.918ms`，`error_count=0`。

当前验证：

- `cmake --build build --target liteim_tests liteim_server liteim_bench -j2`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL `mysql:8.0` 和 Redis `redis:7.2-alpine` 均为 healthy。
- `ctest --test-dir build --output-on-failure`：通过，411/411。
- `cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2`：通过。
- `ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMQtClient.Step50|LiteIMQtClient.Step51|LiteIMQtClient.Step52|LiteIMCMake.QtClientFoundation" --output-on-failure`：通过，8/8。
- `docs/tutorials/step58_final_docs_showcase.md` 标题脚本检查：0-10 结构通过，最后一节是 `## 10. 面试常见追问`。
- `file docs/reports/qt_client_showcase.png && test -s docs/reports/qt_client_showcase.png`：PNG 存在，1080x720。

## 2026-05-20 Step 57 Friend Requests And Private Permission

本次进入 `Step 57：好友申请和私聊权限`。

恢复路线：

- Step 53-56 已补离线 ACK、`client_msg_id` 幂等、私聊 delivery ACK、业务线程池队列上限和真实 IP 登录限流验证。
- Step 57 解决权限短板：好友关系不再由 `AddFriendRequest` 直接创建，私聊发送前必须确认双方已是 accepted friends。
- 本 Step 不做黑名单、好友备注、好友删除、好友重新申请、群审批、群管理员、禁言、群聊全员 ACK 或已读回执。

TDD RED：

- 更新协议测试，要求 `AcceptFriendRequest` / `AcceptFriendResponse` / `RejectFriendRequest` / `RejectFriendResponse` 有稳定名字并被识别为 request/response。
- 更新 TLV 测试，要求 `FriendRequestStatus` 有稳定字段名。
- 更新 `FriendService` 测试，要求 add friend 只创建 pending request，accept 才创建双向好友关系，reject 不创建好友关系，重复 accept 返回清晰错误。
- 更新 `ChatService` 测试，要求未接受好友不能私聊，已接受好友保持原私聊流程。
- 更新 DAO / MySqlStorage 集成测试，要求 `friend_requests` 状态流转、`areFriends()` 和重复操作错误语义可验证。
- 更新 CLI 和 Python E2E，覆盖 accept/reject 命令、私聊前置好友关系、拒绝申请后不能私聊。
- 首次 `cmake --build build --target liteim_tests -j2` 按预期失败于缺少 `FriendRequestRecord`、`FriendRequestStatus`、`IStorage::createFriendRequest()` 和 `FriendDao::createFriendRequest()` 等接口。

GREEN 实现：

- 协议新增 `AcceptFriendRequest = 204`、`AcceptFriendResponse = 205`、`RejectFriendRequest = 206`、`RejectFriendResponse = 207`，TLV 新增 `FriendRequestStatus = 23`。
- 存储层新增 `FriendRequestStatus`、`FriendRequestRecord`、`createFriendRequest()`、`acceptFriendRequest()`、`rejectFriendRequest()` 和 `areFriends()`。
- MySQL 初始化脚本新增 `friend_requests` 表；新增 `scripts/migrations/057_friend_requests.sql`。
- `FriendDao` 实现 pending request 创建、事务化 accept、reject 和 accepted friendship 查询；`MySqlStorage` 转发新接口。
- `FriendService` 把 `AddFriendRequest` 改成 pending request，新增 accept/reject handlers，并在响应中返回 `FriendRequestStatus`。
- `ChatService` 在保存消息、离线写入、unread 增加和 online push 前检查 accepted friendship。
- CLI 新增 `accept-friend <requester_id>` 和 `reject-friend <requester_id>`，响应打印 `friend_request_status`。
- Python E2E helper 新增 accept/reject；离线、背压和私聊用例补齐好友申请/接受前置。
- `liteim_bench` 的 setup 阶段为每个 sender 和 receiver 完成好友申请/接受；计时阶段仍只统计普通私聊 request/response RTT。
- 新增 `docs/tutorials/step57_friend_requests_private_permission.md`，并同步 README、Step03、Step21、Step27、Step36、Step41 等当前-facing 文档。

当前验证：

- `cmake --build build --target liteim_tests liteim_server -j2`：通过。
- `cmake --build build --target liteim_tests liteim_server liteim_bench -j2`：通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `mysql -h127.0.0.1 -P33060 -uliteim -p6 liteim < scripts/migrations/057_friend_requests.sql`：通过，本地数据库已补 `friend_requests` schema。
- `ctest --test-dir build -R "MessageType|TlvType|ClientCliCommandTest|FriendService|ChatService|FriendGroupDaoIntegrationTest|MySqlStorageIntegrationTest|LiteIME2E.test_private_chat" --output-on-failure`：通过，61/61。
- 首次全量 `ctest --test-dir build --output-on-failure`：失败 2/410，原因是旧 `LiteIME2E.test_offline` 和 `LiteIME2E.test_backpressure` 未建立 accepted friendship，触发 Step57 新权限规则。
- `ctest --test-dir build -R "LiteIME2E.test_offline|LiteIME2E.test_backpressure" --output-on-failure`：修正 E2E 前置后通过，2/2。
- 修 benchmark 前最小 smoke：`./build/bench/liteim_bench --host 127.0.0.1 --port 9000 --connections 2 --message-size 16 --interval-ms 1 --duration-sec 1 --format json` 返回 `request_success=0,error_count=1`，证明旧 benchmark 未建立 accepted friendship。
- `ctest --test-dir build -R "Benchmark|MessageType|TlvType|ClientCliCommandTest|FriendService|ChatService|FriendGroupDaoIntegrationTest|MySqlStorageIntegrationTest|LiteIME2E.test_private_chat|LiteIME2E.test_offline|LiteIME2E.test_backpressure" --output-on-failure`：通过，71/71。
- 修 benchmark 后最小 smoke：`./build/bench/liteim_bench --host 127.0.0.1 --port 19057 --connections 2 --message-size 16 --interval-ms 1 --duration-sec 1 --format json` 返回 `request_success=152,error_count=0`。
- `ctest --test-dir build --output-on-failure`：通过，411/411。
- `git diff --check`：通过。
- `docs/tutorials/step57_friend_requests_private_permission.md` 标题脚本检查：0-10 结构通过，最后一节是 `## 10. 面试常见追问`。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" README.md docs/tutorials/step57_friend_requests_private_permission.md docs/tutorials/step03_protocol_types.md docs/tutorials/step41_cli_client.md docs/tutorials/step36_chat_service.md docs/tutorials/step21_storage_cache_interfaces.md docs/tutorials/step27_friend_group_dao.md`：无输出。
- 当前-facing 好友权限文案扫描只命中 Step57 / Step36 的显式历史对比，不是当前规则漂移。

## 2026-05-20 Step 54 Client Message Idempotency

本次进入 `Step 54：client_msg_id 幂等发送`。

恢复路线：

- Step 53 已完成离线 ACK；Step 54 继续可靠性路线，目标是解决“发送方网络重试导致重复消息”。
- 本 Step 只做私聊发送幂等，不做接收方 delivery ACK、群聊全员 ACK、read receipt、客户端自动重试队列或强制旧客户端升级。
- 用户已要求原 final README/showcase slot 移动到 Step 58，因此 Step54 文档只记录幂等发送，不做最终展示材料收口。

TDD RED：

- 更新 TLV 测试，要求 `TlvType::ClientMessageId` 有稳定字段名。
- 更新 `ChatService` 测试，要求重复 `client_msg_id` 返回已存在 `message_id`，且不重复 unread / offline side effect。
- 更新 MySQL storage 集成测试，要求重复保存同一 `(sender_id, client_msg_id)` 返回 `AlreadyExists`，并能查回原消息。
- 更新 CLI 测试，要求 `private-id <receiver_id> <client_msg_id> <text...>` 构造 `PrivateMessageRequest` 并打印 `client_msg_id`。
- 更新 Python E2E，要求 Bob 离线时 Alice 重复发送同一 `client_msg_id` 只产生一条离线消息。
- 首次 `cmake --build build --target liteim_tests -j2` 按预期失败于缺少 `ClientMessageId`、`MessageRecord::client_msg_id` 和 `findMessageByClientMessageId()`。

GREEN 实现：

- 协议新增 `TlvType::ClientMessageId = 51`。
- `MessageRecord` 新增 `client_msg_id` 字段；`appendMessageFields()` 在字段非空时写回 `ClientMessageId`。
- 存储接口新增 `findMessageByClientMessageId(sender_id, client_msg_id, message)`。
- `MessageDao` / `MySqlStorage` 的消息插入、查回、历史查询、离线查询均支持 `client_msg_id`。
- `scripts/init_mysql.sql` 新增 `messages.client_msg_id` 和唯一索引 `uk_messages_sender_client_msg(sender_id, client_msg_id)`；新增 `scripts/migrations/055_client_msg_id.sql`。
- `ChatService` 解析可选 `ClientMessageId`，校验非空和 64 字节上限；重复保存时查回已有消息并直接返回 response。
- CLI 新增 `private-id` 命令，`describePacket()` 打印 `client_msg_id`。
- 新增 `docs/tutorials/step54_client_message_idempotency.md`，并同步 README、Step03 协议教程和 Step41 CLI 教程。

当前验证：

- `cmake --build build --target liteim_tests liteim_server -j2`：通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `mysql -h127.0.0.1 -P33060 -uliteim -p6 liteim < scripts/migrations/055_client_msg_id.sql`：通过，本地数据库已补 `client_msg_id` schema。
- `ctest --test-dir build -R "TlvType|ClientCliCommandTest|ChatService|MySqlStorageIntegrationTest|LiteIME2E.test_private_chat" --output-on-failure`：通过，25/25。
- `ctest --test-dir build --output-on-failure`：通过，390/390。
- `git diff --check`：通过。
- `docs/tutorials/step54_client_message_idempotency.md` 标题脚本检查：0-10 结构通过，最后一节是 `## 10. 面试常见追问`。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" README.md docs/tutorials/step54_client_message_idempotency.md docs/tutorials/step03_protocol_types.md docs/tutorials/step41_cli_client.md`：无输出。
- `rg -n "MessageType::kPrivate" docs/tutorials/step03_protocol_types.md docs/tutorials/step54_client_message_idempotency.md README.md src include tests`：无输出。

## 2026-05-20 Step 53 Offline Delivery ACK

本次进入 `Step 53：离线消息 ACK 与投递状态`。

恢复路线：

- 用户要求删除原 Step53 final README/showcase slot，并把这类最终展示材料移动到 Step58 作为最后收口。
- 当前 Step53 是可靠投递路线的第一步：先修离线消息“拉取即 delivered”的丢消息窗口。
- 本 Step 只做离线 ACK，不做 `client_msg_id` 幂等、私聊在线 delivery ACK、群聊全员 ACK、已读回执、多设备或好友权限。

TDD RED：

- 更新协议测试，要求 `OfflineMessagesAckRequest` / `OfflineMessagesAckResponse` 可被识别为 request/response。
- 更新 TLV 测试，要求 `DeliveryStatus` 有稳定字段名。
- 更新 `OfflineMessageService` 测试，要求 pull 不再 mark delivered，ACK 才 mark delivered 并清 unread。
- 更新 CLI 测试，要求 `offline-ack <message_id>...` 构造批量 ACK 请求，并能打印 `delivery_status`。

GREEN 实现：

- 协议新增 `OfflineMessagesAckRequest = 504`、`OfflineMessagesAckResponse = 505` 和 `TlvType::DeliveryStatus = 50`。
- 存储层新增 `storage::DeliveryStatus` 和 `IStorage::ackOfflineMessages()`。
- MySQL 初始化脚本和 migration 新增 `message_deliveries` 表。
- `OfflineMessageDao` 在事务中 ACK 属于当前用户的离线消息，设置 `offline_messages.delivered = 1`，并 upsert `message_deliveries.status = delivered`。
- `MySqlStorage::saveMessageWithOfflineRecipients()` 保存离线 row 时同步创建 pending delivery row。
- `OfflineMessageService` 注册 pull 和 ACK 两个 handler：pull 只返回 pending，ACK 后再清 unread。
- CLI 新增 `offline-ack` 命令；Python E2E 新增“ACK 前重复拉取仍 pending，ACK 后不再返回”的黑盒验证。
- 新增 `docs/tutorials/step53_offline_delivery_ack.md`，删除旧的 `step53_final_docs_showcase.md`。

当前验证：

- `cmake --build build --target liteim_tests liteim_server -j2`：通过。
- `ctest --test-dir build -R "MessageType|TlvType|ClientCliCommandTest|OfflineMessageService|MessageDaoIntegrationTest|MySqlStorageIntegrationTest|LiteIME2E.test_offline" --output-on-failure`：通过，34/34。
- `ctest --test-dir build --output-on-failure`：通过，387/387。
- `git diff --check`：通过。
- `docs/tutorials/step53_offline_delivery_ack.md` 标题脚本检查：0-10 结构通过，最后一节是 `## 10. 面试常见追问`。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" README.md docs/tutorials/step53_offline_delivery_ack.md`：无输出。
- 本地数据库已执行 `scripts/migrations/054_delivery_ack.sql`，用于给已有 Docker MySQL 数据卷补 `message_deliveries` 表。

## 2026-05-19 Former Step 53 Final README Showcase Materials (moved to Step 58)

本次进入 `Step 53：补齐 README、架构图、Qt 截图、面试说明和压测报告`。

恢复路线：

- 这批内容原本作为 Step 53 完成；用户后来要求把最终 README、架构图、Qt 截图、面试说明和压测报告移动到 Step 58 作为最后结尾 Step。
- 当时 `PROJECT_MEMORY.md` 定义 Step 53 范围为最终展示材料：README、架构图、线程模型图、TLV/MySQL/Redis 摘要、Qt 截图、编译运行测试方式、压测结果、PersonaAgent 接入方式和面试说明。
- 本 Step 是文档/showcase 收口，不改 C++ 服务端行为、Packet/TLV 协议、MySQL schema、Redis key、Qt 功能逻辑或 PersonaAgent 实现。
- PersonaAgent 继续作为未来外部普通账号客户端接入；README 不写成 C++ server 内置 AI。

实施记录：

- README 新增 `Technology Stack`、Mermaid 服务端架构图、线程模型图、`Protocol And Data Model`、`Qt Client Showcase`、`Benchmark Report` 和 `Interview Notes`。
- README 的 MySQL/Redis 部分从 Step 流水账压缩为当前运行时依赖和职责摘要，详细过程仍留在 tutorials/process。
- 新增 `docs/reports/qt_client_showcase.png`。截图由当前 Qt `MainWindow` / `ChatPage` / `MessageBubble` 渲染生成，展示三栏 layout、会话列表、消息气泡、Offline/Reconnect 状态。
- 生成截图时首次误链旧 Qt 静态库位置，导致画面还有旧 `Agent` sidebar；已改为链接当前 `build-qt/client_qt/src/libliteim_qt_client_core.a` 并重新生成。
- 更新 `docs/reports/liteim_benchmark_report_2026-05-18.md`，说明该报告作为 Step53 README 压测数据来源保留，Step53 未重新运行 benchmark。
- 当时新增 `docs/tutorials/step53_final_docs_showcase.md`，按固定 0-10 教程模板记录本 Step；用户后来要求删除原 Step53 并把最终展示材料改到 Step58，因此该教程文件已移除。
- 同步 `docs/process/task_plan.md` 和 `docs/process/findings.md`。

当前验证：

- `git diff --check`：通过。
- 当时的 README / Step53 教程噪声扫描：无输出。
- 当时的 Step53 教程标题脚本检查：0-10 结构通过，最后一节是 `## 10. 面试常见追问`。
- `file docs/reports/qt_client_showcase.png && test -s docs/reports/qt_client_showcase.png`：PNG 存在，1080x720。
- `cmake -S . -B build`：通过。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -L unit --output-on-failure`：311/311 通过。
- `ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMQtClient.Step50|LiteIMQtClient.Step51|LiteIMQtClient.Step52|LiteIMCMake.QtClientFoundation" --output-on-failure`：8/8 通过。
- `cmake --build build -j2`：通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `ctest --test-dir build --output-on-failure`：381/381 通过。

## 2026-05-19 Step 52 Qt Heartbeat Reconnect Client Polish

本次进入 `Step 52：实现 Qt 心跳、断线提示、本地设置和体验打磨`。

恢复路线：

- `PROJECT_MEMORY.md` 定义 Step 52 范围为 Qt 客户端心跳、断线提示、手动重连、一次自动重连、本地设置、状态栏、发送失败提示、未读清理和截图说明。
- 本 Step 不修改服务端协议、MySQL schema、Redis key，不做本地 SQLite 缓存、复杂主题系统、系统托盘或自动重新登录协议。
- PersonaAgent 仍是未来普通外部账号；Qt 侧不增加 AI 特殊身份、特殊 sidebar 或特殊消息类型。

TDD RED：

- 新增 `QtClientRuntimeStep52Test`，覆盖心跳 timer 发 `HeartbeatRequest`、断线后状态变 Offline、手动重连、一次自动重连、登录层不抢聊天响应。
- 新增 `QtMainWindowStep52Test`，覆盖状态栏 Online/Offline 和 Reconnect 按钮，以及离线发送把 outgoing 气泡标为 Failed。
- 新增 `QtLoginWindowStep52Test`，覆盖 `QSettings` 保存并重载服务器地址、端口和最近用户名。
- 首次构建 `liteim_qt_client_tests` 按预期失败于 `ClientRuntime` 缺少 endpoint、heartbeat、status、reconnect 等 Step52 API。

GREEN 实现：

- `ClientRuntime` 增加 server endpoint、连接状态、心跳 `QTimer`、`HeartbeatRequest` 发送、`HeartbeatResponse` 消费、手动 `reconnect()` 和一次自动重连。
- `ClientSession` 增加 `pendingRequest()`，让 runtime、AuthController、ChatController 先判断 pending request 类型再消费 response。
- `AuthController` 在登录开始时记录 endpoint，登录成功后启动默认 30 秒心跳，并且只处理注册/登录 response。
- `ChatController` 只处理聊天、好友、群组和 history response，避免被心跳或 auth response 干扰。
- `MainWindow` 增加状态栏 `connectionStatusLabel` 和 `reconnectButton`，接收 runtime 连接状态并同步聊天页 Online/Offline。
- `ChatPage` 增加把最新 outgoing Sending 气泡改成 Succeeded / Failed 的接口；请求失败时主窗口显示短暂状态栏消息并标记失败气泡。
- `app.qss` 增加状态栏和重连按钮样式。
- README 增加 Step52 说明和 Qt 截图说明；新增 Step52 教程；同步 process 文档。

当前验证：

- `cmake --build build-qt --target liteim_qt_client_tests -j2`：通过。
- `ctest --test-dir build-qt -R LiteIMQtClient.Step52 --output-on-failure`：通过。
- `cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2`：通过。
- 首次 Qt Step46-52 回归发现旧 Step48 布局测试仍假设默认主窗口显示 `Online`；Step52 后默认未连接窗口应显示 `Offline`，已把旧布局测试改为只要求状态文本非空，连接语义由 Step52 专门测试覆盖。
- `ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMQtClient.Step50|LiteIMQtClient.Step51|LiteIMQtClient.Step52|LiteIMCMake.QtClientFoundation" --output-on-failure`：8/8 通过。
- `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib QT_QPA_PLATFORM=offscreen timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124`：通过，Qt 客户端进入事件循环后被 timeout 终止。
- `cmake -S . -B build`：通过，默认构建不查找 Qt。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -L unit --output-on-failure`：311/311 通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `ctest --test-dir build --output-on-failure`：381/381 通过。
- `git diff --check`：通过。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" README.md docs/tutorials/step52_qt_heartbeat_reconnect_polish.md`：无输出。
- `rg -n "^## " docs/tutorials/step52_qt_heartbeat_reconnect_polish.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。

## 2026-05-19 Step 51 Qt Private Group Agent Contact Flows

本次进入 `Step 51：实现 Qt 私聊、群聊和外部 Agent 普通联系人项`。

恢复路线：

- `PROJECT_MEMORY.md` 定义 Step 51 范围为 Qt 客户端主要 IM 功能：从好友/群组列表打开会话，支持添加好友、创建群、加入群，支持私聊/群聊发送，未来 PersonaAgent 只作为普通联系人项。
- 本 Step 不修改服务端协议、MySQL schema、Redis key，不在 C++ server 内加入 AI 身份、LLM、RAG 或特殊 @ 触发。
- PersonaAgent 当前仍是未来外部 Python BotClient 控制的普通账号；Qt 这里只保留普通联系人/会话入口。

TDD RED：

- 新增 `QtChatControllerTest`，覆盖 `AddFriendRequest`、`CreateGroupRequest` 和 `JoinGroupRequest` 的真实 wire packet。
- 新增 `QtMainWindowStep51Test`，覆盖点击好友打开私聊并发 `HistoryRequest` / `PrivateMessageRequest`，点击群组发 `HistoryRequest` / `GroupMessageRequest`，点击 PersonaAgent 后走普通私聊，以及 PersonaAgent 普通私聊 push 显示在当前聊天页。
- 首次构建 `liteim_qt_client_tests` 按预期失败于缺少 `liteim_client/app/ClientRuntime.hpp`。

GREEN 实现：

- 新增 `ClientRuntime`，组合 Qt 端 `TcpClient` 和 `ClientSession`。
- 新增 `ChatController`，把 Qt UI 动作转成现有 LiteIM TLV 协议请求，并解析 private/group push、history response。
- `AuthController` 改为持有 `ClientRuntime`；`LoginWindow` 暴露 runtime；`ClientApp` 登录成功后创建 `MainWindow(login_window.runtime())`。
- `ConversationModel`、`ContactListWidget` 和 `ConversationListWidget` 增加会话目标 id 元数据，让 UI 字符串会话 id 和协议整数字段分离。
- `MainWindow` 记录当前会话类型、目标 id 和 wire conversation id，把 `ChatPage` 的 history/send 信号分发到 `ChatController`。
- 修复 Step51 初次运行失败：联系人/群组列表不再同时响应 `currentRowChanged` 和 `itemClicked`，避免一次选择发出两次 `HistoryRequest`。
- 更新 README、Step51 教程和 process 文档。

当前验证：

- `cmake --build build-qt --target liteim_qt_client_tests -j2`：通过。
- `ctest --test-dir build-qt -R LiteIMQtClient.Step51 --output-on-failure`：通过。
- `cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2`：通过。
- `ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMQtClient.Step50|LiteIMQtClient.Step51|LiteIMCMake.QtClientFoundation" --output-on-failure`：7/7 通过。
- `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib QT_QPA_PLATFORM=offscreen timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124`：通过，Qt 客户端进入事件循环后被 timeout 终止。
- `cmake -S . -B build`：通过，默认构建不查找 Qt。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -L unit --output-on-failure`：311/311 通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `ctest --test-dir build --output-on-failure`：381/381 通过。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" README.md docs/tutorials/step51_qt_private_group_agent_flows.md`：无输出。
- `rg -n "^## " docs/tutorials/step51_qt_private_group_agent_flows.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。
- `git diff --check`：通过。

## 2026-05-19 Step 50 Qt Chat Page Bubbles History

本次进入 `Step 50：实现聊天窗口、消息气泡和历史消息加载`。

恢复路线：

- `PROJECT_MEMORY.md` 定义 Step 50 范围为 Qt 右侧 `ChatPage`、`MessageBubble`、`ChatInputBar`、左右气泡、自动换行、时间、发送状态、历史加载和 Enter / Shift+Enter 行为。
- 本 Step 不修改服务端协议、MySQL schema、Redis key、真实私聊/群聊发包、历史响应解析、心跳断线或 PersonaAgent runtime。
- PersonaAgent 仍然只是未来通过普通账号接入的外部 BotClient；Qt 这里只实现普通会话消息展示组件。

TDD RED：

- 新增 `QtChatPageTest`，覆盖打开会话触发最近历史请求、发送文本出现 outgoing 发送中气泡、空消息不能发送、收到私聊 incoming 消息出现左侧气泡、群聊 incoming 显示发送者昵称、加载更早历史使用最早 `message_id`。
- 新增 `QtChatInputBarTest`，覆盖 Enter 发送和 Shift+Enter 换行。
- 首次构建 `liteim_qt_client_tests` 按预期失败于缺少 `liteim_client/ui/ChatInputBar.hpp`。

GREEN 实现：

- 新增 `ChatInputBar`，封装 `QTextEdit + Send`，支持空输入禁用、Enter 发送、Shift+Enter 换行。
- 新增 `MessageBubble`，封装左右气泡、文本、时间、发送状态和群聊发送者昵称。
- 重写 `ChatPage`，用 `QScrollArea` 展示消息列表，打开会话时发出 `historyRequested(conversation_id, 0)`，加载更早消息时发出 `historyRequested(conversation_id, earliest_message_id)`，输入发送后先追加本地 outgoing `Sending` 气泡再发出 `sendMessageRequested`。
- 更新 `app.qss`，补充聊天页、气泡、输入栏和发送按钮样式。
- 更新 `client_qt/src/CMakeLists.txt`、`client_qt/tests/CMakeLists.txt`、README、Step50 教程和 process 文档。

当前验证：

- `cmake --build build-qt --target liteim_qt_client_tests -j2 && ctest --test-dir build-qt -R LiteIMQtClient.Step50 --output-on-failure`：通过。
- `cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`：通过。
- `cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2`：通过。
- `ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMQtClient.Step50|LiteIMCMake.QtClientFoundation" --output-on-failure`：6/6 通过。
- `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib QT_QPA_PLATFORM=offscreen timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124`：通过，Qt 客户端进入事件循环后被 timeout 终止。
- `cmake -S . -B build`：通过，默认构建不查找 Qt。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -L unit --output-on-failure`：311/311 通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `ctest --test-dir build --output-on-failure`：381/381 通过。
- `git diff --check`：通过。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" README.md docs/tutorials/step50_qt_chat_page_bubbles_history.md`：无输出。
- `rg -n "^## " docs/tutorials/step50_qt_chat_page_bubbles_history.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。

## 2026-05-19 Step 49 Qt Conversation Contact Unread

本次进入 `Step 49：实现会话列表、联系人列表和未读数`。

恢复路线：

- `PROJECT_MEMORY.md` 定义 Step 49 范围为 Qt 侧 `ConversationModel`、会话列表、联系人列表、群组列表、未读红点，以及收到 push 后更新摘要/未读的 UI 行为。
- 本 Step 不修改服务端协议、MySQL schema、Redis key、真实消息发送、历史加载、离线拉取或 PersonaAgent runtime。
- PersonaAgent 仍然是普通联系人或普通会话对象，不在 `SideBar` 顶级导航中出现。

TDD RED：

- 新增 `QtConversationModelTest`，覆盖 incoming message 更新摘要、会话置顶、本地未读 +1、当前会话不加未读、已读清零和新会话创建。
- 新增 `QtContactListWidgetTest`，覆盖联系人式列表显示在线状态和未读数。
- 新增 `QtMainWindowStep49Test`，覆盖主窗口中间栏使用 model-backed Messages，并能切换 Contacts / Groups。
- 首次构建 `liteim_qt_client_tests` 按预期失败于缺少 `liteim_client/model/ConversationModel.hpp`。

GREEN 实现：

- 新增 `client_qt/include/liteim_client/model/ConversationModel.hpp` 和 `client_qt/src/model/ConversationModel.cpp`。
- 新增 `ContactListWidget`，复用好友和群组列表渲染。
- 重写 `ConversationListWidget` 的中间列表区域：Messages 使用 `QListView + ConversationModel`，并通过内部 delegate 绘制头像、摘要、时间和红色未读 badge；Contacts / Groups / Settings 通过 `QStackedWidget` 切换。
- 在 Qt 本地 demo seed data 中加入好友在线/离线状态、群成员数量和普通 `PersonaAgent` 联系人占位项。
- 更新 `app.qss`，让 `QListView#conversationListItems`、`contactListItems`、`groupListItems` 和 `settingsListItems` 共用中间列表样式。
- 更新 `client_qt/src/CMakeLists.txt`、`client_qt/tests/CMakeLists.txt`、README、Step49 教程和 process 文档。

当前验证：

- `ctest --test-dir build-qt -R LiteIMQtClient.Step49 --output-on-failure`：通过。
- `cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`：通过。
- `cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2`：通过。
- `ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMCMake.QtClientFoundation" --output-on-failure`：5/5 通过。
- `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib QT_QPA_PLATFORM=offscreen timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124`：通过，Qt 客户端进入事件循环后被 timeout 终止。
- `cmake -S . -B build`：通过，默认构建不查找 Qt。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -L unit --output-on-failure`：311/311 通过。
- `docker compose -f docker/docker-compose.yml up -d --wait && ctest --test-dir build --output-on-failure`：381/381 通过。

## 2026-05-19 Step 48 Sidebar Agent Entry Cleanup

本次按用户选择采用方案 A：未来 PersonaAgent 是普通账号对象，只出现在联系人列表或会话列表里，不在 `SideBar` 顶级导航建立特殊 Agent 分类。

实现内容：

- 先按 TDD 修改 `QtMainWindowTest`，要求 `navAgentButton` 不存在，并继续覆盖 Messages / Contacts / Groups / Settings 的切换。
- RED 验证：`LiteIMQtClient.Step48` 首次失败于旧代码仍创建 `navAgentButton`。
- 删除 `SideBar` 的 Agent 按钮。
- 删除 `ConversationListWidget` 的 `agent` section 和 `populateAgent()`。
- 删除 `MainWindow` 的 Agent 标题映射。
- 删除 `app.qss` 中 `navAgentButton` 的样式 selector。
- 更新 README、Step48 教程、process 文件、`PROJECT_MEMORY.md`、`AGENTS.md` 和 `CLAUDE.md`，统一说明 Qt 可以参考常见微信式三栏 IM 交互，但不得使用微信品牌、logo、名称、图标、截图或素材。

当前验证：

- `cmake --build build-qt --target liteim_qt_client_tests -j2 && ctest --test-dir build-qt -R LiteIMQtClient.Step48 --output-on-failure`：通过。
- `cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2 && ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMCMake.QtClientFoundation" --output-on-failure`：4/4 通过。
- `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib QT_QPA_PLATFORM=offscreen timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124`：通过，Qt 客户端进入事件循环后被 timeout 终止。
- `cmake -S . -B build && cmake --build build --target liteim_tests -j2 && ctest --test-dir build -L unit --output-on-failure`：311/311 通过。
- `docker compose -f docker/docker-compose.yml up -d --wait && ctest --test-dir build --output-on-failure && git diff --check`：381/381 通过，diff check 通过。

## 2026-05-19 Qt Client Local Structure Refactor

本次按用户确认执行 `client_qt` 局部结构重构，不新开功能 Step。

实现内容：

- 将当时已有 Qt 头文件从 `client_qt/include/liteim_client/` 平铺目录移动到 `app`、`auth`、`network`、`protocol`、`ui` 子目录；Step 49 后新增 `model` 目录。
- 将当时已有 Qt 实现文件从 `client_qt/src/` 平铺目录移动到对应的 `app`、`auth`、`network`、`protocol`、`ui` 子目录，保留 `src/main.cpp` 作为入口；Step 49 后新增 `src/model`。
- 更新所有源码、测试和文档 include 路径，例如 `liteim_client/ui/MainWindow.hpp`、`liteim_client/network/TcpClient.hpp`、`liteim_client/protocol/PacketCodec.hpp`。
- 将 `client_qt/CMakeLists.txt` 拆成顶层入口、`client_qt/src/CMakeLists.txt` 和 `client_qt/tests/CMakeLists.txt`。
- 保留 `liteim_qt_client_core`、`liteim_qt_client`、`liteim_qt_client_tests` target 名和 `LiteIMQtClient.Step46/47/48` CTest 名。
- 保留 Qt 测试的 `QT_QPA_PLATFORM=offscreen` 和本机 Anaconda Qt `LD_LIBRARY_PATH` 处理，并显式把 Qt 可执行文件输出到 `build-qt/client_qt/`，避免改变原有运行命令。
- 更新 `README.md`、`PROJECT_MEMORY.md`、Step45/46/47/48 教程、process 文件和 Qt 工程结构守卫脚本。

当前验证：

- `cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`：通过。
- `cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2`：通过。
- `ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMCMake.QtClientFoundation" --output-on-failure`：4/4 通过。
- `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib QT_QPA_PLATFORM=offscreen timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124`：通过，Qt 客户端进入事件循环后被 timeout 终止。
- `cmake -S . -B build`：通过，默认构建不查找 Qt。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -L unit --output-on-failure`：311/311 通过。

## 2026-05-19 Step 48 Qt Three-Column Main Window

本次进入 `Step 48：实现 Qt 常见 IM 三栏主窗口`。

恢复路线：

- `PROJECT_MEMORY.md` 定义 Step 48 范围为 Qt 主界面布局：`MainWindow`、左侧 `SideBar`、中间 `ConversationListWidget`、右侧 `ChatPage`、消息/联系人/群组/设置按钮、顶部当前用户昵称和在线状态、resize 后自适应。
- 本 Step 不实现 Step 49 的真实会话模型、联系人模型、群组模型、未读红点、消息加载、push 更新、心跳重连或 PersonaAgent 行为。
- 后续 PersonaAgent 是普通账号对象，应进入联系人或会话列表，不作为 `SideBar` 顶级分类。LiteIM C++ 服务端仍不识别 AI 身份。

TDD RED：

- 新增 `QtMainWindowTest`，覆盖三栏对象名、左侧五个按钮、当前用户昵称/在线状态、中间区域切换、resize 后布局可用。
- 首次运行 `ctest --test-dir build-qt -R LiteIMQtClient.Step48 --output-on-failure` 按预期失败：现有空 `MainWindow` 没有 `mainSplitter`、`sideBar`、`conversationListWidget`、`chatPage`。

GREEN 实现：

- 新增 `SideBar`，固定窄宽度，提供 messages / contacts / groups / settings 四个入口和选中状态。
- 新增 `ConversationListWidget`，中间区域根据左侧 section 切换标题和占位列表。
- 新增 `ChatPage`，右侧展示当前用户昵称、在线状态、聊天区占位和禁用输入框。
- 重写 `MainWindow`，使用 `QSplitter` 组合三栏，左栏固定，中栏限制宽度，右栏自适应。
- 更新 `app.qss`，集中维护三栏、导航按钮、列表、状态和占位聊天区样式。
- `client_qt/src/CMakeLists.txt` 注册新 Qt 源文件，`client_qt/tests/CMakeLists.txt` 注册 `LiteIMQtClient.Step48`。
- `README.md` 和 `docs/tutorials/step48_qt_three_column_main_window.md` 已同步 Step 48 边界、运行流程、测试设计和面试表达。

当前验证：

- `cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`：通过。
- `cmake --build build-qt --target liteim_qt_client_tests -j2`：通过。
- `ctest --test-dir build-qt -R LiteIMQtClient.Step48 --output-on-failure`：1/1 通过。
- `ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48" --output-on-failure`：3/3 通过。
- `cmake --build build-qt --target liteim_qt_client -j2`：通过。
- `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib QT_QPA_PLATFORM=offscreen timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124`：通过，Qt 客户端进入事件循环后被 timeout 终止。
- `cmake -S . -B build`：通过，默认构建不查找 Qt。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -L unit --output-on-failure`：311/311 通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `ctest --test-dir build --output-on-failure`：381/381 通过。
- `git diff --check`：通过。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" README.md docs/tutorials/step48_qt_three_column_main_window.md`：无输出。
- `rg -n "^## " docs/tutorials/step48_qt_three_column_main_window.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。

## 2026-05-18 Step 47 Qt Login and Register Window

本次进入 `Step 47：实现 Qt 登录和注册窗口`。

恢复路线：

- `PROJECT_MEMORY.md` 定义 Step 47 范围为 Qt 客户端入口：`LoginWindow`、服务器地址/端口输入、用户名/密码输入、登录按钮、注册按钮、`RegisterDialog`、登录成功打开 `MainWindow`、登录失败显示服务端错误，以及记住最近服务器地址和用户名。
- 设计重点是登录/注册通过 `AuthController` 发送协议请求，UI 只展示状态，不直接操作 `QTcpSocket`。
- 本 Step 不实现三栏聊天主界面、消息气泡、好友/会话列表、历史加载、心跳重连、服务端协议变更或 PersonaAgent。

TDD RED：

- 新增 Qt 侧测试，覆盖登录窗口输入禁用、注册弹窗输入禁用、注册成功后继续登录、错误响应显示服务端错误、登录成功进入主窗口。
- 首次构建 Step 47 Qt 测试按预期失败于缺少 `liteim_client/auth/AuthController.hpp`。

GREEN 实现：

- 新增 `AuthController`，封装 `RegisterRequest` / `LoginRequest` 发送、`RegisterResponse` / `LoginResponse` / `ErrorResponse` 解析、busy 状态和本地登录态写入。
- 新增 `LoginWindow`，提供服务器地址、端口、用户名、密码、登录按钮、注册按钮和状态提示，并通过 `QSettings` 记住服务器地址、端口和用户名。
- 新增 `RegisterDialog`，提供注册用户名、密码、可选昵称和提交/取消按钮。
- 新增 `ClientApp`，把登录成功后的主窗口创建逻辑从 `main.cpp` 抽出为可测桥接函数。
- `client_qt/tests/CMakeLists.txt` 把 Step 46 和 Step 47 Qt 测试拆成两个 CTest 入口，并为 QWidget 测试设置 `QT_QPA_PLATFORM=offscreen`。
- `README.md` 和 `docs/tutorials/step47_qt_login_register.md` 已同步 Step 47 边界、运行流程、测试设计和面试表达。

当前验证：

- `cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`：通过。
- `cmake --build build-qt --target liteim_qt_client_tests -j2`：通过。
- `ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47" --output-on-failure`：2/2 通过。
- `cmake --build build-qt --target liteim_qt_client -j2`：通过。
- `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib QT_QPA_PLATFORM=offscreen timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124`：通过，Qt 客户端进入事件循环后被 timeout 终止。
- `cmake -S . -B build`：通过，默认构建不查找 Qt。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -L unit --output-on-failure`：311/311 通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `ctest --test-dir build --output-on-failure`：381/381 通过。
- `git diff --check`：通过。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" README.md docs/tutorials/step47_qt_login_register.md`：无输出。
- `rg -n "^## " docs/tutorials/step47_qt_login_register.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。

## 2026-05-18 Step 46 Qt PacketCodec and TcpClient

本次进入 `Step 46：实现 Qt PacketCodec 和 TcpClient`。

恢复路线：

- `PROJECT_MEMORY.md` 定义 Step 46 范围为 Qt 端协议和网络基础：PacketCodec、客户端 FrameDecoder、QTcpSocket TcpClient、连接/断开/收包/错误信号，以及 ClientSession 管理 seq_id、pending request 和登录态。
- 本 Step 不实现登录/注册窗口、三栏聊天界面、消息气泡、心跳重连或 PersonaAgent。
- 当前 `client_qt` 已存在未提交中文注释差异；这些不改变行为，后续实现保留并增量修改，不回滚用户已有改动。

TDD RED：

- 新增 Qt 侧 `liteim_qt_client_tests`，覆盖 PacketCodec wire format、半包/粘包、ClientSession、TcpClient 连接失败、发送 Packet 和 packetReceived 信号。
- 首次直接构建 `liteim_qt_client_tests` 失败于旧 `build-qt` 尚未重新配置，目标不存在；重新配置后按预期失败于缺少 `liteim_client/network/ClientSession.hpp`。

GREEN 实现：

- `client_qt/src/CMakeLists.txt` 增加 `liteim_qt_client_core` 并链接 Qt Widgets / Network，`client_qt/tests/CMakeLists.txt` 注册 `LiteIMQtClient.Step46`。
- 新增 `PacketCodec`，把 Qt `QByteArray` / `QString` 适配到现有 `liteim_protocol`，复用 `encodePacket()`、`TlvCodec` 和 `FrameDecoder`。
- 新增 `TcpClient`，使用 `QTcpSocket` 支持连接、断开、发送 Packet、readyRead 解码、`connected` / `disconnected` / `packetReceived` / `errorOccurred` 信号。
- 新增 `ClientSession`，管理客户端本地 seq_id、pending request、user_id、token 和 session_id。
- 针对当前 Anaconda Qt 运行时库顺序，给 `LiteIMQtClient.Step46` 设置 CTest `LD_LIBRARY_PATH`，让系统 `libstdc++` 优先于 Anaconda 旧 `libstdc++`。

当前验证：

- `cmake -S . -B build-qt -DLITEIM_BUILD_QT_CLIENT=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`：通过。
- `cmake --build build-qt --target liteim_qt_client_tests -j2`：通过。
- `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib ./build-qt/client_qt/liteim_qt_client_tests`：6/6 通过。
- `ctest --test-dir build-qt -R LiteIMQtClient.Step46 --output-on-failure`：1/1 通过。
- `cmake --build build-qt --target liteim_qt_client -j2`：通过。
- `cmake -S . -B build && cmake --build build --target liteim_tests -j2`：通过，默认构建不依赖 Qt。
- `ctest --test-dir build -L unit --output-on-failure`：311/311 通过。
- `ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMCMake.QtClientFoundation" --output-on-failure`：2/2 通过。
- `LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu:/home/yolo/anaconda3/lib QT_QPA_PLATFORM=offscreen timeout 2s ./build-qt/client_qt/liteim_qt_client || test $? -eq 124`：通过，Qt 客户端进入事件循环后被 timeout 终止。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `ctest --test-dir build --output-on-failure`：381/381 通过。
- `git diff --check`：通过。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" README.md docs/tutorials/step46_qt_packet_tcp_client.md`：无输出。
- `rg -n "^## " docs/tutorials/step46_qt_packet_tcp_client.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。
- `timeout 2s ./build/server/liteim_server --config <(printf '%s\n' 'server.port = 19001' 'server.io_threads = 4' 'server.business_threads = 4') || test $? -eq 124`：通过，server 监听 `0.0.0.0:19001` 后收到 SIGTERM 并通过 signalfd 退出。

## 2026-05-18 Step 45 Qt Client Foundation

本次进入 `Step 45：Qt 客户端基础工程和资源规范`。

恢复路线：

- 当前 `PROJECT_MEMORY.md` 明确 Step 45 是 Qt Widgets 客户端基础工程，不是旧路线中的测试硬化。
- Step 45 范围：`LITEIM_BUILD_QT_CLIENT` 可选构建、当时的 `client_qt/include/liteim_client/{ui}/`、`client_qt/src/{ui}/`、`resources/qss/app.qss`、图标资源规范文档和空窗口；后续重构扩展为 `app/auth/network/protocol/ui`，Step 49 再加入 `model`。
- 不做 Step 46 以后的 Qt 协议、`QTcpSocket`、登录注册、三栏主窗口、消息气泡或心跳。

环境发现：

- `cmake --find-package` 未找到 Qt6 或 Qt5 Widgets。
- 后续使用 `-DLITEIM_BUILD_QT_CLIENT=ON` 实际配置发现 Anaconda 提供 Qt5 Widgets：`/home/yolo/anaconda3/lib/cmake/Qt5Widgets`，因此不需要安装系统包即可验证 Qt target。

TDD RED：

- 新增 `tests/cmake/qt_client_foundation_test.sh`，注册 CTest `LiteIMCMake.QtClientFoundation`。
- 重新配置后运行 `ctest --test-dir build -R LiteIMCMake.QtClientFoundation --output-on-failure`，按预期失败：`missing LITEIM_BUILD_QT_CLIENT option`。

GREEN 实现：

- 根 `CMakeLists.txt` 增加 `LITEIM_BUILD_QT_CLIENT`，默认 `OFF`，开启后才 `add_subdirectory(client_qt)`。
- 新增 `client_qt/CMakeLists.txt` 查找 Qt Widgets，当前由 `client_qt/src/CMakeLists.txt` 构建 `liteim_qt_client_core` / `liteim_qt_client`，由 `client_qt/tests/CMakeLists.txt` 构建和注册 Qt 测试。
- 新增 `MainWindow.hpp/.cpp` 和 Qt `main.cpp`，启动空 `QMainWindow` 并加载 `:/qss/app.qss`。
- 新增 `resources/liteim_client.qrc`、`resources/qss/app.qss` 和 `resources/icons/README.md`，明确禁止使用第三方 IM 产品品牌资源。
- 修正 `qt_client_foundation_test.sh`，允许 README 说明禁用品牌，但禁止实际资源文件和文件名包含 WeChat/Weixin 品牌。

当前验证：

- `cmake -S . -B build`：通过，默认构建不查找 Qt。
- `ctest --test-dir build -R LiteIMCMake.QtClientFoundation --output-on-failure`：通过。
- `cmake --build build --target liteim_server liteim_tests -j2`：通过。
- `ctest --test-dir build -R "LiteIMCMake.QtClientFoundation|ConfigTest" --output-on-failure`：12/12 通过。
- `cmake -S . -B /tmp/liteim-qt-check -DLITEIM_BUILD_QT_CLIENT=ON`：通过。
- `cmake --build /tmp/liteim-qt-check --target liteim_qt_client -j2`：通过。
- `QT_QPA_PLATFORM=offscreen timeout 2s /tmp/liteim-qt-check/client_qt/liteim_qt_client || test $? -eq 124`：通过，空窗口进入事件循环。
- `cmake --build build -j2`：通过。
- `ctest --test-dir build -L unit --output-on-failure`：311/311 通过。
- `bash -lc 'timeout 2s ./build/server/liteim_server --config <(printf "%s\n" "server.port = 19001" "server.io_threads = 4" "server.business_threads = 4") || test $? -eq 124'`：通过，server 收到 SIGTERM 并关闭。
- `git diff --check`：通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `ctest --test-dir build --output-on-failure`：381/381 通过。

## 2026-05-17 Python BotClient Wording Review

用户指出 Step 41 教程里“Qt Client、Python BotClient 和 CLI 共享同一套服务端协议”会让人误以为当前已经实现 Python BotClient。

复核结论：

- 当前 LiteIM 没有 Python BotClient 功能。
- 当前已有的是 `client_cli/` 命令行调试客户端，以及 `tests/e2e/liteim_e2e.py` 里的 Python E2E 测试 helper。
- PersonaAgent BotClient 是后续项目二组件，应该在 LiteIM 文档中写成“后续 / planned”。

已同步：

- 更新 Step 41 CLI 教程，把当前实现和后续 PersonaAgent BotClient 分清。
- 更新 Step 3/5/22 教程、README、`PROJECT_MEMORY.md`、`AGENTS.md` 和 `CLAUDE.md` 中容易误读为当前功能的 BotClient 表述。
- 更新 process 文档记录该边界。

## 2026-05-17 Step Route Renumber After Step40

用户要求直接重排 Step 40 之后的路线，并同步所有 Markdown。

当时采用的新编号：

- CLI -> Step 41。
- Python E2E -> Step 42。
- benchmark -> Step 43。
- gMock / ASan / UBSan -> Step 44。
- Qt -> Step 45-52。
- final docs -> Step 53。用户后来再次调整，最终展示材料现在移动到 Step58。

实现内容：

- 重命名 post-Step40 教程文件为 `step41_cli_client.md`、`step42_python_e2e.md`、`step43_benchmark_tool.md`、`step44_test_coverage_sanitizers.md`。
- 同步 `/home/yolo/jianli/PROJECT_MEMORY.md`、README、tutorials 和 process Markdown 中的路线编号。
- 保持 C++ / CMake / 测试源码不变；本次是文档路线重排。

## 2026-05-17 Markdown Drift Sync

用户要求同步所有 Markdown，保证旧 C++ assistant 路线不再漂移。

实现内容：

- 全量扫描 `/home/yolo/jianli` 下 54 个 Markdown 文件，排除 LiteIM build 输出目录。
- 更新 `/home/yolo/jianli/AGENTS.md` 和 `/home/yolo/jianli/CLAUDE.md`：PersonaAgent 作为普通账号接入；Qt 只展示普通联系人/会话项；SafetyGuard 约束归 PersonaAgent，不让 C++ 服务端定义行文。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md`：项目二中文称呼统一为 AI Agent Worker；seed 和用户身份边界统一为普通账号。
- 清理 `LiteIM/README.md` 和相关教程里旧 assistant seed、旧专用协议、旧回复示例和旧边界说明。
- 清理 `docs/process/findings.md` / `task_plan.md` / `progress.md` 中会误导后续上下文恢复的旧 C++ assistant 细节，保留“该路线已移除”的当前结论。

当前已验证：

- 旧路线关键词扫描无输出。
- 旧独立身份称呼扫描无输出，保留的 `BotClient` 组件名不算漂移。
- 旧机械替换占位词扫描无输出。

## 2026-05-17 Retire C++ Assistant Route

用户确认原来的 C++ 内置 assistant 路线不再需要，直接从 LiteIM 中移除，后续 PersonaAgent 只作为普通账号客户端接入。

实现内容：

- 删除 C++ 内置 assistant gateway/service/echo fallback 相关头文件、实现、CMake 源文件和测试。
- 删除 assistant 专用 `MessageType` 常量，并让 600/601/602 回到 unknown。
- 删除 assistant 专用 `TlvType` 常量，并让 100/101 回到 unknown。
- `ChatService` / `GroupService` 构造函数不再注入 assistant 服务，所有账号都按普通在线/离线用户处理。
- `server/main.cpp` 不再构造 C++ echo fallback 或 assistant 服务。
- `scripts/seed_test_data.sql` 不再创建固定 assistant 用户、好友、群成员、消息或离线消息，并清理旧 seed 中的 `user_id=9001` 数据。
- 删除旧 assistant 教程，同步 README、PROJECT_MEMORY、Step 3/5/22/27/42/43 教程和 process 文档。

当前已验证：

- `ctest --test-dir build -R MessageType --output-on-failure` 在测试先行阶段按预期失败，证明 600/601/602 当时仍被识别为 assistant 类型。
- `cmake --build build --target liteim_tests -j2`：assistant route 删除后已通过一次。
- `ctest --test-dir build -R "MessageType|ChatService|GroupService" --output-on-failure`：26/26 通过。
- `cmake --build build -j2`：通过。
- `ctest --test-dir build -R "MessageType|TlvType|ChatService|GroupService" --output-on-failure`：28/28 通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- 重新执行 `scripts/seed_test_data.sql` 后查询确认：`users(user_id=9001)` 为 0，旧 assistant 相关 messages 为 0。
- `ctest --test-dir build --output-on-failure`：380/380 通过。
- `git diff --check`：通过。
- 源码/测试/SQL 旧 assistant 路线关键字扫描：无输出。
- 当前面向读者文档旧 assistant 路线关键字扫描：无输出。
- 旧 assistant 教程文件已删除。

## 2026-05-16 Post-Step44 Review Hardening

本次按评审结论执行 Step 44 后的必要收口，不拆 Step 45。

实现内容：

- 统一部分 Redis 降级策略：好友列表在线状态查询失败时降级为 offline；离线消息响应已组装后，Redis unread 清理失败不阻塞 MySQL delivered 标记。
- 将离线消息拉取 limit 下推到 `IStorage` / `MySqlStorage` / `OfflineMessageDao`，SQL 使用 `LIMIT ?`，避免先全量查询再内存截断。
- 增加服务层输入边界：username/nickname <= 64 bytes，password <= 128 bytes，group name <= 128 bytes，message text <= 8192 bytes；benchmark 同步拒绝过长 username prefix 和超过服务上限的 message size。
- 给 `liteim_server` 增加 `--config <path>`；不传时尝试读取 `config/liteim.conf`，不存在则使用 defaults。
- E2E helper 增加 `LITEIM_E2E_STRICT=1`，CI 中 server 启动失败会 fail 而不是 skip。
- 修复 ASan/UBSan CI 红点：fd-exhaustion 单测在 ASan runtime 下跳过，因为 sanitizer 自身会占用额外 fd，使该资源耗尽测试不稳定。
- 修复 CTest 多标签：GoogleTest discovery 通过 `TEST_INCLUDE_FILES` 在 CTest 阶段为 `mysql` / `redis` / `docker` 补真实多标签。
- 提取 `MessagePacketBuilder`，把 5 处重复的消息 TLV 字段编码收拢到一个 service helper。
- 更新 README 和 process docs，说明 config、service limits、history newest-first、bench 指标归属、E2E strict、CI/sanitizer 边界。

当前已验证：

- `cmake --build build -j2`：通过。
- `ctest --test-dir build -R "ChatService|GroupService|OfflineMessageService|HistoryService|FriendService|ConfigTest|PacketTest|BenchmarkOptions" --output-on-failure`：相关测试通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：MySQL / Redis healthy。
- `ctest --test-dir build -N -L mysql`：38 个测试可被 `mysql` 标签筛中。
- `ctest --test-dir build -N -L redis`：30 个测试可被 `redis` 标签筛中。
- `ctest --test-dir build-asan -R "AcceptorTest.FdExhaustionRejectsPendingConnectionWithoutLaterCallback" --output-on-failure`：按预期 skip，ASan 目标测试不再失败。
- `ctest --test-dir build -L unit --output-on-failure`：318/318 通过。
- `LITEIM_E2E_STRICT=1 ctest --test-dir build -L integration --output-on-failure`：71/71 通过。
- `cmake --build build-asan -j2`：通过。
- `LITEIM_E2E_STRICT=1 ctest --test-dir build-asan --output-on-failure`：389/389 通过，其中 `AcceptorTest.FdExhaustionRejectsPendingConnectionWithoutLaterCallback` 在 ASan 下按预期 skip。
- `ctest --test-dir build --output-on-failure`：389/389 通过。
- `git diff --check`：通过。
- 路径级 stale-route scan：无 `server/net`、`server/protocol`、`*SQLite*`、`*InMemory*` 或 `*step15_sqlite*` 文件路径输出。
- `timeout 2s ./build/server/liteim_server || test $? -eq 124`：通过，server 启动后通过 signalfd 收到 SIGTERM 并退出。

## 2026-05-16 Documentation Layout Cleanup

用户确认采用方案 A 后，本次清理目标是让 LiteIM 目录更干净：

- 删除原 GitHub Actions CI Step，不把“每次提交都跑测试”作为项目本身的一部分。
- 当时 Qt 客户端阶段调整为 Step 45-52，最终展示文档调整为 Step 53；用户后来把最终展示文档再次移动到 Step58。
- `task_plan.md`、`findings.md`、`progress.md` 移入 `docs/process/`。
- `tutorials/` 移入 `docs/tutorials/`，删除原 CI 教程。
- 保留 `docs/debug_cases/`。
- 清理本地 `build-asan/`、`build-asan-plan/` 和 `tests/e2e/__pycache__/`，并补充 `.gitignore`。

实现注意：

- 本次只改文档、目录布局、`.gitignore` 和删除 CI workflow，不改 C++ 行为。
- 继续保护进入本任务前已有的用户侧源码改动：`include/liteim/service/GroupService.hpp`、`include/liteim/service/HeartbeatService.hpp`、`include/liteim/service/HistoryService.hpp`、`src/service/GroupService.cpp`、`src/service/HeartbeatService.cpp`、`src/service/HistoryService.cpp`、`src/storage/GroupDao.cpp`。

验证结果：

- `git diff --check`：通过。
- 根目录清理检查：`task_plan.md`、`findings.md`、`progress.md`、`tutorials/`、`.github/`、`build-asan/`、`build-asan-plan/`、`tests/e2e/__pycache__/` 和旧 CI 教程均不存在。
- `docs/process/` 下保留 `task_plan.md`、`findings.md`、`progress.md`。
- `docs/tutorials/` 下保留 Step 00-44 共 46 个教程文件；所有教程最后一个主章节仍是 `## 10. 面试常见追问`。
- 路径级旧路线扫描无输出：没有真实 `server/net`、`server/protocol`、`*SQLite*`、`*InMemory*` 或 `*step15_sqlite*` 文件路径残留。
- 当时 Markdown 路线扫描只剩本清理记录中“删除原 GitHub Actions CI Step”的说明；`PROJECT_MEMORY.md` 在该时点改为 Step 41-44 工具验证、Step 45-52 Qt、Step 53 最终文档。

## 2026-05-16 Repository CI Infrastructure

用户重新确认：CI 有价值，但不要单独拆成独立 Step。本次将 CI 作为仓库基础设施补回。

实现内容：

- 新增 `.github/workflows/ci.yml`。
- workflow 在 push / pull request 到 `main` 时触发。
- `unit` job 安装依赖、Release configure/build，并运行 `ctest --test-dir build -L unit --output-on-failure`。
- `integration` job 安装依赖、Release configure/build，启动 Docker MySQL/Redis，并运行 `ctest --test-dir build -L integration --output-on-failure`。
- `sanitizers` job 使用 `-DLITEIM_ENABLE_SANITIZERS=ON` 配置 Debug build，启动 Docker MySQL/Redis，并运行 `ctest --test-dir build-asan --output-on-failure`。
- README 顶部加入 GitHub Actions badge，并新增 `Repository CI` 小节；Repository Layout 增加 `.github/workflows/ci.yml`。
- 更新 `docs/process/task_plan.md` 和 `docs/process/findings.md`，把本次记录为非编号 infra 改动。

边界：

- 不恢复 CI 教程。
- 不修改 Step 路线；该时点仍是 Step 41-44 工具验证、Step 45-52 Qt、Step 53 最终文档。
- 不修改 C++ 源码、协议、MySQL schema、Redis key、Qt 或 PersonaAgent。
- 继续保护进入本任务前已有的用户侧源码改动。

验证结果：

- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -L unit --output-on-failure`：301/301 通过。
- `ctest --test-dir build -N -L integration`：发现 70 个 integration / Docker / E2E 测试，workflow 的 integration job 可筛中目标。
- `git diff --check`：通过。
- `.github/workflows/ci.yml` 已存在；GitHub Actions CI 教程仍不存在。

## 2026-05-16 Step 44 Test Coverage, gMock, ASan/UBSan

本次进入 `Step 44：补齐单元测试覆盖率 + gMock + ASan/UBSan`。

开始状态：

- Step 43 已完成并记录为 benchmark tooling；Step 44 只补测试、CTest 标签和 sanitizer 构建，不修改服务端协议、MySQL schema、Redis key、Qt 或压测工具行为。
- 工作区已有用户侧未提交改动：`include/liteim/service/GroupService.hpp`、`include/liteim/service/HistoryService.hpp`、`src/service/GroupService.cpp`、`src/service/HistoryService.cpp`、`src/storage/GroupDao.cpp`，以及未跟踪 `build-asan-plan/`。本 Step 不回滚、不清理，并在提交时精确排除。
- `session-catchup.py` 提示的是历史概念问答，不对应当前 Step 44 代码实现。

TDD RED 计划：

- 新增 `tests/mocks/MockStorage.hpp` 和 `tests/mocks/MockCache.hpp`，用 gMock 覆盖 `IStorage` / `ICache` 纯虚接口。
- 新增 `tests/service/service_mock_boundary_test.cpp`，用 mock 验证 `AuthService`、`ChatService`、`GroupService`、`HistoryService` 和 storage/cache/online 边界。
- 先接入测试源再构建，预期暴露当前 `liteim_tests` 尚未链接 gMock 或测试缺口。
- 随后再补 CMake sanitizer 选项、CTest labels、协议/ThreadPool/TimerHeap/TcpServer 额外边界测试。

TDD RED：

- 新增 `tests/mocks/MockStorage.hpp`、`tests/mocks/MockCache.hpp` 和 `tests/service/service_mock_boundary_test.cpp`，并接入 `tests/CMakeLists.txt`。
- 首次 `cmake --build build --target liteim_tests -j2` 按预期失败于 `fatal error: mocks/MockCache.hpp: No such file or directory`，证明新 gMock 测试已进入构建，而测试 target 还缺少 `tests/` 私有 include 根和后续 gMock 链接配置。

TDD GREEN：

- `tests/CMakeLists.txt` 为 `liteim_tests` 增加测试私有 include 根并链接 `GTest::gmock`。
- 根 `CMakeLists.txt` 增加 `LITEIM_ENABLE_SANITIZERS` 和内部 `liteim_sanitizer_flags` interface target，GNU/Clang 下启用 `-fsanitize=address,undefined`、`-fno-omit-frame-pointer` 和 `-fno-sanitize-recover=all`。
- `gtest_discover_tests()` 按 unit / MySQL integration / Redis integration / MySQL+Redis integration 拆分注册；Docker/E2E/server-signal 测试加 integration/docker 标签。GoogleTest 多标签通过 `TEST_LIST` 和 `TEST_INCLUDE_FILES` 在 CTest 阶段设置，保证 `ctest -L integration/mysql/redis/docker` 都能筛中。
- 新增 Mock 边界测试 7 个，覆盖 AuthService 登录限流/失败记录/成功清理绑定，ChatService 在线/离线收件人边界，GroupService 群存在/成员校验后保存和未读，HistoryService 权限校验后才查 history。
- 新增 FrameDecoder 参数化 split 测试、三连包 split、最大 body_len header 等边界；新增 TLV 空 body、重复字段 scalar getter、空字符串重复值和 null body；新增 ThreadPool 空队列 stop/restart；新增 TimerHeap 重复 cancel、未知 cancel 和相同 deadline 顺序。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -R "ServiceMockBoundary|FrameDecoder|TlvCodec|ThreadPool|TimerHeap|TcpServer" --output-on-failure`：78/78 通过。
- 标签检查：`ctest -N -L unit` 为 301 个普通单元测试，`-L integration` / `-L docker` 为 70 个，`-L mysql` 为 38 个，`-L redis` 为 30 个，`-L e2e` 为 6 个。

Sanitizer 验证：

- `cmake -S . -B build-asan -DLITEIM_ENABLE_SANITIZERS=ON` 首次尝试卡在重新 clone GoogleTest；改用现有 FetchContent source override 完成配置：`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/yolo/jianli/LiteIM/build/_deps/googletest-src -DFETCHCONTENT_SOURCE_DIR_SPDLOG=/home/yolo/jianli/LiteIM/build/_deps/spdlog-src`。
- `cmake --build build-asan -j2`：通过。
- 首次 `ctest --test-dir build-asan --output-on-failure` 暴露 5 个已有测试断言问题：`ErrorCodeTest.ToStringReturnsReadableNames`、`MessageTypeTest.CoreTypesReturnReadableNames`、`MessageTypeTest.UnknownTypeReturnsUnknown`、`TlvTypeTest.CoreTypesReturnReadableNames`、`TlvTypeTest.UnknownTypeReturnsUnknown`。根因是 `EXPECT_EQ(const char*, "...")` 比较指针地址而非字符串内容。
- 修正为 `EXPECT_STREQ` 后，`ctest --test-dir build-asan -R "ErrorCodeTest|MessageTypeTest|TlvTypeTest" --output-on-failure`：9/9 通过。
- `ctest --test-dir build-asan --output-on-failure`：371/371 通过。

文档同步：

- 新增 `docs/tutorials/step44_test_coverage_sanitizers.md`，保持固定 0-10 教程模板，最后一节为 `面试常见追问`。
- 更新 README，记录 Step 44 runtime、gMock 覆盖、CTest label 筛选和 sanitizer 构建命令。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和本文件，记录 Step 44 边界、RED/GREEN、标签和 sanitizer 结果。

最终验证：

- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -R "ServiceMockBoundary|FrameDecoder|TlvCodec|ThreadPool|TimerHeap|TcpServer" --output-on-failure`：78/78 通过。
- `ctest --test-dir build -L unit --output-on-failure`：301/301 通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：通过，MySQL / Redis healthy。
- `ctest --test-dir build -L integration --output-on-failure`：70/70 通过。
- `cmake --build build-asan -j2`：通过。
- `ctest --test-dir build-asan --output-on-failure`：371/371 通过。
- `cmake --build build -j2`：通过。
- `ctest --test-dir build --output-on-failure`：371/371 通过。
- `git diff --check`：通过。
- Step 44 教程标题扫描：保持 0-10，最后主章节是 `## 10. 面试常见追问`。
- 路径级 stale-route scan：未发现旧路线文件路径。
- `timeout 2s ./build/server/liteim_server || test $? -eq 124`：通过，server 启动后由 bounded smoke 发送 SIGTERM 并通过 signalfd 退出。

收尾注意：

- Step 44 提交继续排除进入本 Step 前已有的用户侧改动：`include/liteim/service/GroupService.hpp`、`include/liteim/service/HistoryService.hpp`、`src/service/GroupService.cpp`、`src/service/HistoryService.cpp`、`src/storage/GroupDao.cpp`，以及未跟踪 `build-asan-plan/`。
- 本次生成的本地构建目录 `build-asan/` 和 Python `__pycache__` 不纳入 Git。

## 2026-05-16 Step 42 Python E2E

本次进入 `Step 42：实现 Python 端到端测试`。

开始状态：

- Step 41 已提交：`feat(client): add command line im client`。
- 用户确认 MySQL / Redis 在 Docker 环境中运行；本 Step 验证会使用 `docker compose -f docker/docker-compose.yml up -d --wait`。
- 工作区仍保留 进入该任务前已有的用户侧注释改动：`include/liteim/service/GroupService.hpp`、`src/service/GroupService.cpp`、`src/storage/GroupDao.cpp`。本 Step 不应把这些改动混入提交。
- `session-catchup.py` 提示的是旧路线概念问答，不对应当前 Step 42 代码改动。

概念计划：

- 新增 `tests/e2e/` Python 测试目录。
- Python helper 实现最小 TLV 编解码、阻塞 TCP client、请求/响应匹配、push 缓冲和 server 启停。
- CTest 通过 Python `unittest` 运行 `test_auth.py`、`test_private_chat.py`、`test_group_chat.py`、`test_offline.py`、`test_heartbeat.py`、`test_backpressure.py`。
- 每个 E2E 模块启动一个 `liteim_server`，CTest 使用资源锁串行执行，避免默认 `9000` 端口冲突。
- 不修改 C++ 服务端协议、schema、Redis key 或业务 handler。

TDD RED：

- 新增 `tests/e2e/test_auth.py`、`test_private_chat.py`、`test_group_chat.py`、`test_offline.py`、`test_heartbeat.py`、`test_backpressure.py`。
- 更新 `tests/CMakeLists.txt`，用 Python `unittest` 注册 `LiteIME2E.*` CTest，并通过 `LITEIM_SERVER_BIN=$<TARGET_FILE:liteim_server>` 指向构建产物。
- `cmake --build build --target liteim_tests -j2 && ctest --test-dir build -R LiteIME2E.test_auth --output-on-failure` 按预期失败于 `ModuleNotFoundError: No module named 'tests.e2e.liteim_e2e'`，证明当前缺口是 Python E2E helper 尚未实现。

代码完成：

- 新增 `tests/e2e/liteim_e2e.py`，实现 Python `MessageType` / `TlvType`、Packet/TLV 编解码、消息字段组装、`LiteIMClient`、`LiteIMServer` 和 `E2ETestCase`。
- `LiteIMClient` 支持 register/login/heartbeat/add_friend/list_friends/private_message/create_group/join_group/group_message/history/offline 等测试 helper。
- `LiteIMServer` 默认启动 CTest 传入的 `LITEIM_SERVER_BIN`，等待 `127.0.0.1:9000` 可连接，并在测试结束时发送 SIGTERM 停止 server。
- 修正 E2E 测试细节：离线测试使用 register response 取得 receiver id；backpressure 测试在发送阶段不读取 slow receiver，避免提前释放接收缓冲。

TDD GREEN：

- `python3 -m py_compile tests/e2e/*.py`：通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：通过，MySQL / Redis healthy。
- `ctest --test-dir build -R LiteIME2E.test_auth --output-on-failure`：通过。
- `ctest --test-dir build -R LiteIME2E --output-on-failure`：6/6 通过。

文档完成：

- 更新 `README.md`：记录 Python 3、Step 42 runtime、E2E 运行命令和测试边界。
- 新增 `docs/tutorials/step42_python_e2e.md`，按固定 0-10 模板讲解 E2E 边界、接口、运行流程和测试设计。

最终验证：

- `cmake --build build -j2`：通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：通过，MySQL / Redis healthy。
- `python3 -m py_compile tests/e2e/*.py`：通过。
- `ctest --test-dir build -R LiteIME2E --output-on-failure`：通过，6/6 tests passed。
- `ctest --test-dir build --output-on-failure`：通过，344/344 tests passed。
- `git diff --check`：通过。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" docs/tutorials/step42_python_e2e.md README.md`：无输出。
- `rg -n "^## " docs/tutorials/step42_python_e2e.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径扫描无输出。
- `timeout 2s ./build/server/liteim_server || test $? -eq 124`：通过，server 监听 `0.0.0.0:9000` 后收到 SIGTERM 并通过 signalfd 退出。

收尾注意：

- Step 42 提交需要继续排除进入本 Step 前已有的用户侧注释改动：`include/liteim/service/GroupService.hpp`、`src/service/GroupService.cpp`、`src/storage/GroupDao.cpp`。

## 2026-05-15 Step 38 GroupService

本次进入 `Step 38：GroupService 群聊`，先完成概念和边界核对，再按用户确认的方案 A 进入实现。

已确认：

- `PROJECT_MEMORY.md` 的 Step 38 范围是基础群聊：建群、加群、列出我的群、发送群聊、在线 push、离线保存和未读 +1。
- 现有协议枚举和 TLV 字段已经覆盖群聊 request/response/push 以及 `GroupId` / `GroupName`。
- 现有 MySQL schema 和 `GroupDao` 已覆盖建群、加成员、查成员和按 id 查群。
- 当前缺口是 `ListGroupsRequest`：service 层需要按当前登录 user_id 查询“我的群”，但 `IStorage` 还没有 `getGroupsForUser()` 或等价接口。

实现前决策点：

- 推荐扩展 `GroupDao` / `IStorage` / `MySqlStorage`，新增 `getGroupsForUser(user_id, groups)`，让 `GroupService` 继续只依赖 storage/cache/online service 抽象。
- 该改动属于 public interface 扩展，已暂停等待用户确认后再进入 TDD RED。

用户确认采用方案 A 后进入 TDD：

- RED：新增 `StorageInterfaceTest` 对 `findGroupById()` / `getGroupsForUser()` 的接口期望。
- RED：新增 `FriendGroupDaoIntegrationTest.GetGroupsForUserReturnsOwnedAndJoinedGroups`。
- RED：新增 `tests/service/group_service_test.cpp`，覆盖建群、加群、列群、非成员发群消息失败、在线群成员 push、离线群成员 offline/unread，以及 unread 失败不导致发送方失败。
- 首次 `cmake --build build --target liteim_tests -j2` 按预期失败于 `IStorage` 缺少 `findGroupById()` / `getGroupsForUser()`、`GroupDao` 缺少 `getGroupsForUser()`，证明测试先捕获了 Step38 storage API 缺口。
- GREEN：扩展 `IStorage`、`GroupDao` 和 `MySqlStorage`，新增 `findGroupById()` / `getGroupsForUser()` storage facade，补齐所有测试 fake。
- GREEN：新增 `GroupService`，注册四个 group request handler 到 business thread，接入 `server/main.cpp`。
- 当前验证：`cmake --build build --target liteim_tests -j2` 通过；`ctest --test-dir build -R "GroupService|FriendGroupDao|StorageInterface" --output-on-failure` 18/18 通过。

已完成代码：

- `GroupDao::getGroupsForUser()` 通过 `group_members` join `chat_groups` 查询用户所在群。
- `IStorage` / `MySqlStorage` 暴露 `findGroupById()` 和 `getGroupsForUser()`，保持 service 层不直接依赖 DAO。
- `GroupService::registerHandlers()` 注册 `CreateGroupRequest`、`JoinGroupRequest`、`ListGroupsRequest` 和 `GroupMessageRequest`。
- `handleCreateGroup()` 使用当前登录 user id 作为 owner，返回 `GroupId` / `GroupName`。
- `handleJoinGroup()` 先查 group 存在，再把当前用户加入群。
- `handleListGroups()` 按当前登录用户返回重复 `GroupId` / `GroupName`。
- `handleGroupMessage()` 校验发送者是群成员，保存群消息，在线成员收到 `GroupMessagePush`，离线成员写 `offline_messages` 并 unread +1。
- Redis unread 递增失败只记录 warning，不把 MySQL 已保存的群消息变成发送失败。

已完成文档同步：

- 新增 `docs/tutorials/step38_group_service.md`。
- 更新 README、`docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。
- 没有更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的完成状态；它继续只作为长期路线/边界文件。

最终验证：

- `cmake --build build -j2`：通过。
- `ctest --test-dir build -R "GroupService|FriendGroupDao|StorageInterface" --output-on-failure`：18/18 通过。
- `ctest --test-dir build --output-on-failure`：312/312 通过。
- `git diff --check`：通过。
- `docs/tutorials/step38_group_service.md` 保持固定 0-10 章节，最后主章节是 `## 10. 面试常见追问`。
- `docs/tutorials/step*.md` 最后主章节扫描：全部仍以 `## 10. 面试常见追问` 收尾。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：server 成功监听 `0.0.0.0:9000`，timeout 发送 SIGTERM 后 signalfd 路径退出。

## 2026-05-15 Pre-Step38 Critical Hardening

本次进入 Step 38 群聊前的独立 hardening，不开 `Step 36.1`，只修两个 runtime correctness 问题：

- session close 后补上 `TcpServer -> business ThreadPool -> OnlineService::unbindSession(session_id)` 清理通道，避免只删除 `TcpServer::sessions_` 而遗留 `SessionManager` / Redis online。
- ChatService 离线消息已经保存到 MySQL `messages` / `offline_messages` 后，Redis unread 递增失败只记录 warning，不再给发送方返回失败。

TDD 过程：

- RED：新增 `SessionManagerTest.ClosedSessionStillExposesBoundUserForCloseCleanup`、`OnlineServiceTest.UnbindSessionClearsClosedCurrentBindingAndCache`、`OnlineServiceTest.OldClosedSessionUnbindSessionDoesNotClearNewRedisOnlineKey`、`OnlineServiceRedisIntegrationTest.UnbindSessionCloseCleanupClearsRedisOnlineState`、`TcpServerTest.SessionCloseCallbackReceivesRemovedSessionId` 和 `ChatServiceFixture.OfflineUnreadFailureStillReturnsSenderSuccess`。
- 首次运行 `cmake --build build --target liteim_tests -j2` 按预期失败于缺少 `TcpServer::SessionCloseCallback` / `setSessionCloseCallback()`，证明 close callback 新 API 测试先于生产代码。
- GREEN：新增 `SessionManager::getBoundUserBySession()`、`OnlineService::unbindSession()`、`TcpServer::setSessionCloseCallback()`，并在 `server/main.cpp` 把 close cleanup 投递到 business pool。
- GREEN：调整 `ChatService` unread 失败语义，MySQL 保存成功后 `cache_.incrUnread()` 失败只写 warning，仍返回 `PrivateMessageResponse`。

当前验证：

- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -R "SessionManager|OnlineService|TcpServer|ChatService" --output-on-failure`：37/37 通过。

最终验证：

- `cmake --build build -j2`：通过。
- `ctest --test-dir build --output-on-failure`：303/303 通过，本机 Docker MySQL/Redis 集成路径也通过。
- `git diff --check`：通过。
- `rg -n "^## " docs/tutorials/step32_session_manager_online_service.md docs/tutorials/step36_chat_service.md`：两篇改过的教程仍保持固定 0-10 章节，最后主章节都是 `## 10. 面试常见追问`。
- stale failure-semantics 扫描：README、Step36 教程和 planning 文件没有保留“Redis unread 失败导致发送失败”的旧说法。
- `timeout 1s ./build/server/liteim_server`：server 成功监听 `0.0.0.0:9000`，timeout 发送 SIGTERM 后 signalfd 路径退出；退出码 124 属于该 bounded smoke 的预期结果。

## 2026-05-15 Step 37 OfflineMessageService

本次进入 `Step 37：OfflineMessageService 离线消息拉取`，用户确认采用方案一：登录成功后客户端主动发送 `OfflineMessagesRequest`，服务端登录流程不额外发送 follow-up response。

TDD 过程：

- RED：新增 `tests/service/offline_message_service_test.cpp` 并接入 `tests/CMakeLists.txt`。
- 首次运行 `cmake --build build --target liteim_tests -j2` 按预期失败于缺少 `liteim/service/OfflineMessageService.hpp`，证明测试覆盖了当前 Step 缺失的服务接口。
- GREEN：新增 `include/liteim/service/OfflineMessageService.hpp`、`src/service/OfflineMessageService.cpp`，更新 `src/service/CMakeLists.txt` 和 `server/main.cpp`，让 `OfflineMessagesRequest` 通过 `MessageRouter` 进入 business `ThreadPool` handler。

当前边界：

- 本 Step 只做主动离线消息拉取、`OfflineMessagesResponse`、delivered 标记和未读清理。
- 不修改 `AuthService` / `MessageRouter` 的一请求一响应模型。
- 不实现可靠 ACK、重试、群聊、历史分页、跨节点路由。

已完成代码：

- `OfflineMessageService::registerHandlers()` 注册 `OfflineMessagesRequest` 为 `BusinessThread` handler。
- `handleOfflineMessages()` 从当前 session 查登录用户，读取可选 `Limit`，调用 `IStorage::getOfflineMessages()` 获取 pending rows，并按 service 上限截断。
- `OfflineMessagesResponse` 对每条消息重复写入 `MessageId`、`ConversationType`、`ConversationId`、`SenderId`、`ReceiverId`、`MessageText` 和 `TimestampMs`。
- 对返回批次按会话去重调用 `ICache::clearUnread()`，随后调用 `IStorage::markOfflineDelivered()` 标记本批 message_id。
- server runtime 创建 `OfflineMessageService` 并注册到同一个 `MessageRouter`。

已完成文档同步：

- 新增 `docs/tutorials/step37_offline_message_service.md`。
- 更新 README、`docs/process/task_plan.md`、`docs/process/findings.md`、本文件和 `/home/yolo/jianli/PROJECT_MEMORY.md`。
- 没有更新 AGENTS/CLAUDE 的进度状态；它们仍只保留约束和读取顺序。

最终验证：

- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -R "OfflineMessageService" --output-on-failure`：启动本地 Docker MySQL/Redis 后 6/6 通过，集成用例未跳过。
- `cmake --build build -j2`：通过。
- `ctest --test-dir build --output-on-failure`：297/297 通过。
- `git diff --check`：通过。
- 教程模板扫描：`docs/tutorials/step00` 到 `docs/tutorials/step37` 都符合固定 0-10 模板，最后主章节是 `## 10. 面试常见追问`。
- 旧教程章节、`Current Status`、旧面试章节名、行号锚点扫描：无输出。
- 真实数据例子扫描：38/38 篇教程都有“该项目代码在实际应用中的具体数据例子”。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，server 启动 MySQL / Redis pool 后监听 `0.0.0.0:9000`，收到 SIGTERM 后通过 signalfd 退出。

## 2026-05-14 Step 36 ChatService

本次进入 `Step 36：ChatService 私聊业务`，目标是在 Step 33-35 的 service runtime 基础上补齐私聊发送闭环。

Markdown 审计：

- 检查 current-facing README、AGENTS、CLAUDE 和 docs/tutorials，没有需要删除的当前错误文档。
- 修复 `docs/tutorials/step34_auth_service.md` 和 `docs/tutorials/step35_friend_service.md` 的重复边界措辞。
- 将 `docs/process/task_plan.md` 中旧 `Current Decision` 过程块改名为 `Historical Route Snapshot`，避免后续 agent 把历史快照当成当前状态。
- 清理 `docs/process/task_plan.md` 旧 Step0 kept-files 列表中已经不存在的 `docs/architecture.md`、`docs/project_layout.md` 和 `docs/tutorials/README.md`。

TDD 过程：

- RED：新增 `tests/service/chat_service_test.cpp` 并接入 `tests/CMakeLists.txt`，首次 `cmake --build build --target liteim_tests -j2` 按预期失败于缺少 `liteim/service/ChatService.hpp`。
- GREEN：新增 `include/liteim/service/ChatService.hpp`、`src/service/ChatService.cpp`，更新 `src/service/CMakeLists.txt` 和 `server/main.cpp`，让 `PrivateMessageRequest` 通过 `MessageRouter` 进入 business `ThreadPool` handler。

已完成代码：

- `ChatService::registerHandlers()` 注册 `PrivateMessageRequest` 为 `BusinessThread` handler。
- `handlePrivateMessage()` 从当前 session 查发送者，读取 `ReceiverId` / `MessageText`，校验接收方存在，生成私聊 conversation id，先落库，再在线 push 或离线未读 +1。
- 在线接收方通过 `OnlineService::getSessionByUser()` 拿当前进程 `Session`，发送 `PrivateMessagePush`。
- 离线接收方通过 `IStorage::saveMessageWithOfflineRecipients(message, {receiver_id}, saved)` 写离线记录，并通过 `ICache::incrUnread()` 增加未读数。
- sender response 和 receiver push 都携带 `MessageId`、`ConversationType`、`ConversationId`、`SenderId`、`ReceiverId`、`MessageText`、`TimestampMs`。

已完成文档同步：

- 新增 `docs/tutorials/step36_chat_service.md`。
- 更新 README、`docs/process/task_plan.md`、`docs/process/findings.md`、本文件和 `/home/yolo/jianli/PROJECT_MEMORY.md`。
- 没有更新 AGENTS/CLAUDE 的进度状态；它们仍只保留约束和读取顺序。

最终验证：

- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -R "ChatService" --output-on-failure`：6/6 通过。
- `cmake --build build -j2`：通过。
- `ctest --test-dir build --output-on-failure`：291/291 通过。
- `git diff --check`：通过。
- 教程模板扫描：`docs/tutorials/step00` 到 `docs/tutorials/step36` 都符合固定 0-10 模板，最后主章节是 `## 10. 面试常见追问`。
- 旧教程章节、`Current Status`、旧面试章节名、行号锚点扫描：无输出。
- 真实数据例子扫描：37/37 篇教程都有“该项目代码在实际应用中的具体数据例子”。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，server 启动 MySQL / Redis pool 后监听 `0.0.0.0:9000`，收到 SIGTERM 后通过 signalfd 退出。

## 2026-05-14 Step Tutorial Markdown Compact Lecture Rewrite

本次按既定计划做 Markdown-only 教程重构，范围是 `docs/tutorials/step00_reset.md` 到 `docs/tutorials/step35_friend_service.md`。

执行边界：

- 只重构 Markdown 教程结构和相关约束文字。
- 不修改 C++、SQL、CMake、测试源码或协议行为。
- 不创建 `docs/tutorials/README.md`。
- 不把本次进度、测试总数或 commit hash 写进 README。
- 保留进入任务前已有的 C++ dirty diff；`step32_session_manager_online_service.md` 基于当前工作区内容重写。

已完成：

- 先试点 `step35_friend_service.md`、`step14_session.md`、`step20_backpressure.md`，确认固定 0-10 二级章节可用。
- 将 `docs/tutorials/step00_reset.md` 到 `docs/tutorials/step35_friend_service.md` 全量重组到新模板。
- 去掉教程里的精确 `#Lxx` 行号锚点，改为文件路径或函数名语境，避免后续行号漂移。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的长期教程模板规则。
- 更新 `/home/yolo/jianli/AGENTS.md` 和 `/home/yolo/jianli/CLAUDE.md` 的教程约束文字，不写进度状态。
- 更新 `docs/process/task_plan.md` 和 `docs/process/findings.md` 记录本次 Markdown-only 重构。

验证结果：

- 教程最后主章节扫描：36/36 篇最后一个主章节都是 `## 10. 面试常见追问`。
- 旧提交章节 / Current Status 扫描：`docs/tutorials/step*.md` 和 README 无命中。
- 历史过程噪声扫描：`docs/tutorials/step*.md` 无命中。
- 真实数据例子扫描：36/36 篇都包含“该项目代码在实际应用中的具体数据例子”。
- 标题模板扫描：36/36 篇二级章节与固定 0-10 模板一致。
- 旧行号锚点和旧面试章节名扫描：`docs/tutorials/step*.md` 和 README 无 `#Lxx`、`面试时怎么讲`、`面试讲法` 残留。
- `git diff --check`：通过。

## 2026-05-14 Step 35 FriendService

本次进入 `Step 35：FriendService 好友业务`，用户已确认采用协议方案一：新增 `TlvType::OnlineStatus`，好友列表和添加好友响应里用 `uint64` 的 `1/0` 表示在线/离线。

目标边界：

- 实现 `AddFriendRequest` / `ListFriendsRequest` handler。
- 当前用户身份来自 `OnlineService::getUserBySession()`，未登录请求返回错误。
- 添加好友写 `IStorage::addFriendship()`，重复好友返回 `AlreadyExists`。
- 好友列表来自 `IStorage::getFriends()`，在线状态来自 `ICache::isUserOnline()`。
- handler 通过 `MessageRouter` 注册为 `BusinessThread`，避免在 Reactor I/O 线程执行 MySQL / Redis 阻塞调用。
- 不实现好友申请审批、黑名单、备注名、私聊、群聊、离线消息、历史消息、HeartbeatService 。

TDD 过程：

- RED：新增 `tests/service/friend_service_test.cpp` 并接入 `tests/CMakeLists.txt`，更新 `tests/protocol/tlv_type_test.cpp` 期望 `TlvType::OnlineStatus`；首次 `cmake --build build --target liteim_tests -j2` 按预期失败于缺少 `liteim/service/FriendService.hpp`。
- GREEN：新增 `TlvType::OnlineStatus`、`FriendService` header/source、server runtime 注册，并把 `FriendService.cpp` 接入 `liteim_service`。

当前验证：

- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -R "FriendService|TlvType" --output-on-failure`：8/8 通过。

已完成代码：

- 新增 `include/liteim/service/FriendService.hpp` 和 `src/service/FriendService.cpp`。
- 新增 `TlvType::OnlineStatus`，字符串名为 `ONLINE_STATUS`，好友在线状态用 `uint64` 的 `1/0` 表达。
- `FriendService::registerHandlers()` 将 `AddFriendRequest` / `ListFriendsRequest` 注册为 `BusinessThread` handler。
- `handleAddFriend()` 从当前 session 查登录用户，读取 `TargetUserId`，校验目标用户存在，重复好友返回 `AlreadyExists`，成功后写 MySQL 好友关系并返回好友公开资料和在线状态。
- `handleListFriends()` 查询当前用户好友列表，并为每个好友追加 `FriendId` / `Username` / `Nickname` / `OnlineStatus`。
- `server/main.cpp` 注入 `FriendService` 并注册好友 handler。

已完成文档同步：

- 更新 README、`docs/tutorials/step03_protocol_types.md`、新增 `docs/tutorials/step35_friend_service.md`。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md`、本文件和 `/home/yolo/jianli/PROJECT_MEMORY.md`。

最终验证：

- `cmake --build build -j2`：通过。
- `ctest --test-dir build -R "FriendService|AuthService|MessageRouter|Service|Session|TcpServer|TlvType|LiteIMServerSignal" --output-on-failure`：65/65 通过。
- `ctest --test-dir build --output-on-failure`：285/285 通过。
- `git diff --check`：通过。
- `rg -n "^## .*提交信息|^## .*Current Status|^## .*当前状态" README.md docs/tutorials/step35_friend_service.md docs/tutorials/step03_protocol_types.md`：无输出。
- `docs/tutorials/step35_friend_service.md` 最后一个主章节是 `## 6. 面试常见追问`。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，server 启动 MySQL / Redis pool 后监听 `0.0.0.0:9000`，收到 SIGTERM 后通过 signalfd 退出。

## 2026-05-14 Step 34 AuthService

本次进入 `Step 34：AuthService 注册登录`，目标是在 Step33 `MessageRouter` 骨架上接入真实注册/登录业务，并保证 MySQL / Redis 调用只发生在 business 线程池 handler 中。

TDD 过程：

- RED：新增 `tests/service/auth_service_test.cpp` 并注册到 `tests/CMakeLists.txt` 后，`cmake --build build --target liteim_tests -j2` 按预期失败于缺少 `liteim/service/AuthService.hpp`。
- GREEN：新增 `include/liteim/service/AuthService.hpp`、`src/service/AuthService.cpp`，更新 `src/service/CMakeLists.txt` 链接 OpenSSL，更新 `server/main.cpp` 注入 MySQL / Redis / OnlineService / AuthService。
- 首次 `ctest --test-dir build -R "AuthService" --output-on-failure` 有 6/7 通过，唯一失败是集成测试生成的 username 超过 `users.username VARCHAR(64)`；修正测试用户名长度后 AuthService 7/7 通过。

已完成代码：

- `AuthServiceOptions` 提供登录失败阈值、失败窗口 TTL 和当前默认 remote_ip。
- `AuthService::registerHandlers()` 最初将 `RegisterRequest` / `LoginRequest` 注册为 `BusinessThread` handler；Post-Step58 audit 已把协议里已定义的 `LogoutRequest` 也接入同一 business-thread auth handler。
- 注册流程解析 `Username` / `Password` / 可选 `Nickname`，生成 salt，使用 `PBKDF2-HMAC-SHA256` 生成密码 hash，调用 `IStorage::createUser()`，返回 `RegisterResponse`。
- 登录流程先检查 Redis 登录失败限制，再查 MySQL 用户、验证密码、失败时记录失败次数、成功时清除失败计数并调用 `OnlineService::bindUser()`。
- `server/main.cpp` 现在启动 `MySqlPool`、`RedisPool`、`MySqlStorage`、`RedisCache`、`SessionManager`、`OnlineService` 和 `AuthService`，并把注册/登录/登出 handler 接到 `MessageRouter`。

当前验证：

- `ctest --test-dir build -R "AuthService" --output-on-failure`：7/7 通过。
- `ctest --test-dir build -R "AuthService|MessageRouter|Service|Session|TcpServer|LiteIMServerSignal" --output-on-failure`：57/57 通过。
- `cmake --build build -j2`：通过。
- `ctest --test-dir build --output-on-failure`：279/279 通过。
- `git diff --check`：通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，server 启动 MySQL / Redis pool 后监听 `0.0.0.0:9000`，收到 SIGTERM 后通过 signalfd 退出。

## 2026-05-14 Step 33 MessageRouter

本次进入 `Step 33：MessageRouter 异步分发框架`，目标是在不实现 AuthService / ChatService 的前提下，把网络层收到的 Packet 接到 service 层路由骨架。

TDD 过程：

- RED：新增 `tests/service/message_router_test.cpp` 并注册到 `tests/CMakeLists.txt` 后，`cmake --build build --target liteim_tests -j2` 失败于缺少 `liteim/service/MessageRouter.hpp`。
- GREEN：新增 `include/liteim/service/MessageRouter.hpp`、`src/service/MessageRouter.cpp`，更新 `src/service/CMakeLists.txt`、`server/CMakeLists.txt` 和 `server/main.cpp`。

已完成代码：

- `MessageRouter` 支持 `route(Session::Ptr, Packet)`、`registerHandler(MessageType, Handler, DispatchMode)`、`RouterRequest` 和 `DispatchMode::Inline / BusinessThread`。
- 默认 `HeartbeatRequest` inline 返回 `HeartbeatResponse`，不启动或使用业务线程池。
- 注册 handler 的请求会按 dispatch mode 执行；业务线程 handler 通过 `ThreadPool::submit()` 执行。
- Router 统一解析 TLV，统一构造 `ErrorResponse`，并强制响应 `seq_id` 等于请求 `seq_id`。
- 未知 / 非 request / 未注册 handler / TLV parse 失败 / handler 返回错误 / 线程池拒绝任务都会返回 `ErrorResponse`。
- 异步任务通过 `weak_ptr<Session>` 做开始前和发送前检查，关闭后不发送、不崩溃。
- `server/main.cpp` 启动业务 `ThreadPool`，创建 `MessageRouter`，并通过 `TcpServer::setMessageCallback()` 接入。

当前验证：

- `ctest --test-dir build -R "MessageRouter" --output-on-failure`：8/8 通过。
- `ctest --test-dir build -R "MessageRouter|Service|Session|TcpServer" --output-on-failure`：49/49 通过。
- `cmake --build build -j2`：通过。
- 首次 `ctest --test-dir build --output-on-failure`：271/272 通过，`LiteIMServerSignalTest.TerminatesOnSigterm` 失败，server 在 SIGTERM 后以 143 退出。
- 根因：`server/main.cpp` 先启动了业务 `ThreadPool`，后启动 `SignalWatcher`；`SignalWatcher::start()` 只阻塞当前线程信号，已创建的业务线程没有继承 SIGINT/SIGTERM 阻塞掩码，SIGTERM 可能落到业务线程按默认动作终止进程。
- 修复：调整 `server/main.cpp` 启动顺序，先 `signal_watcher.start()`，再 `business_pool.start()`；退出时先 `business_pool.stop()`，再 `signal_watcher.stop()`。
- `cmake --build build --target liteim_server -j2 && ctest --test-dir build -R "LiteIMServerSignal" --output-on-failure`：通过。
- `ctest --test-dir build --output-on-failure`：272/272 通过。
- `git diff --check`：通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，server 收到 SIGTERM 后通过 signalfd 退出。

## 2026-05-14 Step 32 SessionManager / OnlineService

本次按用户确认采用“踢旧保新”的重复登录策略，并同步相关 Markdown。

TDD 过程：

- RED：新增 `tests/service/session_manager_test.cpp` 和 `tests/service/online_service_test.cpp`，并把 `liteim_service` 加到测试链接；`cmake --build build --target liteim_tests -j2` 预期失败于缺失 `liteim/service/SessionManager.hpp`。
- GREEN：新增 `include/liteim/service/SessionManager.hpp`、`src/service/SessionManager.cpp`、`include/liteim/service/OnlineService.hpp`、`src/service/OnlineService.cpp`、`src/service/CMakeLists.txt`，并在 `src/CMakeLists.txt` 注册 `service` 模块。

已完成代码：

- `SessionManager` 维护 `user_id -> weak_ptr<Session>` 和 `session_id -> user_id`，支持绑定、按用户查 session、按 session 查用户、匹配解绑、stale weak_ptr 清理。
- 重复登录时，旧 session 从内存表移除后在锁外 `close()`，新 session 成为唯一当前绑定。
- `OnlineService` 通过 `ICache` 写入、刷新、删除 Redis 在线状态，并保证旧 session 的延迟解绑不会删除新 session 的在线 key。

已完成文档同步：

- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md`、README、`docs/process/task_plan.md`、`docs/process/findings.md`、本文件，以及 Step 21 / Step 29 / Step 31 相关教程中的 Step 32 边界描述。
- 新增 `docs/tutorials/step32_session_manager_online_service.md`。

当前验证：

- `ctest --test-dir build -R "SessionManagerTest|OnlineServiceTest|OnlineServiceRedisIntegrationTest" --output-on-failure`：10/10 通过，其中 Redis 集成项因本机 Redis 不可用按规则 skipped。
- `cmake --build build -j2`：通过。
- `ctest --test-dir build --output-on-failure`：264/264 通过；MySQL / Redis 相关集成项因本机服务不可用按规则 skipped。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，server 启动后收到 timeout SIGTERM 并通过 signalfd 退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧路线 `server/net` / `server/protocol` / `SQLite` / `InMemory` 路径扫描：无输出。
- 教程结构扫描：`docs/tutorials/step00` 到 `docs/tutorials/step32` 的最后一个主章节都是“面试常见追问”；教程内无 `提交信息` 主章节；Step 32 含“该项目代码在实际应用中的具体数据例子”。

错误记录：

- 一次文案扫描命令把带反引号的 pattern 放进双引号，shell 执行了命令替换并输出 `SessionManager: command not found`。已改用单引号重新执行，确认没有旧 Step 32 / Pre-Step 31 表述残留。

## 2026-05-13 Step 31 Route Documentation Alignment

本次按用户确认做 Markdown-only 路线调整，不修改 C++ 代码逻辑，不回滚现有 dirty diff。

已完成文档修复：

- 将原独立存储/缓存契约小重构正式纳入 `Step 31：MySqlStorage 和 RedisCache 聚合适配层`。
- 将原 Step 31 `SessionManager and OnlineService` 后移为 Step 32；当前后续路线为 Step 32-40 业务、Step 41-44 工具验证、Step 45-52 Qt、Step 53。
- 新增 `docs/tutorials/step31_storage_cache_adapters.md`，按固定教程结构讲解 `MySqlStorage : IStorage`、`RedisCache : ICache`、事务边界、Redis 缓存边界、测试和面试追问。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md`、README、`docs/process/task_plan.md`、`docs/process/findings.md`、本文件，以及 Step 21 / Step 23 / Step 26 教程里的旧编号和 Pre-Step 说法。

当前验证：

- 残留旧路线扫描：无旧 Pre-Step 表述、无旧 Step 31 业务层表述、无旧阶段区间编号命中。
- 教程结构扫描：Step 00-31 的最后一个主章节都包含“面试常见追问”；教程内无提交信息章节。
- `git -C /home/yolo/jianli/LiteIM diff --check -- README.md docs/process/task_plan.md docs/process/findings.md docs/process/progress.md docs/tutorials/step21_storage_cache_interfaces.md docs/tutorials/step23_mysql_connection.md docs/tutorials/step26_message_dao.md docs/tutorials/step31_storage_cache_adapters.md`：通过。
- 触碰文件 trailing whitespace 扫描：无命中。

## 2026-05-12 Markdown Contract Alignment / Doc Drift Fix

本次只做 Markdown / 项目记忆同步，不修改 C++ 代码逻辑，不回滚现有 dirty diff，也不作为 Step 31。

已完成文档修复：

- `/home/yolo/jianli/PROJECT_MEMORY.md` 的教程结构约束改为 `概念 -> hpp 接口说明 -> 作用场景和运行流程 -> 测试 -> 面试常见追问`；教程文件不再维护“提交信息”章节。
- `docs/tutorials/step00_reset.md` 到 `docs/tutorials/step30_unread_login_cache.md` 的运行流程第 5 小节统一改为“该项目代码在实际应用中的具体数据例子”，并改成带 LiteIM 真实数据的场景说明。
- 移除教程里的“提交信息 / 本 Step 提交信息”章节，补齐缺失的“面试常见追问”，并确保它是每个教程最后的主章节。
- 同步 MySQL history 索引字段名为 `(conversation_type, conversation_id, message_id)`。
- 同步 HeartbeatService 与 `Session::last_active_time` 分工：完整合法入站 Packet 在 `Session` 读路径刷新连接活跃时间，HeartbeatService 只返回响应并为已登录用户刷新 Redis 在线 TTL。
- 同步 `saveMessageWithOfflineRecipients()` 当前契约：重复离线用户先去重；`queryLastInsertedMessage()` 成功后、`COMMIT` 前失败会 `ROLLBACK` 并清空 `saved_message`。
- 更新 `docs/process/task_plan.md` 和 `docs/process/findings.md`，把本轮记为 Markdown contract alignment / doc drift fix，不记成 Step 31。

当前验证：

- Markdown 结构扫描：教程无提交信息章节；每个 Step 都包含“面试常见追问”和“该项目代码在实际应用中的具体数据例子”；每个教程最后一个主章节都是“面试常见追问”。
- 漂移扫描：`PROJECT_MEMORY.md` 无旧 `(conv_type, conv_id, id)` / `conv_id` / `conv_type`；当前教程和 PROJECT_MEMORY 无 HeartbeatService 直接刷新 `Session::last_active_time` 的实现描述；README / AGENTS / CLAUDE 无 `Current Status` / `当前状态`。
- `git diff --check`：通过。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build --output-on-failure`：254/254 通过，其中新增通过项为 `SaveMessageWithOfflineRecipientsDeduplicatesOfflineUsers`。

## 2026-05-12 Step 31 Storage / Cache Adapter Layer

本次按用户确认将原独立存储/缓存契约小重构纳入正式 Step 31 教学路线，不实现 `SessionManager` / `OnlineService`；业务层从 Step 32 开始。

目标边界：

- 补 `MySqlStorage : IStorage` 和 `RedisCache : ICache`，让 Step21 的抽象有真实可注入实现。
- 将好友列表返回类型改成公开资料 DTO，避免把认证字段带到业务响应边界。
- 给 `PreparedStatement` 补 `bindUInt64()`，对齐 MySQL `BIGINT UNSIGNED` schema。
- 补 MySQL 消息保存 + 离线记录保存的统一事务入口，避免消息落库成功但离线投递记录失败。
- 补未读计数 delta 的 Redis signed-int 范围校验，并把 LoginRateLimiter 的滑动失败窗口语义写清楚。

当前状态：

- 已完成项目总设计、planning 文件和现有 dirty diff 检查。
- 已确认已有 dirty diff 只在 `MySqlConnection` 注释附近；后续如果触碰该文件，会保留既有变更，不无意回滚。
- TDD RED：新增/更新测试后首次 `cmake --build build --target liteim_tests -j2` 按预期失败，错误集中在缺少 `UserProfileRecord`、`IStorage::saveMessageWithOfflineRecipients()` 和 `PreparedStatement::bindUInt64()`。

已完成代码：

- 新增 `include/liteim/storage/MySqlStorage.hpp` 和 `src/storage/MySqlStorage.cpp`，实现 `MySqlStorage : IStorage`。
- 新增 `include/liteim/cache/RedisCache.hpp` 和 `src/cache/RedisCache.cpp`，实现 `RedisCache : ICache`。
- 新增 `UserProfileRecord`，并将 `IStorage::getFriends()` / `FriendDao::getFriends()` 改为返回公开资料，不返回 `password_hash` / `password_salt`。
- 新增 `PreparedStatement::bindUInt64()`，DAO 的 `BIGINT UNSIGNED` id 绑定改用 unsigned bind。
- 新增 `MySqlStorage::saveMessageWithOfflineRecipients()`，在同一 MySQL 事务内写 `messages` 和 `offline_messages`；Redis 未读数仍由后续业务层在 MySQL commit 成功后处理。
- `UnreadCounter::incrUnread()` 拒绝超过 Redis signed 64-bit integer 范围的 delta。

新增/更新测试：

- 更新 `StorageInterfaceTest`、`FriendGroupDaoIntegrationTest`、`MySqlIntegrationTest` 和 Step30 invalid-input 测试。
- 新增 `MySqlStorageIntegrationTest`，覆盖真实 `IStorage` 适配、公开好友资料、消息 + 离线记录同事务提交和离线插入失败回滚。
- 新增 `RedisCacheIntegrationTest`，覆盖真实 `ICache` 聚合在线状态、未读计数和登录失败限制。

已完成文档同步：

- 更新 README、Step21/23/25/26/27/30/31 教程、`docs/process/task_plan.md`、`docs/process/findings.md`、本文件和 `/home/yolo/jianli/PROJECT_MEMORY.md`。
- 明确 `LoginRateLimiter` 当前是滑动失败窗口，`allow()` / `recordFailure()` 分离，后续 AuthService 如需强原子登录门禁再扩展 Lua 脚本。
- 明确当前 v1 MySQL schema 没有 `用户身份类型列`，如果未来需要数据库级 human/agent 区分，应单独做迁移。

当前验证：

- RED：`cmake --build build --target liteim_tests -j2` 首次按预期失败。
- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -R "StorageInterfaceTest|MySqlConnection|FriendGroupDao|MySqlStorage|UnreadCounter|LoginRateLimiter|RedisCache|CacheInterfaceTest" --output-on-failure`：25/25 通过。
- `ctest --test-dir build -R "MySqlIntegrationTest|MessageDaoIntegrationTest" --output-on-failure`：10/10 通过。
- `cmake --build build -j2`：通过。
- `ctest --test-dir build --output-on-failure`：254/254 通过，新增通过项来自 `SaveMessageWithOfflineRecipientsDeduplicatesOfflineUsers`。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过。
- `git diff --check`：通过。

## 2026-05-11 Step 30 UnreadCounter / LoginRateLimiter

本次正式开启 `Step 30：实现 UnreadCounter 和 LoginRateLimiter`。

概念边界：

- `UnreadCounter` 只管理 Redis 未读计数 key，提供递增、读取和清零。
- `LoginRateLimiter` 只管理 Redis 登录失败计数 key，提供是否允许、记录失败和清除失败计数。
- 本 Step 不接入 AuthService、ChatService、TcpServer、Session 或任何运行时业务流程。
- MySQL/Redis 仍然只能在后续业务线程中调用，不能放进 Reactor I/O 线程。

已完成测试 RED：

- 新增 `tests/cache/unread_login_cache_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 30 测试。
- RED：新增测试后首次 `cmake --build build --target liteim_tests` 按预期失败，错误为缺少 `liteim/cache/LoginRateLimiter.hpp`。

已完成代码：

- 新增 `include/liteim/cache/UnreadCounter.hpp` 和 `src/cache/UnreadCounter.cpp`。
- 新增 `include/liteim/cache/LoginRateLimiter.hpp` 和 `src/cache/LoginRateLimiter.cpp`。
- `src/cache/CMakeLists.txt` 接入 Step 30 cache 源文件。
- `UnreadCounter` 通过 Redis key 维护用户在私聊/群聊会话中的未读数，支持递增、读取和清零。
- `LoginRateLimiter` 通过 Redis failure key + TTL 维护用户名和远端地址维度的登录失败窗口，支持允许判断、记录失败和清除计数。

新增测试：

- `UnreadCounterTest.RejectsInvalidInputBeforeBorrowingRedis`
- `LoginRateLimiterTest.RejectsInvalidInputBeforeBorrowingRedis`
- `Step30CacheIntegrationTest.UnreadCounterIncrementsReadsAndClearsCount`
- `Step30CacheIntegrationTest.UnreadCounterSeparatesUsersAndConversations`
- `Step30CacheIntegrationTest.LoginRateLimiterRejectsAfterFailureThresholdAndClearAllowsAgain`
- `Step30CacheIntegrationTest.LoginRateLimiterTtlExpiryAllowsAgain`
- `Step30CacheIntegrationTest.LoginRateLimiterSeparatesUsernameAndRemoteIp`

已完成文档同步：

- README 更新 cache 模块说明、本地 Redis 说明和 storage/cache 验证命令。
- 新增 `docs/tutorials/step30_unread_login_cache.md`。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。

当前验证：

- RED：新增测试后首次 `cmake --build build --target liteim_tests` 按预期失败，错误为缺少 `liteim/cache/LoginRateLimiter.hpp`。
- `cmake --build build --target liteim_tests`：通过。
- `ctest --test-dir build -R "UnreadCounterTest|LoginRateLimiterTest|Step30CacheIntegrationTest" --output-on-failure`：7/7 通过。
- `cmake --build build`：通过。
- `ctest --test-dir build -R "Redis|OnlineStatusCache|UnreadCounter|LoginRateLimiter|Step30CacheIntegrationTest" --output-on-failure`：29/29 通过。
- `ctest --test-dir build --output-on-failure`：246/246 通过。

## 2026-05-11 Step 21-29 Tutorial Format Alignment

本次按已有计划修正 `docs/tutorials/step21_storage_cache_interfaces.md` 到 `docs/tutorials/step29_online_status_cache.md` 的 Markdown 教程结构。

已完成：

- Step 21 补齐独立概念章节，扩写 `StorageTypes.hpp`、`IStorage.hpp`、`CacheTypes.hpp` 和 `ICache.hpp` 的 DTO、接口、失败语义、线程边界和后续实现边界。
- Step 22 改成 Docker Compose / SQL 脚本契约说明，补 MySQL/Redis 服务契约、schema、seed 数据、Redis 空实例边界和标准运行流程。
- Step 23-29 全部扩写到详细接口说明和两层运行流程：既说明它们在 LiteIM 业务架构里的上下游位置，也说明模块自身内部如何运行。
- 每个 Step 都补齐测试设计、验证命令、面试说法和面试常见追问章节。
- 本次只修改 Step 21-29 教程和 planning 记录，不修改 `docs/tutorials/step20_backpressure.md`，也不改 C++/SQL 行为。

当前验证：

- 教程结构扫描：Step 21-29 均包含接口/契约说明、作用场景和运行流程、验证命令、面试常见追问；运行流程章节均包含 5 个固定小节。
- `git diff --check`：通过。
- `cmake --build build`：通过。
- `ctest --test-dir build -R "StorageInterfaceTest|CacheInterfaceTest|MySql|UserDao|MessageDao|FriendGroupDao|Redis|OnlineStatusCache" --output-on-failure`：60/60 通过。
- `ctest --test-dir build --output-on-failure`：237/237 通过。

## 2026-05-11 Step 29 OnlineStatusCache

本次进入 `Step 29：实现 OnlineStatusCache`，目标是在 Step 28 `RedisClient` / `RedisPool` 之上提供 Redis TTL 在线状态缓存。

概念边界：

- `OnlineStatusCache` 只管理 `online:user:<user_id>` 在线状态 key。
- 在线状态保存 `user_id`、`session_id`、`server_id` 和 `last_active_time_ms`。
- 本 Step 不实现未读计数、登录失败限制、业务 service、SessionManager、OnlineService，也不修改 `TcpServer` / `Session` 运行时行为。

已完成测试 RED：

- 新增 `tests/cache/online_status_cache_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 29 测试。
- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/cache/OnlineStatusCache.hpp`。

已完成代码：

- 新增 `include/liteim/cache/OnlineStatusCache.hpp`。
- 新增 `src/cache/OnlineStatusCache.cpp`。
- `src/cache/CMakeLists.txt` 接入 `OnlineStatusCache.cpp`。
- `OnlineStatusCache` 通过 `RedisPool::acquire()` 借 Redis 连接，使用 `SETEX` 上线写 TTL、`EXPIRE` 心跳刷新 TTL、`DEL` 下线删除、`GET` 查询和解析 session。

新增测试：

- `OnlineStatusCacheTest.HeaderIsSelfContained`
- `OnlineStatusCacheIntegrationTest.SetUserOnlineMakesUserQueryable`
- `OnlineStatusCacheIntegrationTest.ServerIdMayContainColon`
- `OnlineStatusCacheIntegrationTest.RefreshUserOnlineExtendsTtl`
- `OnlineStatusCacheIntegrationTest.SetUserOfflineRemovesOnlineSession`
- `OnlineStatusCacheIntegrationTest.TtlExpiryMakesUserOffline`
- `OnlineStatusCacheIntegrationTest.RefreshMissingUserReturnsNotFound`

已完成文档同步：

- README 更新 cache 模块说明、本地 Redis 说明和 storage/cache 测试命令。
- 新增 `docs/tutorials/step29_online_status_cache.md`。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。

当前验证：

- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/cache/OnlineStatusCache.hpp`。
- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "OnlineStatusCache" --output-on-failure`：7/7 通过。
- `ctest --test-dir build --output-on-failure`：237/237 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(cache): add redis online status cache
```

## 2026-05-11 Step 28 RedisClient / RedisPool

本次进入 `Step 28：实现 RedisClient 和 RedisPool`，目标是在 Step 21 cache 接口和 Step 22 Docker Redis 之上提供 hiredis 阻塞客户端与固定连接池。

概念边界：

- `RedisClient` 只封装 Redis 连接、认证、DB 选择和基础命令。
- `RedisPool` 只负责固定数量 Redis 连接、线程安全 `acquire(timeout, guard)`、显式 `release()`、RAII 归还和失效重连。
- 本 Step 不实现在线状态 cache、未读数、登录失败限制、不接入业务 service、不修改 `TcpServer` / `Session` 运行时行为。

已完成测试 RED：

- 新增 `tests/cache/redis_client_pool_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 28 测试。
- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/cache/RedisClient.hpp`。

已完成代码：

- 新增 `include/liteim/cache/RedisClient.hpp`。
- 新增 `include/liteim/cache/RedisPool.hpp`。
- 新增 `src/cache/RedisClient.cpp`。
- 新增 `src/cache/RedisPool.cpp`。
- `src/cache/CMakeLists.txt` 从 header-only interface target 升级为静态库，并通过 `pkg-config hiredis` 链接 hiredis。

新增测试：

- `RedisClientTest.HeaderIsSelfContained`
- `RedisClientTest.UnavailableRedisReturnsErrorStatus`
- `RedisPoolTest.RejectsZeroPoolSize`
- `RedisIntegrationTest.ConnectsAndPingsLocalRedis`
- `RedisIntegrationTest.SetexAndGetRoundTripValue`
- `RedisIntegrationTest.GetMissingKeyReturnsEmptyOptional`
- `RedisIntegrationTest.ExpireRefreshesTtl`
- `RedisIntegrationTest.IncrReturnsIncrementedInteger`
- `RedisIntegrationTest.DelRemovesExistingKey`
- `RedisIntegrationTest.EvalCanReadRedisKey`
- `RedisPoolIntegrationTest.AcquiresConnectedRedisClient`
- `RedisPoolIntegrationTest.AcquireTimesOutWhenAllClientsAreBorrowed`
- `RedisPoolIntegrationTest.ReleaseReturnsClientToPool`
- `RedisPoolIntegrationTest.MultipleThreadsAcquireAndReleaseClients`
- `RedisPoolIntegrationTest.ReconnectsClientThatWasClosedWhileBorrowed`

已完成文档同步：

- README 更新 cache 模块说明、hiredis 构建依赖和 Redis 集成测试说明。
- 新增 `docs/tutorials/step28_redis_client_pool.md`。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。

当前验证：

- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/cache/RedisClient.hpp`。
- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "RedisClientTest|RedisIntegrationTest|RedisPoolTest|RedisPoolIntegrationTest" --output-on-failure`：15/15 通过。
- `ctest --test-dir build --output-on-failure`：230/230 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(cache): implement redis client and pool
```

## 2026-05-11 Step 27 FriendDao / GroupDao

本次进入 `Step 27：实现 FriendDao 和 GroupDao`，目标是在 Step 23/24/25 的 MySQL wrapper、连接池和 users DAO 之上提供好友表和群组表 DAO。

概念边界：

- `FriendDao` 只做 `friendships` 表的双向好友关系写入和好友列表查询。
- `GroupDao` 只做 `chat_groups` / `group_members` 表的建群、成员增删查和按 id 查群。
- 本 Step 不做好友申请审批、群管理员/禁言/公告、Redis 缓存、不接入业务 service、不修改 `TcpServer` / `Session` 运行时行为。

已完成测试 RED：

- 新增 `tests/storage/friend_group_dao_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 27 测试。
- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/storage/FriendDao.hpp`。

已完成代码：

- 新增 `include/liteim/storage/FriendDao.hpp`。
- 新增 `include/liteim/storage/GroupDao.hpp`。
- 新增 `src/storage/FriendDao.cpp`。
- 新增 `src/storage/GroupDao.cpp`。
- `src/storage/CMakeLists.txt` 接入 Step 27 DAO 源文件。

新增测试：

- `FriendGroupDaoTest.HeadersAreSelfContained`
- `FriendGroupDaoIntegrationTest.AddFriendshipCreatesBidirectionalRelationship`
- `FriendGroupDaoIntegrationTest.RepeatedAddFriendshipDoesNotCreateDuplicates`
- `FriendGroupDaoIntegrationTest.CreateGroupPersistsGroupAndOwnerMembership`
- `FriendGroupDaoIntegrationTest.AddGroupMemberIsIdempotent`
- `FriendGroupDaoIntegrationTest.RemoveGroupMemberRemovesNormalMember`
- `FriendGroupDaoIntegrationTest.FindMissingGroupReturnsNotFound`

已完成文档同步：

- README 更新 storage 模块说明和 MySQL storage 测试说明。
- 新增 `docs/tutorials/step27_friend_group_dao.md`。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。

当前验证：

- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "FriendGroupDaoTest|FriendGroupDaoIntegrationTest" --output-on-failure`：7/7 通过。
- `ctest --test-dir build --output-on-failure`：215/215 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(storage): implement friend and group dao
```

## 2026-05-11 Step 26 MessageDao / OfflineMessageDao

本次进入 `Step 26：实现 MessageDao 和 OfflineMessageDao`，目标是在 Step 23/24/25 的 MySQL wrapper、连接池和 users DAO 之上提供消息表和离线消息表 DAO。

概念边界：

- `MessageDao` 只做 `messages` 表私聊/群聊消息落库和会话历史分页查询，消息落库的 insert + 查回记录在同一事务内完成。
- `OfflineMessageDao` 只做 `offline_messages` 表待投递记录保存、pending 拉取和 delivered 标记。
- 本 Step 不做 Redis 未读数、不接入业务 service、不修改 `TcpServer` / `Session` 运行时行为。

已完成测试 RED：

- 新增 `tests/storage/message_dao_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 26 测试。
- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/storage/MessageDao.hpp`。

已完成代码：

- 新增 `include/liteim/storage/MessageDao.hpp`。
- 新增 `include/liteim/storage/OfflineMessageDao.hpp`。
- 新增 `src/storage/MessageDao.cpp`。
- 新增 `src/storage/OfflineMessageDao.cpp`。
- `src/storage/CMakeLists.txt` 接入 Step 26 DAO 源文件。
- `MySqlConnection` 新增 `executeSimple()`，用于 `START TRANSACTION`、`COMMIT`、`ROLLBACK` 这类无参数控制语句。

新增测试：

- `MessageDaoTest.HeadersAreSelfContained`
- `MessageDaoIntegrationTest.SavePrivateMessagePersistsRecord`
- `MessageDaoIntegrationTest.SaveGroupMessagePersistsRecord`
- `MessageDaoIntegrationTest.OfflineMessageSaveFetchAndDeliveredFlow`
- `MessageDaoIntegrationTest.HistoryReturnsNewestMessagesBeforeCursor`
- `MessageDaoIntegrationTest.HistoryLimitIsCappedAtFifty`

已完成文档同步：

- README 更新 storage 模块说明和 MySQL storage 测试说明。
- 新增 `docs/tutorials/step26_message_dao.md`。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。

当前验证：

- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "MessageDaoTest|MessageDaoIntegrationTest" --output-on-failure`：6/6 通过。
- `ctest --test-dir build --output-on-failure`：208/208 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(storage): implement message dao and offline messages
```

## 2026-05-11 Historical Step 25 UserDao / AuthDao

Post-Step58 note: this section records the original Step 25 implementation history. The current first-version code has removed AuthDao and keeps users-table access in UserDao.

本次进入 `Step 25：实现 UserDao 和 AuthDao`，目标是在 Step 23/24 的 MySQL wrapper 和连接池之上提供 users 表 DAO。

概念边界：

- `UserDao` 只做用户创建和按 username/id 查询。
- `AuthDao` 当前只做 username 存在性查询。
- DAO 不做注册/登录业务判断、不做密码校验、不接入网络层或 Redis。

已完成代码：

- 新增 `include/liteim/storage/UserDao.hpp`。
- 新增 `include/liteim/storage/AuthDao.hpp`。
- 新增 `src/storage/UserDao.cpp`。
- 新增 `src/storage/AuthDao.cpp`。
- `src/storage/CMakeLists.txt` 接入 DAO 源文件。
- `ErrorCode` 新增 `AlreadyExists`。
- `PreparedStatement` 新增 `lastErrorNumber()`，用于 DAO 识别 MySQL duplicate key errno。
- 新增 `tests/storage/user_dao_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 25 测试。

新增测试：

- `UserDaoTest.HeadersAreSelfContained`
- `UserDaoIntegrationTest.CreateUserPersistsAndReturnsCreatedRecord`
- `UserDaoIntegrationTest.CreateUserWorksWithSingleConnectionPool`
- `UserDaoIntegrationTest.DuplicateUsernameReturnsAlreadyExists`
- `UserDaoIntegrationTest.FindUserByUsernameReturnsCreatedUser`
- `UserDaoIntegrationTest.FindUserByIdReturnsCreatedUser`
- `UserDaoIntegrationTest.FindMissingUserReturnsNotFound`
- `UserDaoIntegrationTest.UsernameExistsReportsExistingAndMissingUsers`

已完成文档同步：

- README 更新 storage 模块说明和 MySQL storage 测试说明。
- 新增 `docs/tutorials/step25_user_dao.md`。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。

当前验证：

- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/storage/AuthDao.hpp`。
- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "UserDaoTest|UserDaoIntegrationTest|ErrorCodeTest" --output-on-failure`：9/9 通过。
- `ctest --test-dir build --output-on-failure`：202/202 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(storage): implement user dao
```

## 2026-05-11 Step 24 MySqlPool / ConnectionGuard

本次进入 `Step 24：实现 MySqlPool 和 ConnectionGuard`，目标是在 Step 23 单连接封装之上提供业务线程可复用的固定 MySQL 连接池。

概念边界：

- `MySqlPool` 负责固定数量连接、线程安全 `acquire(timeout, guard)`、连接失效重连和关闭。
- `ConnectionGuard` 负责借出连接的 RAII 自动归还。
- 本 Step 不实现 DAO、AuthService、ChatService、Redis client，也不让网络 I/O 线程访问 MySQL。

已完成代码：

- 新增 `include/liteim/storage/MySqlPool.hpp`。
- 新增 `src/storage/MySqlPool.cpp`。
- `src/storage/CMakeLists.txt` 接入 `MySqlPool.cpp`。
- 新增 `tests/storage/mysql_pool_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 24 测试。

新增测试：

- `MySqlPoolTest.HeaderIsSelfContained`
- `MySqlPoolTest.RejectsZeroPoolSize`
- `MySqlPoolIntegrationTest.AcquiresConnectedConnection`
- `MySqlPoolIntegrationTest.AcquireTimesOutWhenAllConnectionsAreBorrowed`
- `MySqlPoolIntegrationTest.ConnectionGuardReturnsConnectionOnDestruction`
- `MySqlPoolIntegrationTest.CloseMakesAcquireFail`
- `MySqlPoolIntegrationTest.MultipleThreadsAcquireAndReleaseConnections`
- `MySqlPoolIntegrationTest.ReconnectsConnectionThatWasClosedWhileBorrowed`

已完成文档同步：

- README 更新 storage 模块说明和 MySQL storage 测试说明。
- 新增 `docs/tutorials/step24_mysql_pool.md`。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。

当前验证：

- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/storage/MySqlPool.hpp`。
- `cmake --build build`：通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `ctest --test-dir build -R "MySqlPoolTest|MySqlPoolIntegrationTest" --output-on-failure`：8/8 通过。
- `ctest --test-dir build --output-on-failure`：194/194 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(storage): implement mysql connection pool
```

## 2026-05-11 Step 23 MySqlConnection / PreparedStatement

本次进入 `Step 23：实现 MySqlConnection 和 PreparedStatement`，目标是封装 MySQL C API，为后续连接池和 DAO 层提供最底层的连接与 prepared statement 能力。

概念边界：

- `MySqlConnection` 只负责单连接 RAII、连接、ping 和关闭。
- `PreparedStatement` 只负责 SQL prepare、参数绑定、执行更新和执行查询。
- `MySqlQueryResult` 用输出参数保存列名和行数据，不引入 `Result<T>`。
- 本 Step 不实现连接池、DAO、AuthService、ChatService、Redis client，也不让网络 I/O 线程访问 MySQL。

已完成代码：

- 新增 `include/liteim/storage/MySqlConnection.hpp`。
- 新增 `src/storage/MySqlConnection.cpp`。
- `src/storage/CMakeLists.txt` 从 header-only interface target 改为静态库，并通过 `pkg-config mysqlclient` 链接 MySQL C API。
- 新增 `tests/storage/mysql_connection_test.cpp`。
- `tests/CMakeLists.txt` 接入 Step 23 测试。

新增测试：

- `MySqlConnectionTest.HeaderIsSelfContained`
- `MySqlIntegrationTest.ConnectsAndPingsLocalMySql`
- `MySqlIntegrationTest.PreparedStatementExecutesSimpleSelect`
- `MySqlIntegrationTest.ExecuteUpdateAndQueryRoundTripSpecialCharacters`
- `MySqlIntegrationTest.InvalidSqlReturnsErrorStatus`

已完成文档同步：

- README 增加 MySQL C API wrapper、mysqlclient 构建依赖和 MySQL 集成测试说明。
- 新增 `docs/tutorials/step23_mysql_connection.md`。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。

验证：

- RED：新增测试后首次 `cmake --build build` 按预期失败，错误为缺少 `liteim/storage/MySqlConnection.hpp`。
- `cmake --build build`：通过。
- `ctest --test-dir build -R "MySqlConnectionTest|MySqlIntegrationTest|StorageInterfaceTest" --output-on-failure`：7/7 通过。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 8.0 / Redis 7.2 容器均 healthy。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `ctest --test-dir build --output-on-failure`：186/186 通过。
- `git diff --check -- README.md docs/process/task_plan.md docs/process/findings.md docs/process/progress.md docs/tutorials/step23_mysql_connection.md tests/CMakeLists.txt tests/storage/mysql_connection_test.cpp include/liteim/storage/MySqlConnection.hpp src/storage/CMakeLists.txt src/storage/MySqlConnection.cpp`：通过。
- `ldd build/tests/liteim_tests | rg -n "mysql|maria|ssl|crypto|anaconda"`：确认链接到系统 `/lib/x86_64-linux-gnu/libmysqlclient.so.21`，没有链接 Anaconda mysqlclient。

提交信息：

```text
feat(storage): add mysql connection and prepared statement
```

## 2026-05-11 Step 22 Local Dev MySQL 8.0 / Password Alignment

本次在已完成的 Step 22 基础上更新本地开发依赖，不进入 Step 23。

已完成修改：

- `docker/docker-compose.yml`：MySQL 镜像改为 `mysql:8.0`；MySQL `liteim` 用户、MySQL root 用户、Redis 认证密码默认统一为 `6`；Redis 启动时开启 `requirepass`。
- `Config::defaults()`：MySQL 默认端口改为 `33060`、密码 `6`；Redis 默认端口改为 `63790`、密码 `6`。
- `tests/base/config_test.cpp`：补充默认 MySQL / Redis 开发端口和密码断言。
- `README.md`、`docs/tutorials/step22_docker_mysql.md`、`docs/process/task_plan.md`、`docs/process/findings.md`：同步 MySQL 8.0、密码 `6`、Redis 认证和 `33060` 非 MySQL X Protocol 的说明。

验证：

- `docker compose -f docker/docker-compose.yml config`：通过。
- `docker compose -f docker/docker-compose.yml down -v && docker compose -f docker/docker-compose.yml up -d --wait`：重建本地开发数据卷并启动成功，MySQL / Redis 均 healthy。
- `docker compose -f docker/docker-compose.yml ps`：MySQL 镜像为 `mysql:8.0`，端口仍为 `127.0.0.1:33060->3306`；Redis 端口为 `127.0.0.1:63790->6379`。
- MySQL 查询验证：`SELECT VERSION()` 返回 `8.0.46`；`SHOW TABLES` 返回 6 张表；seed 可查到 `alice`、`bob` 和待投递离线消息。
- Redis 认证验证：未带密码 `redis-cli ping` 返回 `NOAUTH Authentication required.`；`REDISCLI_AUTH=6 redis-cli ping` 返回 `PONG`。
- MySQL root 账号验证：`mysql -uroot -p6 -e "SELECT VERSION()"` 返回 `8.0.46`。
- MySQL Workbench keyring 中 `LiteIM Docker Local` 的 `liteim@127.0.0.1:33060` 密码已更新为 `6`。
- `cmake --build build`：通过。
- `ctest --test-dir build -R "ConfigTest" --output-on-failure`：7/7 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `ctest --test-dir build --output-on-failure`：181/181 通过。
- `git diff --check -- docker/docker-compose.yml include/liteim/base/Config.hpp tests/base/config_test.cpp README.md docs/tutorials/step22_docker_mysql.md docs/process/task_plan.md docs/process/findings.md docs/process/progress.md`：通过。

## 2026-05-10 Step 22 Docker Compose / MySQL Init SQL

本次进入 `Step 22：编写 Docker Compose 和 MySQL 初始化 SQL`，目标是为后续 MySQL / Redis 存储缓存实现准备可重复启动的本地开发环境。

概念边界：

- Docker Compose 只负责启动本地 MySQL / Redis。
- MySQL init SQL 只建主线表和索引。
- seed SQL 只写本地测试数据。
- 本 Step 不实现 C++ MySQL/Redis client、DAO、连接池、业务服务，也不让网络 I/O 线程执行阻塞存储调用。

已完成文件：

- 新增 `docker/docker-compose.yml`。
- 新增 `scripts/init_mysql.sql`。
- 新增 `scripts/seed_test_data.sql`。
- 新增 `docs/tutorials/step22_docker_mysql.md`。
- 更新 `README.md`，加入 MySQL / Redis 本地开发说明和目录布局。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。

MySQL schema：

- `users`
- `friendships`
- `chat_groups`
- `group_members`
- `messages`
- `offline_messages`

seed 数据：

- 用户：`alice`、`bob`。
- 群组：`dev_group`。
- 示例私聊、群聊消息和 pending offline message 记录。

验证：

- `docker compose -f docker/docker-compose.yml config`：通过。
- `LITEIM_MYSQL_PORT=33306 LITEIM_REDIS_PORT=36379 docker compose -p liteim-step22-verify -f docker/docker-compose.yml up -d --wait`：MySQL / Redis 均 healthy。
- MySQL 查询验证：`SHOW TABLES` 返回 6 张表；`SHOW INDEX FROM messages` 包含 `idx_messages_history`、`idx_messages_sender`、`idx_messages_receiver`；seed 可查到 `alice`、`bob`、`dev_group` 和待投递离线消息。
- 后续本地开发口径更新：MySQL 开发镜像固定为 `mysql:8.0`，避免 Workbench 8.0 对 MySQL 8.4 弹兼容性警告；MySQL/Redis 开发密码统一为 `6`。
- Redis 验证：`REDISCLI_AUTH=6 redis-cli ping` 返回 `PONG`。
- `LITEIM_MYSQL_PORT=33306 LITEIM_REDIS_PORT=36379 docker compose -p liteim-step22-verify -f docker/docker-compose.yml down -v`：临时验证容器和数据卷已清理。
- `cmake --build build`：通过。
- `ctest --test-dir build --output-on-failure`：181/181 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。

提交信息：

```text
feat(infra): add mysql redis docker environment
```

## 2026-05-10 Step 21 Storage / Cache Interfaces

本次进入 `Step 21：定义 IStorage / ICache 抽象`，目标是在接入真实 MySQL / Redis 前先建立业务层依赖的接口边界。

概念边界：

- `IStorage` 是未来 MySQL 持久化层的抽象。
- `ICache` 是未来 Redis 状态层的抽象。
- 业务层后续依赖接口，不直接 include MySQL / Redis 具体实现。
- 本 Step 不实现 MySQL client、Redis client、DAO、SQL schema、业务服务或运行时接入。

已完成代码：

- 新增 `include/liteim/storage/StorageTypes.hpp` 和 `IStorage.hpp`。
- 新增 `include/liteim/cache/CacheTypes.hpp` 和 `ICache.hpp`。
- 新增 `src/storage/CMakeLists.txt` 和 `src/cache/CMakeLists.txt`，以 header-only interface target 暴露接口层。
- `src/CMakeLists.txt` 接入 `storage` / `cache`。
- `tests/CMakeLists.txt` 接入 Step 21 测试并链接 `liteim_storage` / `liteim_cache`。

新增测试：

- `StorageInterfaceTest.HeaderIsSelfContained`
- `StorageInterfaceTest.CanBeImplementedByFakeStorage`
- `CacheInterfaceTest.HeaderIsSelfContained`
- `CacheInterfaceTest.NullCacheCanBeUsedAsTestDouble`

已完成文档同步：

- README 增加 `liteim_storage` / `liteim_cache` 模块说明和目录布局。
- 新增 `docs/tutorials/step21_storage_cache_interfaces.md`。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。

验证：

- `cmake --build build`：通过。
- `git diff --check`：通过。
- `ctest --test-dir build -R "(StorageInterfaceTest|CacheInterfaceTest)" --output-on-failure`：4/4 通过。
- `ctest --test-dir build --output-on-failure`：181/181 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
feat(storage): define storage and cache interfaces
```

## 2026-05-10 Markdown Documentation Alignment

本次只做 Markdown 文档对齐，目标是把当前面向读者的文档同步到 `9dd671b refactor(net): simplify owner loop cleanup and session state` 后的代码状态。

已修改：

- `docs/tutorials/step09_reactor_interfaces.md`：删除当前 `EventLoop` 接口说明里的 `isStopped()`，补齐真实成员字段，并说明 `EventLoop` 不再暴露 stopped 查询给外部清理路径。
- `docs/debug_cases/net_lifecycle_review_hardening.md`：保留 2026-05-09 第一轮 hardening 的历史记录，但明确当时的跨线程 `Acceptor::close()` 契约已被后续 owner-loop-only cleanup 取代，并更新面试回答。
- `docs/tutorials/step16_tcp_server.md`：把“不做心跳超时 / 输出高水位 / sendToUser”限定为 Step 16 当时边界，并说明后续 Step 的当前状态。
- `docs/tutorials/step14_session.md`：补充 heartbeat timeout 已在 Step 18 通过 `timerfd` / `TimerManager` 接入，避免继续写成当前后续项。
- `docs/process/task_plan.md`、`docs/process/findings.md`、`docs/process/progress.md`：顶部追加本次 Markdown alignment 过程记录。

已复查但未修改：

- `README.md`
- `docs/tutorials/step12_event_loop.md`
- `docs/tutorials/step13_acceptor.md`
- `docs/tutorials/step20_backpressure.md`
- `/home/yolo/jianli/AGENTS.md`
- `/home/yolo/jianli/CLAUDE.md`
- `/home/yolo/jianli/PROJECT_MEMORY.md`

验证：

- `git diff --check`：通过。
- `rg -n 'isStopped\(|loop_exited_|kSessionOutputHighWaterMark|Session::fd\(|TcpServer::sendToUser\(' include src tests server`：无输出，源码旧 API 残留检查通过。
- Markdown 漂移扫描：仍命中的内容是本次新增的当前说明、AGENTS/CLAUDE/PROJECT_MEMORY 的允许规则，以及 `docs/process/findings.md` / `docs/process/progress.md` / debug case 中明确作为历史记录保留的旧阶段流水；未发现 README 或当前教程继续把旧 API 写成当前事实。
- 本次未运行 `cmake --build build` 或 `ctest`，因为没有修改 C++、CMake 或测试文件。

提交信息：

```text
docs(net): align markdown with current lifecycle APIs
```

## 2026-05-10 Network Lifecycle/API Cleanup

本次按用户给定计划执行独立网络层 cleanup/refactor，不命名为 Step 20.1，不进入 Step 21。

概念边界：

- `Acceptor::close()` 统一为 owner-loop-only，直接非 owner 调用终止进程。
- `EventLoop` 删除 `isStopped()`，生命周期清理由 owner-loop-only API 契约保证。
- public API 只保留当前真实可用的能力：删除 `Session::fd()`、`TcpServer::sendToUser()` 占位接口和 `kSessionOutputHighWaterMark` 兼容别名。
- `SessionState` 收敛启动/关闭状态，但保留 `closed_` 给 `TcpServer` heartbeat/base loop 跨线程读关闭状态。
- `sendToSession()` 查表失败、空 session、调用时已关闭 session 都返回 `NotFound`。

已完成代码：

- `Acceptor::close()` 删除跨线程 promise/future/wait fallback，非 owner loop 线程调用 `std::terminate()`。
- `EventLoop` 删除 `loop_exited_` 和 `isStopped()`；`LoopingGuard` 只管理 `looping_`。
- `Session` 新增 `SessionState`，`startInLoop()` 只允许 `kNew -> kStarted`，读写发送只在 `kStarted` 下工作，`closeInLoop()` 推进到 `kClosing/kClosed`。
- `Session` 删除 public `fd()`，默认高水位只保留 `kSessionDefaultOutputHighWaterMark`。
- `TcpServer` 删除 `sendToUser()`，并让 `sendToSession()` 对空/已关闭 session 返回 `NotFound`。

新增/更新测试：

- `AcceptorTest.CloseFromNonOwnerThreadTerminates`
- `TcpServerTest.SlowClientAccumulatedSmallPacketsTriggerHighWaterMark`
- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `ReactorInterfaceTest.TcpServerHeaderIsSelfContained`
- 删除旧的跨线程 `Acceptor::close()` 成功返回测试、`EventLoop::isStopped()` 测试、`sendToUser()` 测试。

已完成文档同步：

- 更新 `/home/yolo/jianli/AGENTS.md` 和 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Acceptor owner-loop-only 口径。
- 更新 README、Step 12/13/14/16/20 教程、网络生命周期 debug case、`docs/process/task_plan.md`、`docs/process/findings.md` 和本文件。

验证：

- `cmake --build build`：通过。
- `ctest --test-dir build -R "Acceptor|EventLoop|Session|TcpServer|ReactorInterface" --output-on-failure`：59/59 通过。
- `ctest --test-dir build --output-on-failure`：177/177 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径检查无输出。

提交信息：

```text
refactor(net): simplify owner loop cleanup and session state
```

## 2026-05-09 Optional Step 18.6 Session Input Path Simplification

本次按用户确认执行 `Optional Step 18.6: Session 输入路径简化`，目标是删除 `Session` 输入路径里多余的一层 `input_buffer_`。

概念边界：

- `FrameDecoder` 已经维护 TCP 半包 / 粘包所需的内部缓存。
- `Session` 输入路径改为 `socket read -> FrameDecoder -> Packet callback`。
- 只有成功解出完整入站 `Packet` 后才刷新 `last_active_time`。
- 出站写仍不刷新 heartbeat 活跃时间。
- 本次不改 output buffer、不改 Step 20 高水位回压、不改 `Session` 状态机、不改 Packet/TLV wire format。

已完成代码：

- 删除 `Session::input_buffer_` 成员。
- 删除 `feedInputBuffer()`。
- `Session::handleRead()` 在 `read() > 0` 后直接调用 `decoder_.feed(buffer.data(), n, packets)`。
- `handleRead()` 继续保持 `EINTR` retry、`EAGAIN/EWOULDBLOCK` 返回、`n == 0` 关闭、解码失败关闭。
- `closeInLoop()` 不再清理输入 buffer，只清理输出 buffer。

新增测试：

- `SessionTest.SplitPacketAcrossReadsInvokesCallbackAfterSecondRead`
- `SessionTest.MalformedPacketClosesSession`

已完成文档同步：

- 更新 Step 14 Session 教程，把读路径改为直接喂 `FrameDecoder`。
- 更新 Step 7 Buffer 教程，明确输入半包/粘包缓存由 `FrameDecoder` 维护，`Buffer` 主要用于输出缓冲。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和 `docs/process/progress.md`。

验证：

- `cmake --build build && ctest --test-dir build -R "SessionTest|TcpServerTest|FrameDecoderTest" --output-on-failure`：37/37 通过。
- `ctest --test-dir build --output-on-failure`：183/183 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。

提交信息：

```text
refactor(net): simplify session input decoding path
```

## 2026-05-09 Step 20 Slow-Client Backpressure Hardening

本次进入 `Step 20: 完善慢客户端回压保护`，目标是在已有默认 4MB 超限关闭基础上，把输出缓冲区高水位整理为可配置、可观测、可测试的策略。

概念边界：

- `Session` 的输出高水位默认仍是 4MB。
- 每次服务端发送编码后的 Packet 前，在 owner loop 中检查 `pending output + incoming encoded bytes`。
- 超过高水位时记录 warning 日志，并关闭该 `Session`。
- `TcpServer` 创建新 `Session` 时把 server 级高水位传入连接。
- `Config` 新增 `server.output_high_water_mark_bytes` 配置键。
- `pendingOutputBytes()` 仍然只能 owner-loop 查询。
- 本次不做暂停读、低水位恢复、消息优先级丢弃、群聊广播优化、`Session::input_buffer_` 简化或状态机重构。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试先失败在 `Config::session_output_high_water_mark` 等接口缺失，证明测试覆盖了 Step 20 缺失能力。

已完成代码：

- `Config` 增加 `session_output_high_water_mark`，默认 `4 * 1024 * 1024`。
- `Config::loadFromFile()` 支持 `server.output_high_water_mark_bytes`，并拒绝 0。
- `Session` 增加 `kSessionDefaultOutputHighWaterMark`、`output_high_water_mark_`、`setOutputHighWaterMark()` 和 `outputHighWaterMark()`。
- `Session::sendEncodedInLoop()` 改为使用实例级高水位；超限时记录 `pending`、`incoming`、`limit` 并 `closeInLoop()`。
- `TcpServer` 增加 `setSessionOutputHighWaterMark()`，要求 base loop 线程、启动前调用、阈值大于 0。
- `TcpServer::createSessionInLoop()` 在 I/O loop 中创建 `Session` 后设置输出高水位。
- `server/main.cpp` 把 `Config` 中的输出高水位传入 `TcpServer`。

新增/更新测试：

- `ConfigTest.ZeroHighWaterMarkFails`
- `SessionTest.DefaultOutputHighWaterMarkIsFourMegabytes`
- `SessionTest.RejectsZeroOutputHighWaterMark`
- `SessionTest.CloseWhenPendingOutputExceedsConfiguredHighWaterMark`
- `TcpServerTest.NormalClientDoesNotTriggerConfiguredHighWaterMark`
- `TcpServerTest.RejectsZeroSessionOutputHighWaterMark`
- `TcpServerTest.SessionOutputHighWaterMarkMustBeSetBeforeStart`
- `TcpServerTest.SlowClientIsClosedWhenOutputExceedsHighWaterMark`
- `TcpServerTest.ClosedSlowClientIsRemovedFromSessionTable`
- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `ReactorInterfaceTest.TcpServerHeaderIsSelfContained`

已完成文档同步：

- 更新 README，写入可配置输出高水位和配置键。
- 更新 Step 2 Config 教程，补充新字段、新 key 和 0 值校验。
- 更新 Step 14 Session 教程，把固定 4MB 文案改为 Step 20 可配置高水位。
- 新增 `docs/tutorials/step20_backpressure.md`。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 中 Step 20 的配置键说明。
- 更新 `docs/process/task_plan.md`、`docs/process/findings.md` 和 `docs/process/progress.md`。

最终验证：

- `cmake --build build && ctest --test-dir build -R "(ConfigTest|SessionTest|TcpServerTest|ReactorInterfaceTest)" --output-on-failure`：42/42 通过。
- `cmake --build build && ctest --test-dir build --output-on-failure`：181/181 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `git diff --check`：通过。

提交信息：

```text
feat(net): add session high water mark backpressure
```

## 2026-05-09 Session Activity Semantics Fix

本次只处理 P0：`last_active_time` 出站刷新语义错误。

问题：

- 设计语义应为“客户端发来完整、合法、成功解码的入站 Packet 才算连接活跃”。
- 旧实现里 `Session::sendEncodedInLoop()` append output buffer 后会刷新活跃时间。
- 旧实现里 `Session::handleWrite()` 每次写出字节后也会刷新活跃时间。
- 这会导致服务端持续 push / echo 时，沉默客户端被误判为活跃连接。

TDD RED 已确认：

```bash
cmake --build build && ctest --test-dir build -R "ServerWritesDoNotRefreshHeartbeatActivity" --output-on-failure
```

新增 `TcpServerTest.ServerWritesDoNotRefreshHeartbeatActivity` 后先失败：`closed_by_heartbeat` 为 `false`，证明服务端持续写会错误续期连接。

已完成代码：

- 删除 `Session::sendEncodedInLoop()` 中的出站 `updateLastActiveTime()`。
- 删除 `Session::handleWrite()` 中的出站 `updateLastActiveTime()`。
- 保留 `feedInputBuffer()` 成功解出完整入站 Packet 后的 `updateLastActiveTime()`。

已完成文档同步：

- 更新 Step 14 教程，把 `last_active_time` 语义改为完整入站 Packet 活跃时间。
- 更新 Step 18 教程，明确服务端出站写不续期。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 中 Step 18 的活跃时间描述。
- 更新 `docs/process/findings.md` 和 `docs/process/task_plan.md` 记录 P0 修复边界。

边界：

- 不做 Step 20 慢客户端回压完善。
- 不配置 high water mark。
- 不删除 `Session::input_buffer_`。
- 不重构 `Session` 状态机。
- 不自动 commit，避免混入已有未提交的文档边界修正改动。

最终验证：

- `cmake --build build && ctest --test-dir build -R "ServerWritesDoNotRefreshHeartbeatActivity" --output-on-failure`：修复后通过。
- `cmake --build build && ctest --test-dir build -R "Session|TcpServer" --output-on-failure`：20/20 通过。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124`：通过，服务端收到 SIGTERM 后优雅退出。
- `ctest --test-dir build --output-on-failure`：172/172 通过。
- `git diff --check`：通过。

## 2026-05-09 Documentation Boundary Correction

本次只修改 Markdown，目标是把文档职责重新分清：

- `/home/yolo/jianli/PROJECT_MEMORY.md` 只保留总设计、长期路线、架构约束、Step 目标、边界、测试要求和计划 commit message。
- `/home/yolo/jianli/AGENTS.md` 和 `/home/yolo/jianli/CLAUDE.md` 只保留 agent 约束和读取顺序，不记录 Step 完成状态、实际提交 hash 或活动下一步。
- `LiteIM/README.md` 继续作为对外说明文档，不记录过程进度。
- `docs/process/task_plan.md`、`docs/process/findings.md`、`docs/process/progress.md` 负责进度、发现、验证结果和过程记忆。

已完成：

- 删除 `PROJECT_MEMORY.md` 顶部的优先级、完成状态、真实状态、最近提交、默认下一步等过程信息。
- 将 Step 18、Step 18.5、Step 19 段落改回路线层描述，删除实际完成 hash 和完成验证命令。
- 清理 `AGENTS.md` / `CLAUDE.md` 中的当前 Step 状态和默认下一步描述。
- 将 README 的 `Current Modules` 改为 `Core Components`，移除 `currently/current implementation` 这类进度措辞。
- 在 `docs/process/task_plan.md`、`docs/process/findings.md`、`docs/process/progress.md` 靠前位置记录新的文档职责边界。

验证：

- 本次不改 C++、CMake 或测试代码，不需要重新编译。
- `planning-with-files` 的 session catchup 仍提示旧的纯概念问答未同步；该上下文不对应本次文档修改。
- `rg` 扫描确认 `AGENTS.md` / `CLAUDE.md` 不再包含当前状态、默认下一步或 Step 18/19 完成状态措辞。
- `rg` 扫描确认 `PROJECT_MEMORY.md` 不再包含顶部优先级、最近提交、完成验证、完成提交或默认下一步等过程记录措辞。
- `rg` 扫描确认 `README.md` 不再包含 `Current Status`、`当前状态`、`Current Modules`、`current` 或 `currently`。
- `git diff --check` 通过；额外 trailing whitespace 扫描对目标 Markdown 无输出。
- `git status --short` 显示 LiteIM Git 仓库内只修改 `README.md`、`docs/process/task_plan.md`、`docs/process/findings.md`、`docs/process/progress.md`；根目录的 `PROJECT_MEMORY.md`、`AGENTS.md`、`CLAUDE.md` 位于 LiteIM 仓库外。

## 2026-05-09 Step 19 Signalfd Graceful Shutdown

本次进入 `Step 19: signalfd graceful shutdown`，目标是让 `liteim_server` 收到 `SIGINT` / `SIGTERM` 后走 Reactor 内部优雅退出路径。

概念边界：

- 信号不在传统 signal handler 中清理资源。
- `SIGINT` / `SIGTERM` 通过 `signalfd` 变成 fd 可读事件。
- signal callback 在 base `EventLoop` 线程执行。
- callback 中先 `server.stop()`，再 `loop.quit()`。
- `SignalWatcher` 和 `TcpServer` 一样遵守 owner-loop-only stop/destruct 规则。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试后先失败于 `fatal error: liteim/net/SignalWatcher.hpp: No such file or directory`，证明测试覆盖了 Step 19 缺失接口。

已完成代码：

- 新增 `include/liteim/net/SignalWatcher.hpp`。
- 新增 `src/net/SignalWatcher.cpp`。
- `SignalWatcher` 使用 `pthread_sigmask()`、`signalfd()`、`Channel` 和 `EventLoop` 把信号接入 Reactor。
- `SignalWatcher::start()` 要求 owner loop 线程调用，失败返回 `Status`。
- `SignalWatcher::stop()` 和析构要求 owner loop 线程调用，非 owner 线程直接 stop 会 `std::terminate()`。
- `server/main.cpp` 创建 `SignalWatcher(SIGINT, SIGTERM)`，启动后再启动 `TcpServer`，收到信号后停止 server 并退出主 loop。
- `src/net/CMakeLists.txt` 已把 `SignalWatcher.cpp` 编入 `liteim_net`。

新增测试：

- `tests/net/signal_watcher_header_test.cpp`
- `tests/net/signal_watcher_test.cpp`
- `tests/server_signal_shutdown_test.sh`
- `LiteIMServerSignalTest.TerminatesOnSigterm` CTest。

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build -R "SignalWatcher|LiteIMServerSignal" --output-on-failure
```

结果：Step 19 相关 4/4 tests passed。

最终验证已完成：

- `cmake -S . -B build && cmake --build build` 通过。
- `ctest --test-dir build -R "SignalWatcher|LiteIMServerSignal" --output-on-failure` 通过，4/4。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124` 通过，服务端收到 SIGTERM 后记录 graceful shutdown 日志。
- `ctest --test-dir build --output-on-failure` 通过，171/171。
- `git diff --check` 无输出。
- `.gitkeep` 和旧 SQLite / `InMemoryStorage` / old `server/net` 路径扫描无输出。

提交状态：

- 将按 `PROJECT_MEMORY.md` 使用提交信息 `feat(server): add signalfd graceful shutdown` 完成 Step 19 commit。

## 2026-05-09 PROJECT_MEMORY Markdown Alignment

本次按新版 `/home/yolo/jianli/PROJECT_MEMORY.md` 同步其余 Markdown，不改 C++ 代码。

已完成：

- 更新 `/home/yolo/jianli/AGENTS.md`，把它作为未来 agent 的约束文件：按当时进度写入 Step 18 / Step 18.5 已完成、后续进入 Step 19 `signalfd graceful shutdown`，并补充 muduo-style owner-loop 生命周期规则。
- 更新 `/home/yolo/jianli/CLAUDE.md`，同步当前进度、docs 边界、owner-loop 约束和 `docs/debug_cases/` 保留规则。
- 将 `docs/debug_cases/net_lifecycle_review_hardening.md` 从英文完整改写为中文，保留复盘背景、已接受 bug、不采纳项、验证命令和面试回答。
- 更新 `docs/process/task_plan.md` 当前阶段，补上 Step 18.5 已完成记录和本轮 Markdown alignment 记录。
- 更新 `docs/process/findings.md` 权威来源和文档边界，避免旧的 “从 Step 0 重新开始” 口径压过当前真实进度。

边界：

- 没有修改 C++ 源码、CMake 或测试代码。
- 没有恢复 `docs/architecture.md`、`docs/project_layout.md`、`docs/roadmap.md` 或 `docs/tutorials/README.md`。
- 没有把 Optional Step 18.6 / Step 18.7 变成 Step 19 前置条件。

验证：

- `find /home/yolo/jianli -maxdepth 1 -name 'PROJECT_MEMORY*.md' -printf '%f\n' | sort` 只输出 `PROJECT_MEMORY.md`。
- `find docs -type f | sort` 只输出两个 debug case 文档。
- `find docs/tutorials -maxdepth 1 -type f -name 'README.md' -print` 无输出。
- 合并残留和旧入口文案扫描无输出。
- `rg -n "Current Status|当前状态" README.md` 无输出。
- `git diff --check` 无输出。

## 2026-05-09 Muduo-style Lifecycle Hardening

本次根据外部 review 做一轮小范围 hardening，目标是让 Reactor 对象生命周期更接近 muduo：对象在哪个 loop 线程拥有，就在哪个 loop 线程停止和析构。

范围：

- 修复 `TcpServer::stop()` 非 owner 线程捕获裸 `this` 异步投递的 UAF 风险。
- 修复 `TimerManager::stop()` 同类非 owner 线程裸 `this` 异步投递风险。
- 把 `Session` 构造函数从裸 `int fd` 改为接收 `UniqueFd`，让 accepted fd 所有权全程由 RAII 类型表达。
- 让 `Session::pendingOutputBytes()` 先检查 owner loop 线程，避免跨线程读 `output_buffer_`。
- 把 `server/main.cpp` 从 scaffold 改为真实启动 `EventLoop + TcpServer` echo server。

TDD RED：

- `cmake --build build` 先失败在 `ReactorInterfaceTest.SessionHeaderIsSelfContained` 的三个 static_assert：`Session(EventLoop*, UniqueFd, id)` 尚不存在、裸 `int fd` 构造仍存在、`pendingOutputBytes()` 仍是 `noexcept`。

代码完成：

- `TcpServer::~TcpServer()` 和 `TcpServer::stop()` 改为 owner-loop-only；非 owner 线程直接调用 `stop()` 会 `std::terminate()`。
- `TimerManager::~TimerManager()` 和 `TimerManager::stop()` 改为 owner-loop-only；非 owner 线程直接调用 `stop()` 会 `std::terminate()`。
- `TcpServer::createSessionInLoop()` 直接把 accepted `UniqueFd` move 给 `Session`，删除裸 fd `release()` 串联。
- `Session(EventLoop*, UniqueFd, id)` 在构造体内接管 fd，构造失败也由 `UniqueFd` 自动关闭 fd。
- `Session::pendingOutputBytes()` 去掉 `noexcept` 并调用 `loop_->assertInLoopThread()`。
- `server/CMakeLists.txt` 让 `liteim_server` 链接 `liteim_net`；`server/main.cpp` 创建 `EventLoop` 和 `TcpServer`，启动后进入 `loop()`。

新增/更新测试：

- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `SessionTest.PendingOutputBytesRequiresOwnerLoopThread`
- `TcpServerTest.StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis`
- `TimerManagerTest.StopFromNonOwnerThreadTerminatesInsteadOfQueueingThis`

验证完成：

- `cmake -S . -B build` 通过。
- `cmake --build build` 通过。
- `ctest --test-dir build -R "(Session|TcpServer|TimerManager)" --output-on-failure` 通过，23/23。
- `timeout 1s ./build/server/liteim_server || test $? -eq 124` 通过，输出 `LiteIM server is listening on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure` 通过，167/167。
- `git diff --check` 无输出。
- `.gitkeep` 和旧 SQLite / `InMemoryStorage` / old `server/net` 路径扫描无输出。

## 2026-05-09 Step 18 TimerManager

本次进入 `Step 18: implement TimerManager + timerfd heartbeat timeout`。

概念边界：

- `TimerHeap` 只负责 timer id、过期时间、小根堆、回调保存、取消标记和 lazy deletion。
- `TimerManager` 只负责把 Linux `timerfd` 接入 `EventLoop`，读取 tick 计数并触发已过期 timer。
- `TcpServer` 本轮只接入服务端 idle session cleanup：每 5 秒检查一次，90 秒未活跃连接关闭。
- `Session` 收到完整 `Packet` 后刷新活跃时间；后续真正的心跳协议、登录态续期、Redis 在线状态和 `HeartbeatService` 留到后续 Step。

准备新增 RED 测试：

- `tests/timer/timer_heap_header_test.cpp`
- `tests/timer/timer_heap_test.cpp`
- `tests/timer/timer_manager_header_test.cpp`
- `tests/timer/timer_manager_test.cpp`
- `tests/net/tcp_server_test.cpp` 增加 idle close / active survive 回归测试。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试后先失败于 `TcpServer::setHeartbeatOptions` 接口不存在，证明测试覆盖到了 Step 18 的 TcpServer 心跳超时配置入口。

已完成代码：

- 新增 `include/liteim/timer/TimerHeap.hpp` 和 `src/timer/TimerHeap.cpp`。
- 新增 `include/liteim/timer/TimerManager.hpp` 和 `src/timer/TimerManager.cpp`。
- 新增 `src/timer/CMakeLists.txt`，把 timer 源码编进 `liteim_net`，避免 `TimerManager` 依赖 `EventLoop` 时形成库循环依赖。
- `TimerHeap` 支持 one-shot timer、取消、过期回调、小根堆顺序和 lazy deletion。
- `TimerManager` 使用 `timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)`，并通过 `Channel` 注册到 owner `EventLoop`。
- `TcpServer` 增加 `setHeartbeatOptions()`，默认 5 秒检查一次、90 秒未活跃关闭。
- `TcpServer` 在 base loop 上创建 `TimerManager`，每轮扫描 session 快照，过期连接通过 `Session::close()` 回到所属 I/O loop。
- `Session` 调整为收到完整 `Packet` 后刷新 `last_active_time`。

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Timer|TcpServerTest\\.(IdleSessionIsClosedByHeartbeatTimeout|ActiveSessionSurvivesHeartbeatTimeout)|ReactorInterfaceTest.TcpServerHeaderIsSelfContained"
```

结果：10/10 Step 18 相关测试通过。

全量测试已通过：

```bash
ctest --test-dir build --output-on-failure
```

结果：164/164 tests passed。

文档同步：

- README、docs/architecture.md、docs/project_layout.md、docs/roadmap.md、docs/tutorials/README.md、Step 18 tutorial、task_plan、findings、progress 和 PROJECT_MEMORY 已同步 Step 18 结果。

最终验证已通过：

```bash
cmake -S . -B build && cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：configure/build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 164/164 通过；`git diff --check` 无输出；`.gitkeep` 检查无输出；旧路线路径检查无输出。

提交状态：

- 将按 `PROJECT_MEMORY.md` 使用提交信息 `feat(timer): integrate timerfd heartbeat timeout` 完成 Step 18 commit。

## 2026-05-09 Network Review Hardening

本次根据外部 review 核对 Step 17 后的网络/并发代码，只接受能在当前代码中证实、并且能用回归测试固定住的问题。

已接受并修复：

- `EventLoopThread::stop()` 自线程调用不再 detach，也不在 `stop()` 中清空 `loop_` / `running_`；状态清理统一放到 `threadFunc()` 末尾，owner 线程后续负责 join。
- `EventLoopThread` 增加 `thread_id_` 和 `join_started_`，避免 self-stop / owner-stop 边界上并发访问同一个 `std::thread` join 状态。
- `Session` 新增 `kSessionOutputHighWaterMark = 4MB`，append 输出缓冲前先检查高水位，超限直接关闭连接。
- `Session` 增加逻辑 `id()`；`TcpServer` 使用自增 `std::uint64_t next_session_id_` 作为 `sessions_` key，不再用 fd 当 session id。
- `TcpServer::sendToSession()` 接口改为 `std::uint64_t session_id`。
- `Channel::handleEvent()` 处理 `EPOLLERR` 后不再立即返回；如果同一轮还有可读事件，会继续执行 read callback。
- `Acceptor::close()` 保留跨线程 close 契约，但在 close task 已投递、loop 却先退出的竞态下不再永久等待，会检测 `isStopped()` 后走 fallback 清理。

TDD RED 已确认：

```bash
cmake --build build
```

新增/修改测试后先失败于 `TcpServer::sendToSession` 接口静态断言，证明测试覆盖到了 fd-as-id 到逻辑 session id 的 API 变化。

新增/更新测试：

- `ChannelTest.ErrorWithReadableEventInvokesErrorThenRead`
- `AcceptorTest.CloseFromOtherThreadWhileLoopExitsWithQueuedCloseDoesNotBlock`
- `EventLoopThreadTest.OwnerStopWaitsAfterStopIsRequestedInsideLoop`
- `SessionTest.CloseWhenPendingOutputExceedsHighWaterMark`
- `TcpServerTest.SendToSessionFromOtherThreadDeliversPacket` 改为使用 `session->id()`
- `ReactorInterfaceTest.TcpServerHeaderIsSelfContained` 改为校验 `sendToSession(std::uint64_t, Packet)`

本次不采用/暂不改：

- 不把 `Acceptor::close()` 改成 owner-loop-only 契约；当前项目保留跨线程 close，并修复 queued close 等不到执行的竞态。
- 不把 `ThreadPool::stop()` 改成 swap-and-join；当前 `stop_mutex_` 已有并发 stop 回归测试，worker 内部 delete pool 仍属于不支持的对象生命周期错误，detach 不能真正修好。
- 不把 `Session` 状态 bool、`EventLoop` flag、`FrameDecoder` buffer 结构等风格项混入本次 bugfix；这些需要单独测试和边界讨论。

文档同步：

- README、docs/architecture.md、docs/project_layout.md、docs/roadmap.md、Step 11/13/14/15/16 docs/tutorials、task_plan、findings、progress 和 PROJECT_MEMORY 已同步本轮 hardening。
- 新增 `docs/debug_cases/net_lifecycle_review_hardening.md` 记录复盘过程、修复点、不采纳项和面试回答。

验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/server/liteim_server
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：build 通过；CTest 155/155 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；`git diff --check` 无输出；`.gitkeep` 检查无输出；旧路线路径检查无输出。

## 2026-05-08 Step 17 Business ThreadPool

本次进入 `Step 17: implement business ThreadPool`，目标是实现独立业务线程池，让后续 MySQL / Redis / 密码哈希 / 历史消息查询不阻塞 I/O loop。

概念边界：

- I/O 线程继续只负责 socket 读写、Packet/TLV 编解码和连接生命周期。
- 业务线程池负责执行可能阻塞或较重的业务任务。
- 业务线程不能直接修改 `Session` 内部状态；响应必须通过 `Session::sendPacket()` 或 `EventLoop::queueInLoop()` 回到连接所属 I/O loop。
- 第一版 `ThreadPool` 不提供 `future`、任务优先级、动态扩缩容或 work stealing。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试先失败于 `fatal error: liteim/concurrency/ThreadPool.hpp: No such file or directory`，证明测试覆盖了当前 Step 缺失的业务线程池接口。

已完成代码：

- 新增 `include/liteim/concurrency/ThreadPool.hpp`。
- 新增 `src/concurrency/CMakeLists.txt`。
- 新增 `src/concurrency/ThreadPool.cpp`。
- `ThreadPool::start()` 创建固定数量 worker，拒绝 0 worker 和重复启动。
- `ThreadPool::submit()` 把任务放入 `deque<Task>`，通过 `condition_variable` 唤醒 worker；未运行或空任务会返回 `InvalidArgument`。
- `ThreadPool::workerLoop()` 等待任务、取出任务并执行；任务抛异常时会被捕获，worker 继续处理后续任务。
- `ThreadPool::stop()` 停止接收新任务，唤醒 worker，并等待已入队任务执行完成后退出。
- `ThreadPool` 内部状态已收敛为单一 `running_`；`workerLoop()` 在 `!running_ || !tasks_.empty()` 时被唤醒，stop 后会先 drain 队列再退出。
- `ThreadPool::stop()` 如果从 worker 任务内部调用，会通过 worker-local 标记识别 self-stop，只发出停止请求并返回，等待 owner 线程后续清理。
- 外部多线程并发调用 `stop()` 时通过 `stop_mutex_` 串行化 join/cleanup，避免同时 join 同一个 `std::thread`。
- `pendingTaskCount()` 返回仍在队列中等待执行的任务数量。
- `src/CMakeLists.txt` 已接入 `concurrency` 模块；`tests/CMakeLists.txt` 已链接 `liteim_concurrency`。

新增测试：

- `tests/concurrency/thread_pool_header_test.cpp`
- `tests/concurrency/thread_pool_test.cpp`
- `ConcurrencyInterfaceTest.ThreadPoolHeaderIsSelfContained`
- `ThreadPoolTest.StartRejectsZeroWorkers`
- `ThreadPoolTest.SubmitExecutesTask`
- `ThreadPoolTest.MultipleWorkersRunConcurrently`
- `ThreadPoolTest.StopRejectsNewTasks`
- `ThreadPoolTest.StopCalledFromWorkerRequiresOwnerCleanupBeforeRestart`
- `ThreadPoolTest.ConcurrentStopCallsAreSerialized`
- `ThreadPoolTest.DestructorWaitsForQueuedTasks`
- `ThreadPoolTest.PendingTaskCountTracksQueuedTasks`

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "ThreadPool|ConcurrencyInterfaceTest.ThreadPool"
```

结果：9/9 Step 17 tests passed。该正则同时匹配了历史 `EventLoopThreadPool` 测试，CTest 输出中共运行 13 个测试，全部通过。

已完成文档同步：

- README、docs/architecture.md、docs/project_layout.md、docs/roadmap.md、docs/tutorials/README.md、Step 17 tutorial、task_plan、findings、progress 和 PROJECT_MEMORY 已同步 Step 17 结果。
- 新增并更新 `docs/debug_cases/thread_pool_worker_stop.md`，记录 worker 内部调用 `ThreadPool::stop()`、并发外部 `stop()`、`running_` 状态收敛、RED 测试、修复设计、验证命令和面试回答。

全量验证已通过：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 151/151 通过；`git diff --check` 无输出；`.gitkeep` 检查无输出；旧路线路径检查无输出。

提交状态：

- 已按 `PROJECT_MEMORY.md` 使用提交信息 `feat(concurrency): add business thread pool` 完成 Step 17 commit。

## 2026-05-08 Step 16 TcpServer

本次进入 `Step 16: implement TcpServer multi-Reactor version`，目标是实现第一个多 Reactor echo server。

概念边界：

- base `EventLoop` 持有 `Acceptor`，负责监听和 accept。
- `EventLoopThreadPool` 提供子 I/O loops，新连接按 round-robin 分配。
- `Session` 在被选中的 I/O loop 中创建，后续读写固定在该 loop。
- `TcpServer` 维护线程安全的 `sessions_` 表，并提供 `sendToSession()` / 基础 `sendToUser()`。
- 第一版业务只做 echo，不进入 business `ThreadPool`、MySQL、Redis、登录态或 MessageRouter。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试先失败于 `fatal error: liteim/net/TcpServer.hpp: No such file or directory`，证明测试覆盖了当前 Step 缺失的 `TcpServer` 接口。

已完成代码：

- 新增 `include/liteim/net/TcpServer.hpp`。
- 新增 `src/net/TcpServer.cpp`。
- `TcpServer::start()` 在 base loop 线程启动 I/O 线程池、创建 `Acceptor` 并记录实际端口。
- `Acceptor` accept 到的 `UniqueFd` 由 `TcpServer` 分配到子 I/O loop。
- `TcpServer::createSessionInLoop()` 在目标 loop 中创建 `Session`、设置 message/close callback、写入 `sessions_` 表并启动读事件。
- 默认 message callback 为空时执行 echo。
- `sendToSession()` 支持从其他线程调用，实际发送仍回到 session 所属 loop。
- 当时曾保留用户发送占位接口并返回 `NotFound`；2026-05-10 cleanup 已删除该占位 API，等待后续登录态和 user-session 绑定实现真实用户路由。
- `src/net/CMakeLists.txt` 已接入 `TcpServer.cpp`。

新增测试：

- `tests/net/tcp_server_header_test.cpp`
- `tests/net/tcp_server_test.cpp`
- `ReactorInterfaceTest.TcpServerHeaderIsSelfContained`
- `TcpServerTest.EchoesPacketToClient`
- `TcpServerTest.DistributesConnectionsAcrossIoLoops`
- `TcpServerTest.RemovesSessionAfterClientDisconnects`
- `TcpServerTest.SendToSessionFromOtherThreadDeliversPacket`
- `TcpServerTest.SendToUnknownUserReturnsNotFound`

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "TcpServer|ReactorInterfaceTest.TcpServer"
```

结果：6/6 Step 16 tests passed。

已完成文档同步：

- README、docs/architecture.md、docs/project_layout.md、docs/roadmap.md、docs/tutorials/README.md、Step 16 tutorial、task_plan、findings、progress 和 PROJECT_MEMORY 已同步 Step 16 结果。

全量验证已通过：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print
find . -path ./build -prune -o -path ./.git -prune -o \( -path ./server/net -o -path ./server/protocol -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 142/142 通过；`git diff --check` 无输出；`.gitkeep` 检查无输出；旧路线路径检查无输出。

提交状态：

- 已按 `PROJECT_MEMORY.md` 使用提交信息 `feat(net): implement multi reactor tcp server` 完成 Step 16 commit。

## 2026-05-08 Pre-Step 16 Code Cleanup

本次继续上轮未完成收尾，目标是在 `Step 16: implement TcpServer multi-Reactor version` 前完成接口和代码卫生清理，不启动 TcpServer 实现。

已确认采用的清理项：

- `Epoller::owner_loop_` 不能闲置，必须用于检查 `Channel` 是否属于同一个 `EventLoop`。
- Packet/TLV 的大端读写 helper 有明确复用价值，应抽到协议层通用头文件。
- `Acceptor::NewConnectionCallback` 应接收 `UniqueFd`，用类型表达 accepted fd 所有权。
- `Acceptor::listening_` 是重复状态，可以由 `listen_fd_` 推导。
- 测试里的自制 `FdGuard` 应替换成生产 `UniqueFd`。
- 生产代码中过长教学注释应移到教程，源码只保留必要契约说明。

已完成代码：

- 新增 `include/liteim/protocol/ByteOrder.hpp`，提供 `appendUint16BE()`、`appendUint32BE()`、`appendUint64BE()`、`readUint16BE()`、`readUint32BE()`、`readUint64BE()`。
- `src/protocol/Packet.cpp` 和 `src/protocol/TlvCodec.cpp` 改为复用 `ByteOrder.hpp`，删除重复 helper，wire format 不变。
- `Epoller::updateChannel()` / `removeChannel()` 新增 owner-loop 校验，跨 `EventLoop` 的 `Channel` 返回 `InvalidArgument`。
- `Acceptor::NewConnectionCallback` 改为 `std::function<void(UniqueFd, const sockaddr_in&)>`。
- `Acceptor::handleRead()` 使用 `std::move(accepted_fd)` 把 accepted fd 所有权交给 callback，不再靠裸 fd 和手动 `release()` 表达。
- `Acceptor::listening_` 已删除，`listening()` 直接检查 `listen_fd_`。
- `tests/net/acceptor_test.cpp` 和 `tests/net/socket_util_test.cpp` 删除自制 `FdGuard`，统一使用 `liteim::UniqueFd`。
- 生产代码里明显的教学型长注释已清理，保留 `Channel` callback 生命周期契约这类必要注释。

新增/更新测试：

- `tests/protocol/byte_order_test.cpp`
- `ByteOrderTest.AppendsUnsignedIntegersAsBigEndianBytes`
- `ByteOrderTest.ReadsUnsignedIntegersFromBigEndianBytes`
- `EpollerTest.RejectsChannelOwnedByDifferentEventLoop`
- `ReactorInterfaceTest.AcceptorHeaderIsSelfContained` 更新为 `UniqueFd` callback 签名。
- Acceptor callback 测试均改为接收 `UniqueFd`。

已完成文档同步：

- README、docs/architecture.md、docs/project_layout.md、docs/roadmap.md、Step 4/5/10/13/15 docs/tutorials、task_plan、findings、progress 和 PROJECT_MEMORY 已同步本次清理。

验证结果：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

结果：build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 136/136 通过。

## 2026-05-08 Step 15 EventLoopThreadPool

本次进入 `Step 15: implement EventLoopThread and EventLoopThreadPool`，目标是实现 one-loop-per-thread 多 Reactor 的线程基础，不启动 Step 16 `TcpServer`。

概念边界：

- `EventLoopThread` 表示“一个线程拥有一个 `EventLoop`”。
- `EventLoopThreadPool` 表示“多个 I/O loops 的管理器”。
- 主 Reactor 后续继续负责 accept；子 Reactor 后续负责已连接 `Session` 的 I/O。
- 本 Step 不创建 `TcpServer`，不做连接分发，也不进入业务线程池、MySQL 或 Redis。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试先失败于 `fatal error: liteim/net/EventLoopThread.hpp: No such file or directory`，证明测试覆盖了当前 Step 缺失的线程接口。

已完成代码：

- 新增 `include/liteim/net/EventLoopThread.hpp` 和 `src/net/EventLoopThread.cpp`。
- 新增 `include/liteim/net/EventLoopThreadPool.hpp` 和 `src/net/EventLoopThreadPool.cpp`。
- `EventLoopThread::startLoop()` 启动工作线程，等待线程内 `EventLoop` 构造完成后返回观察指针。
- `EventLoopThread::stop()` 通过 `EventLoop::quit()` 唤醒并退出 loop，然后 join 线程；析构自动 stop。
- `EventLoopThreadPool::start()` 启动指定数量的子 loops。
- `EventLoopThreadPool::getNextLoop()` round-robin 返回子 loop；线程数为 0 时返回 base loop。
- `src/net/CMakeLists.txt` 已接入 Step 15 新源码。

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

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "EventLoopThread|EventLoopThreadPool|ReactorInterfaceTest.EventLoopThread"
```

结果：9/9 Step 15 tests passed。

全量验证已通过：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . \( -path './server/net' -o -path './server/protocol' -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 133/133 通过；`git diff --check` 无输出；旧路线路径检查无输出。

## 2026-05-08 Pre-Step 15 Byte/Bytes API Cleanup

本次在进入 `Step 15: implement EventLoopThread and EventLoopThreadPool` 前，先收口上次未完成的字节类型清理，目标是不让后续网络线程池和 `TcpServer` 继续扩散混用 `std::vector<char>` / `std::vector<std::uint8_t>` / `std::string_view` 的公共接口。

已完成代码：

- 新增 `include/liteim/base/Types.hpp`，定义 `liteim::Byte` 和 `liteim::Bytes`。
- `Packet`、`TlvCodec`、`FrameDecoder`、`Buffer`、`Session` 和相关 tests 已切换到 `Byte` / `Bytes`。
- `Buffer` 公共接口保留 `append(const Byte*, len)`、`append(const Bytes&)` 和 `append(const std::string&)`；`ensureWritableBytes()` 变成内部实现细节。
- `ErrorCode::toString()` 不再返回 `std::string_view`；`Logger::parseLogLevel()` 不再在公共接口接收 `std::string_view`。

已完成文档同步：

- README、docs、Step 4/5/6/7 docs/tutorials、task_plan、findings、progress 和 PROJECT_MEMORY 已同步 `Byte` / `Bytes` API 说明。

验证结果：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . \( -path './server/net' -o -path './server/protocol' -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：build 通过；server scaffold 正常输出 `LiteIM server scaffold is running on 0.0.0.0:9000`；CTest 124/124 通过；`git diff --check` 无输出；旧路线路径检查无输出；旧接口扫描只剩规则说明和本次清理记录中的说明性命中。

## 2026-05-07 Step 14 Session

本次进入 `Step 14: implement Session`，目标是实现单个已连接 fd 的生命周期和 Packet I/O，不启动 Step 15 多 Reactor 线程池。

概念边界：

- `Session` 是连接 owner，接管一个已连接 fd。
- `Acceptor` 只负责 accept，后续 `TcpServer` 会创建 `Session`；本 Step 不实现 `TcpServer`。
- `Session` 持有 `Channel`、输入 `Buffer`、输出 `Buffer` 和 `FrameDecoder`。
- I/O 线程负责非阻塞读写和协议编解码，不做 MySQL / Redis 阻塞业务。
- 跨线程 `sendPacket()` 通过 `EventLoop::queueInLoop()` 回到连接所属 loop。

TDD RED 已确认：

```bash
cmake --build build
```

新增测试先失败于 `fatal error: liteim/net/Session.hpp: No such file or directory`，证明测试覆盖了当前 Step 缺失的 `Session` 接口。

已完成代码：

- 新增 `include/liteim/net/Session.hpp`。
- 新增 `src/net/Session.cpp`。
- `Session` 使用 `std::enable_shared_from_this`，`start()` 中调用 `Channel::tie()`。
- `handleRead()` 循环读到 `EAGAIN`，输入字节经过 `Buffer` 后交给 `FrameDecoder`。
- `sendPacket()` 编码 Packet，跨线程调用时投递回 owner `EventLoop`。
- `handleWrite()` 发送 output buffer，未写完的数据保留，写完后关闭写兴趣。
- `close()` / `closeInLoop()` 在 loop 线程移除 `Channel`、关闭 fd、清理缓冲并触发 close callback。
- `Channel` 对象释放延迟到当前事件回调栈帧之后，避免在 `Channel::handleEvent()` 正执行时析构当前 `Channel`。
- `last_active_time` 使用原子毫秒时间戳保存。
- `liteim_net` 链接 `liteim_protocol`，`src/CMakeLists.txt` 构建顺序调整为 base -> protocol -> net。

新增测试：

- `ReactorInterfaceTest.SessionHeaderIsSelfContained`
- `SessionTest.CompletePacketInvokesMessageCallback`
- `SessionTest.HalfPacketDoesNotInvokeMessageCallback`
- `SessionTest.StickyPacketsInvokeCallbackForEachPacket`
- `SessionTest.PeerCloseInvokesCloseCallback`
- `SessionTest.SendPacketFromOtherThreadDeliversEncodedPacket`
- `SessionTest.LargePacketLeavesPendingOutputWhenPeerDoesNotRead`
- `SessionTest.LastActiveTimeIsInitialized`

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "Session"
```

结果：8/8 Session tests passed。

全量验证已通过：

```bash
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
find . \( -path './server/net' -o -path './server/protocol' -o -name '*SQLite*' -o -name '*InMemory*' -o -name '*step15_sqlite*' \) -print
```

结果：server scaffold 正常输出监听配置，124/124 tests passed，diff whitespace check 无输出，旧路线文件路径检查无输出。

## 2026-05-07 Step 13 Hardening Round 3

本次任务继续复核 Step 13 hardening round 2 后的审阅反馈，仍然限定在 `EventLoop` / `Acceptor` / `Channel` 网络层边界内，不启动 Step 14。

采纳并修复：

- `EventLoop::isStopped()` 从 `!looping_` 推断改为显式 `loop_exited_` 状态，区分“尚未启动”和“已经退出”，也覆盖 `quit()` 早于第一次 `loop()` 的场景。
- 新增 `EventLoopTest.IsStoppedIsFalseBeforeLoopEverStarts`，覆盖 loop 刚构造但尚未进入 `loop()` 的状态语义。
- 新增 `EventLoopTest.IsStoppedIsFalseAfterQuitBeforeLoopEverStarts`，覆盖 `quit()` 早于第一次 `loop()` 时不能误判 stopped。
- 新增 `EventLoopTest.LoopRunsQueuedTaskWhenQuitWasRequestedBeforeStart`，覆盖 `quit()` 早于第一次 `loop()` 时仍要先执行已排队清理任务。
- 新增 `AcceptorTest.CloseFromOtherThreadBeforeLoopStartsStillRemovesChannel`，覆盖 loop 尚未启动时跨线程 close 仍必须回到 owner loop 删除 listen channel。
- `Acceptor` fd 用尽 helper 保持 `noexcept`，但 warn 日志改走内部 no-throw wrapper，避免 `spdlog` 异常触发 `std::terminate()`。
- `Channel.hpp`、Step 11 教程和 findings 补充 callback 不复制后的契约：未 `tie()` 的 callback 不能销毁当前 `Channel` 或重置正在执行的 callback；可能释放 owner 的场景必须调用 `tie()`。
- `Acceptor::rejectOneConnectionAfterFdExhaustion()` 内联 idle fd 重建逻辑，删除只被单点调用的 private helper，让 fd 用尽后的“腾 fd、拒绝连接、补回 idle fd”流程连续可读。
- `Channel::handleEvent()` 合并事件分发 private helper，让 `tie()` 得到的局部 `shared_ptr` guard 和事件分发 body 保持在同一个栈帧，生命周期边界更直观。

定向 RED 已确认：

```bash
ctest --test-dir build --output-on-failure -R "EventLoopTest.IsStoppedIsFalseBeforeLoopEverStarts|AcceptorTest.CloseFromOtherThreadBeforeLoopStartsStillRemovesChannel"
ctest --test-dir build --output-on-failure -R "EventLoopTest.IsStoppedIsFalseAfterQuitBeforeLoopEverStarts"
ctest --test-dir build --output-on-failure -R "EventLoopTest.LoopRunsQueuedTaskWhenQuitWasRequestedBeforeStart"
```

旧实现下新增回归会失败：未启动状态和提前 `quit()` 状态下 `isStopped()` 返回 true，提前 `quit()` 后启动 loop 时 pending task 不执行，Acceptor close-before-loop-start 测试抛出 `fd already belongs to a different channel`。

定向验证已通过：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "EventLoopTest.IsStoppedIsFalseBeforeLoopEverStarts|EventLoopTest.IsStoppedIsFalseAfterQuitBeforeLoopEverStarts|EventLoopTest.LoopRunsQueuedTaskWhenQuitWasRequestedBeforeStart|AcceptorTest.CloseFromOtherThreadBeforeLoopStartsStillRemovesChannel"
ctest --test-dir build --output-on-failure -R "EventLoopTest|AcceptorTest|ChannelTest|LoggerTest"
```

全量验证已通过：

```bash
./build/server/liteim_server
ctest --test-dir build --output-on-failure
git diff --check
```

结果：116/116 tests passed，server scaffold 正常输出监听配置，diff whitespace check 无输出。

## 2026-05-07 Step 13 Hardening Round 2

本次任务来自外部代码评价复核，目标是在不启动 Step 14 `Session` 的前提下，继续收口 Step 13 已暴露的网络层健壮性问题。

采纳并修复：

- `Logger::get()` 不再重置日志级别；新增回归测试覆盖 `init(Debug)` 和 `setLevel(Debug)` 后多次 `get()` 仍保持 Debug。
- `Epoller` 构造时如果 `epoll_create1()` 失败，直接抛出 `std::runtime_error`，避免半初始化对象继续参与 `EventLoop` 构造。
- `EventLoop` 的 wakeup fd 改为 `UniqueFd` 持有；`looping_` 改为原子状态并由 RAII guard 复位。
- `EventLoop::loop()` 捕获 `Channel` 回调和 pending task 异常并记录日志，单个业务回调不再直接杀死 loop。
- 新增 `EventLoop::isStopped()`，供停止后的跨线程资源清理判断。
- `Acceptor` 增加 idle fd 保护，`EMFILE` / `ENFILE` 时拒绝一个 pending connection，避免 LT 模式 busy loop。
- `Acceptor::close()` 在 loop 已停止时不再等待 queued task，改走直接释放资源的 fallback。
- `Channel::handleEvent()` 不再复制四个 `std::function`，依靠 `Channel::tie()` 保证 owner 生命周期。未 `tie()` 的 `Channel` 必须保证 callback 不会销毁自身或重置当前 callback。

未采纳：

- 暂不把 `EventLoop::updateChannel()` / `removeChannel()` 改成返回 `Status`，避免扩大接口迁移；当前本地系统/编程错误仍通过异常暴露。
- `FrameDecoder::feed()` 的 `vector::erase()` 继续留到 Step 14 `Session` 输入路径使用 `Buffer` 时处理。

新增/更新测试：

- `LoggerTest.GetDoesNotResetConfiguredLevel`
- `LoggerTest.SetLevelSurvivesLaterGetCalls`
- `ChannelTest.HandleEventDoesNotCopyStoredCallbacks`
- `EventLoopTest.ChannelCallbackExceptionDoesNotEscapeLoop`
- `EventLoopTest.IsStoppedBecomesTrueAfterLoopReturns`
- `AcceptorTest.CloseFromOtherThreadAfterLoopStopsDoesNotBlock`
- `AcceptorTest.FdExhaustionRejectsPendingConnectionWithoutLaterCallback`

定向验证已通过：

```bash
cmake --build build && ctest --test-dir build --output-on-failure -R "AcceptorTest.FdExhaustionRejectsPendingConnectionWithoutLaterCallback|AcceptorTest.CloseFromOtherThreadAfterLoopStopsDoesNotBlock|AcceptorTest.AcceptedFdIsClosedWhenCallbackThrowsBeforeTakingOwnership|EventLoopTest.ChannelCallbackExceptionDoesNotEscapeLoop|LoggerTest.GetDoesNotResetConfiguredLevel|ChannelTest.HandleEventDoesNotCopyStoredCallbacks"
```

全量验证已通过：

```bash
cmake -S . -B build && cmake --build build && ./build/server/liteim_server && ctest --test-dir build --output-on-failure
```

结果：112/112 tests passed。

## 2026-05-07 Step 13 Review Hardening

本次任务来自外部代码评价核对，目标是在不启动 Step 14 `Session` 的前提下，修复 Step 13 网络层中已经确认的局部风险。

确认结论：

- `Acceptor::close()` 非 loop 线程调用时没有从 `Epoller` 删除 `listen_channel_`，存在 stale `Channel*` 风险，需要回到 loop 线程执行关闭清理。
- `Acceptor::handleRead()` 中 accepted fd 缺少 RAII 所有权保护，callback 抛异常时可能泄漏 fd。
- `Channel` 需要补 `tie(std::shared_ptr<void>)` / `weak_ptr` 机制，为后续 `Session` 生命周期管理打基础。
- `EventLoop` 异常策略已在 hardening round 2 中收口；`FrameDecoder` 高吞吐输入消费方式留到后续 `Session` / `TcpServer` 设计中处理。

实施计划：

1. 先补 Acceptor / Channel 回归测试，并确认新测试在当前实现下失败。
2. 实现轻量 `UniqueFd`、Acceptor loop 内关闭、Channel tie。
3. 同步 README、docs、docs/tutorials、task_plan、findings、progress。
4. 运行 CMake build、server smoke、CTest、旧路线残留检查，并 review diff。

当前完成：

- 已新增 `include/liteim/net/UniqueFd.hpp` 和 `src/net/UniqueFd.cpp`。
- 已让 `Acceptor` 使用 `UniqueFd` 管理 listen fd 和 accepted fd。
- 已把 `Acceptor::close()` 的实际清理移动到 `closeInLoop()`，非 loop 线程调用时通过 `queueInLoop()` 投递并等待完成。
- 已新增 `Channel::tie()`，用 `weak_ptr` 保护 owner 生命周期。
- 已新增 `UniqueFdTest`、Acceptor hardening 回归测试和 Channel tie 回归测试。
- 定向验证已通过：`cmake --build build && ctest --test-dir build --output-on-failure -R "UniqueFdTest|ChannelTest|AcceptorTest|ReactorInterfaceTest.Channel"`，20/20 passed。
- 全量验证已通过：`cmake -S . -B build && cmake --build build && ./build/server/liteim_server && ctest --test-dir build --output-on-failure`，105/105 passed。
- 路径级旧路线残留检查无输出：没有 `server/net`、`server/protocol`、SQLite、InMemoryStorage 或 `step15_sqlite` 文件路径。
- README/docs/tutorials 仓库外 `PROJECT_MEMORY` 链接检查无输出；公开入口已改为 `docs/roadmap.md`。
- 最终 diff review 未发现需要继续修改的代码逻辑；`FrameDecoder` 输入 Buffer 化仍记录为后续 `Session` / `TcpServer` 设计事项。

## 2026-05-05 Step 0 Reset

本次进度是 `Step 0: reset workspace`，目标是把 LiteIM 文件夹清理成可以从零教学推进的最小起点。

## 已完成

- 删除旧源码目录：`include/`、`src/`、`server/`。
- 删除旧测试目录：`tests/`。
- 删除旧教程目录：`docs/tutorials/`。
- 删除旧文档目录：`docs/`。
- 删除旧 Qt 临时目录：`client_qt/`。
- 删除旧 SQL 目录：`sql/`。
- 删除旧构建产物：`build/`。
- 删除空的 `.codex` 临时文件。
- 删除未来 Step 才会使用的空目录和 `.gitkeep`。
- 将根 `CMakeLists.txt` 改成 Step 0 空 CMake 骨架。
- 重写 README、task_plan、findings、progress。
- 新增 `docs/architecture.md`、`docs/project_layout.md`、`docs/tutorials/README.md`、`docs/tutorials/step00_reset.md`。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md`，加入 Step 0 说明，并把 Qt 描述统一为常见 IM 三栏布局。

## 当前状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| Step 0 清理 | done | 旧路线代码、测试、教程和 build 产物已删除。 |
| Step 0 最小起点 | done | 不提前保留未来目录，也不保留 `.gitkeep`。 |
| Step 0 文档 | done | README、计划文件、docs 和 tutorial 已更新。 |
| Step 0 验证 | done | CMake、CTest、`.gitkeep` 检查和旧文件名检查已通过。 |
| Step 0 commit | done | 已提交：`29e41e9 chore: keep LiteIM step0 minimal`。 |
| Step 1 | pending | 下一步创建真正可构建的 server/test target。 |

## 下一步

验证 Step 0：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
find . -name .gitkeep
rg -n "SQLiteStorage|step15_sqlite|InMemoryStorage|server/net|server/protocol" .
```

然后进入 Step 1：`init: create LiteIM project structure with googletest`。

## Step 0 最小起点验证结果

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `ctest --test-dir build --output-on-failure`：通过，当前没有测试用例，符合 Step 0 预期。
- `find . -name .gitkeep`：无输出，说明没有 `.gitkeep` 残留。
- 旧路线文件名检查：无 `SQLite`、`InMemory`、`step15`、`server/net`、`server/protocol` 文件路径残留。

## 2026-05-05 Step 1 Project Init

本次进入 `Step 1: init CMake project structure`。

已完成：

- 创建 `server/` 和 `tests/` 目录。
- 更新根 `CMakeLists.txt`，接入 `server` 和 `tests` 子目录。
- 新增 `server/CMakeLists.txt` 和 `server/main.cpp`，生成最小 `liteim_server`。
- 新增 `tests/CMakeLists.txt` 和 `tests/test_main.cpp`，生成最小 `liteim_tests`。
- 将 Step 1 测试从自写 `main()` 改成 GoogleTest：`GTest::gtest_main` + `gtest_discover_tests` + `TEST(SmokeTest, GoogleTestWorks)`。
- 更新 README、docs、findings、task_plan 和 docs/tutorials。
- 新增 `docs/tutorials/step01_project_init.md`。

当前仍然没有创建 `include/`、`src/`、`client_qt/`、`bench/`、`scripts/`、`docker/` 等后续目录。

待验证：

```bash
cmake -S . -B build
cmake --build build
./build/server/liteim_server
ctest --test-dir build --output-on-failure
```

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过，生成 `liteim_server`、`liteim_tests`，并构建 GoogleTest / GoogleMock 依赖。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running.`。
- `ctest --test-dir build --output-on-failure`：通过，`SmokeTest.GoogleTestWorks` 1/1 passed。
- `find . -name .gitkeep`：无输出。
- 旧路线文件名检查：无 `SQLite`、`InMemory`、`step15`、`server/net`、`server/protocol`、`gitkeep` 文件路径残留。

提交：

```text
init: create LiteIM project structure with googletest
```


## 2026-05-05 PROJECT_MEMORY PersonaAgent Sync

用户更新了 `/home/yolo/jianli/PROJECT_MEMORY.md`，主要变化集中在项目二 PersonaAgent。

已同步到 LiteIM 文档的结论：

- PersonaAgent 新路线是 Authorized Style RAG Edition。
- PersonaAgent 保持 20 Step，但 Step 7-20 改为 6 节点 LangGraph + Knowledge/Memory/Authorized Style RAG + Persona + Safety + Tool Calling + Checkpoint + Trace + Evaluation。
- LiteIM 不嵌入 Python、LangGraph、LLM、embedding 或 vector DB，只提供 Python BotClient 可以复用的 TLV 协议和普通账号协议边界。
- Authorized Style RAG 样本必须有 consent manifest、来源、用途、脱敏、撤回和 SafetyGuard 边界。

## 2026-05-05 Step 2 Base Module

本次进入 `Step 2: add config logger and error code`。

概念完成：

- 明确 `base` 模块只放跨模块基础能力，不放业务和网络逻辑。
- `Config` 统一保存 server、MySQL、Redis、Qt 客户端默认连接配置。
- `Logger` 统一封装 `spdlog`，避免后续模块直接使用 `std::cout`。
- `ErrorCode` + `Status` 统一表达成功/失败，避免到处返回裸字符串。
- `Timestamp` 提供基础时间表达，后续消息、日志、压测统计可复用。

代码完成：

- 更新根 `CMakeLists.txt`，新增 `spdlog` v1.13.0 `FetchContent`，并添加 `src/` 子目录。
- 新增 `src/CMakeLists.txt` 和 `src/base/CMakeLists.txt`。
- 新增 `include/liteim/base/Config.hpp`、`ErrorCode.hpp`、`Status.hpp`、`Logger.hpp`、`Timestamp.hpp`。
- 新增 `src/base/Config.cpp`、`ErrorCode.cpp`、`Status.cpp`、`Logger.cpp`、`Timestamp.cpp`。
- 更新 `server/CMakeLists.txt`，让 `liteim_server` 链接 `liteim_base`。
- 更新 `server/main.cpp`，使用默认配置初始化日志并输出 `0.0.0.0:9000`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/base/` 下的 GoogleTest 文件。

测试完成：

- 新增 `tests/base/config_test.cpp`，覆盖默认配置、文件覆盖、缺失值保留默认值、缺失文件、未知 key、非法端口。
- 新增 `tests/base/error_code_test.cpp`，覆盖 `ErrorCode` 字符串和 `Status` 成功/失败状态。
- 新增 `tests/base/logger_test.cpp`，覆盖日志级别解析、未知级别回退和 logger 初始化。
- 新增 `tests/base/timestamp_test.cpp`，覆盖当前毫秒时间戳和 Unix epoch UTC 格式。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，15/15 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 docs/tutorials。
- 已新增 `docs/tutorials/step02_base.md`。
- 已删除临时 `build/` 产物。
- 提交完成：`feat(base): add config logger and error code`。

恢复说明：

- `planning-with-files` 的 `session-catchup.py` 提示了旧版 Buffer 纯问答上下文，但该内容属于重构前路线，当前 Step 2 以 `/home/yolo/jianli/PROJECT_MEMORY.md` 和当前 LiteIM 文件为准。

## 2026-05-05 Step 3 Protocol Types

本次进入 `Step 3: define MessageType and TLV types`。

概念完成：

- 明确 Step 3 只做协议枚举和分类，不做 Packet 编解码。
- `MessageType` 表示一帧消息的业务类型，后续会进入 Packet header 的 `msg_type` 字段。
- `TlvType` 表示 TLV body 中每个字段的类型，例如用户名、消息文本、群组 ID、错误信息和 Persona ID。
- `Push` 消息用于后续服务端主动投递给接收方，它既不是 request，也不是 response。
- 补充修正：`Push` 需要 `isPushType()` 显式分类，不能只靠 `!isRequestType() && !isResponseType()`，否则会和 `Unknown` 混在一起。

代码完成：

- 新增 `include/liteim/protocol/MessageType.hpp`。
- 新增 `include/liteim/protocol/Tlv.hpp`。
- 新增 `src/protocol/CMakeLists.txt`。
- 新增 `src/protocol/MessageType.cpp`。
- 新增 `src/protocol/Tlv.cpp`。
- 更新 `src/CMakeLists.txt`，接入 `protocol` 子目录。
- 更新 `tests/CMakeLists.txt`，让 `liteim_tests` 链接 `liteim_protocol`。
- 补充修正 `MessageType`：新增 `ListGroupsRequest` / `ListGroupsResponse`，并把群聊消息编号调整为 `406/407/408`。
- 补充 `isPushType()`，显式识别 `PrivateMessagePush` 和 `GroupMessagePush`。

测试完成：

- 新增 `tests/protocol/message_type_test.cpp`，覆盖核心消息类型字符串、未知消息类型、请求类型分类、响应类型分类、Push 类型分类和 Unknown 不分类。
- 新增 `tests/protocol/tlv_type_test.cpp`，覆盖核心 TLV 字段字符串和未知 TLV 类型。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，23/23 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 docs/tutorials。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 3 测试清单。
- 已删除临时 `build/` 产物。
- 提交完成：`feat(protocol): define message and tlv types`。

## 2026-05-05 Step 4 Packet Encoding

本次进入 `Step 4: implement Packet encode/decode`。

概念完成：

- 明确 Step 4 只实现 fixed Packet header 编解码，不实现 TLV body 字段编解码。
- `PacketHeader` 固定 20 字节，字段为 `magic`、`version`、`flags`、`msg_type`、`seq_id`、`body_len`。
- Header 多字节字段使用网络字节序，避免结构体 padding、CPU 字节序和内存对齐影响 wire format。
- `body_len` 最大 1MB，异常 header 直接返回错误。
- TCP 半包 / 粘包处理留给 Step 6 `FrameDecoder`。

代码完成：

- 新增 `include/liteim/protocol/Packet.hpp`。
- 新增 `src/protocol/Packet.cpp`。
- 更新 `src/protocol/CMakeLists.txt`，把 `Packet.cpp` 加入 `liteim_protocol`，并链接 `liteim_base`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/protocol/packet_test.cpp`。
- 新增 `PacketHeader`、`Packet`、`validateHeader()`、`encodePacket()` 和 `parseHeader()`。

测试完成：

- 新增 `tests/protocol/packet_test.cpp`，覆盖普通 Packet 编解码、空 body、UTF-8 body、网络字节序、错误 magic、错误 version、body_len 超限、encode 超大 body、不完整 header 和空指针输入。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，33/33 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 docs/tutorials。
- 已新增 `docs/tutorials/step04_packet.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 4 测试清单。
- 已删除临时 `build/` 产物。
- 提交完成：`feat(protocol): add packet encoding and header validation`。

## 2026-05-05 Step 5 TlvCodec

本次进入 `Step 5: implement TlvCodec`。

开始状态：

- 工作区已有用户未提交改动：`src/base/Config.cpp` 和 `src/protocol/Packet.cpp`。
- 这些改动主要是格式和注释调整，不属于 Step 5 范围；本 Step 保留它们，不纳入 Step 5 commit。

概念完成：

- 明确 Step 5 只实现 TLV body 字段编解码。
- TLV 格式固定为 `type(2 bytes) + len(4 bytes) + value(len bytes)`。
- `type` 和 `len` 使用网络字节序。
- 重复字段通过 `TlvMap` 保存为一个 type 对应多个 value。
- 缺失必需字段由 getter 返回 `NotFound`，不在通用 parser 中判断。
- TCP 半包 / 粘包处理留给 Step 6 `FrameDecoder`。

代码完成：

- 新增 `include/liteim/protocol/TlvCodec.hpp`。
- 新增 `src/protocol/TlvCodec.cpp`。
- 更新 `src/protocol/CMakeLists.txt`，把 `TlvCodec.cpp` 加入 `liteim_protocol`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/protocol/tlv_codec_test.cpp`。
- 新增 `appendString()`、`appendUint64()`、`parseTlvMap()`、`getString()`、`getUint64()`、`getRepeatedString()` 和 `getRepeatedUint64()`。

测试完成：

- 新增 `tests/protocol/tlv_codec_test.cpp`，覆盖单字段、多字段、UTF-8 字符串、重复字符串字段、重复 `uint64` 字段、`uint64` 网络字节序、TLV len 越界、不完整 TLV header、缺失字段、错误 `uint64` 长度和 Unknown 类型编码。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，45/45 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 docs/tutorials。
- 已新增 `docs/tutorials/step05_tlv_codec.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 5 测试清单。
- 本次按用户要求保留本地 `build/` 目录。
- 提交完成：`feat(protocol): implement tlv codec`。

## 2026-05-06 Step 5 TlvCodec API Correction

根据 Step 34 / Step 37 后续好友列表、群列表等真实用例，修正 Step 5 API：

- 删除暂时没有真实字段需求的 `appendInt64()`。
- 新增 `getRepeatedUint64()`，用于读取重复 `FriendId`、`UserId`、`GroupId`、`MessageId` 等 ID 列表。
- 保持 C++17 和当前项目 `Status + output parameter` 风格，不改成 C++20 `std::span` 或 `std::byte`。

## 2026-05-06 Step 6 FrameDecoder

本次进入 `Step 6: implement FrameDecoder`。

开始状态：

- 工作区已有用户暂存改动：`include/liteim/protocol/TlvCodec.hpp`、`src/base/Config.cpp` 和 `src/protocol/Packet.cpp`。
- Step 6 不修改这 3 个文件，不把它们纳入 Step 6 commit。

概念完成：

- 明确 TCP 是字节流，不保留消息边界。
- `FrameDecoder` 只负责连续字节流到完整 `Packet` 的解包。
- 本 Step 不读 socket、不调用 epoll、不解析 TLV body、不做业务路由。
- 错误 header 进入 error 状态，等待上层关闭连接或显式 `reset()`。

代码完成：

- 新增 `include/liteim/protocol/FrameDecoder.hpp`。
- 新增 `src/protocol/FrameDecoder.cpp`。
- 更新 `src/protocol/CMakeLists.txt`，把 `FrameDecoder.cpp` 加入 `liteim_protocol`。
- 更新 `tests/CMakeLists.txt`，加入 `tests/protocol/frame_decoder_test.cpp`。
- 新增 `FrameDecoder::feed()`、`hasError()`、`bufferedBytes()` 和 `reset()`。

测试完成：

- 新增 `tests/protocol/frame_decoder_test.cpp`，覆盖完整包、半包、粘包、半包后接粘包、错误 magic、错误 version、body_len 超限、error 状态拒绝继续解析和空指针输入。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，54/54 tests passed。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 docs/tutorials。
- 已新增 `docs/tutorials/step06_frame_decoder.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 6 测试清单。
- 本次按用户要求保留本地 `build/` 目录。
- 提交完成：`feat(protocol): implement tcp frame decoder`。

## 2026-05-06 Step 7 Buffer

本次进入 `Step 7: add buffer abstraction`。

概念完成：

- 明确 `Buffer` 是网络层通用字节缓冲区，不读 socket、不调用 epoll、不管理连接生命周期。
- `Buffer` 后续会作为 `Session` 的输入缓冲区和输出缓冲区底座。
- `read_index_` 和 `write_index_` 用来区分已读区域、可读区域和可写区域。
- `ensureWritableBytes()` 优先复用已读空间，空间仍不足时再扩容。
- `retrieve()` 越界返回 `InvalidArgument`，不使用进程级断言处理线上输入错误。

代码完成：

- 新增 `include/liteim/net/Buffer.hpp`。
- 新增 `src/net/CMakeLists.txt`。
- 新增 `src/net/Buffer.cpp`。
- 更新 `src/CMakeLists.txt`，接入 `net` 子目录。
- 新增 `tests/net/buffer_test.cpp`。
- 更新 `tests/CMakeLists.txt`，让 `liteim_tests` 链接 `liteim_net`。

测试完成：

- 新增 `BufferTest.DefaultBufferHasNoReadableBytes`。
- 新增 `BufferTest.AppendIncreasesReadableBytes`。
- 新增 `BufferTest.AppendStringStoresReadableData`。
- 新增 `BufferTest.AppendBytePointerStoresBytes`。
- 新增 `BufferTest.RetrieveAdvancesReadIndex`。
- 新增 `BufferTest.RetrieveAllResetsBuffer`。
- 新增 `BufferTest.RetrieveAllAsStringReturnsReadableDataAndClearsBuffer`。
- 新增 `BufferTest.AppendExpandsWhenNeeded`。
- 新增 `BufferTest.AppendCompactsReadableDataBeforeExpanding`。
- 新增 `BufferTest.AppendExpandsAndPreservesExistingData`。
- 新增 `BufferTest.RetrievePastReadableBytesReturnsError`。
- 新增 `BufferTest.NullAppendWithNonzeroLengthReturnsError`。
- 新增 `BufferTest.NullAppendWithZeroLengthIsOk`。

验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，67/67 tests passed。
- `git diff --check`：通过。
- `find . -name .gitkeep`：无输出。
- 旧路线文件名检查：没有恢复旧 `SQLiteStorage`、`step15_sqlite`、真实 `server/net` 或 `server/protocol` 文件路径。

收尾完成：

- 已更新 README、docs、findings、task_plan、progress 和 docs/tutorials。
- 已新增 `docs/tutorials/step07_buffer.md`。
- 已更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 7 测试清单。
- 本次按用户要求保留本地 `build/` 目录。
- 提交完成：`feat(net): add buffer abstraction`。

## 2026-05-06 Step 8 SocketUtil

本次进入 `Step 8: implement SocketUtil`。

开始状态：

- 工作区干净。
- 当前新路线中 Step 7 `Buffer` 已完成，下一步是 `SocketUtil`。
- 旧记忆里曾出现过 Step 8 `EventLoop`，但那属于重启前路线；本次以 `/home/yolo/jianli/PROJECT_MEMORY.md` 和当前 `docs/process/task_plan.md` 为准。

概念进行中：

- `SocketUtil` 只封装 Linux socket 常用系统调用。
- 本 Step 不实现 `epoll`、`Channel`、`EventLoop`、`Acceptor`、`Session` 或 `TcpServer`。

概念完成：

- 明确 `SocketUtil` 是 Linux fd 工具层，不拥有连接生命周期。
- `createNonBlockingSocket()` 负责创建 `AF_INET` / `SOCK_STREAM` / `SOCK_NONBLOCK` / `SOCK_CLOEXEC` socket。
- `setNonBlocking()` 使用 `fcntl(F_GETFL)` + `fcntl(F_SETFL)` 补充设置非阻塞。
- socket option 封装只设置常用选项，不在工具函数里绑定地址、监听端口或退出进程。
- `closeFd()` 接收 fd 引用，关闭前保存当前 fd，然后把变量置为 `kInvalidFd`，避免同一变量重复关闭。

代码完成：

- 新增 `include/liteim/net/SocketUtil.hpp`。
- 新增 `src/net/SocketUtil.cpp`。
- 更新 `src/net/CMakeLists.txt`，把 `SocketUtil.cpp` 加入 `liteim_net`。
- 新增 `createNonBlockingSocket()`、`setNonBlocking()`、`setReuseAddr()`、`setReusePort()`、`setTcpNoDelay()`、`setKeepAlive()`、`closeFd()` 和 `getSocketError()`。

测试完成：

- 新增 `tests/net/socket_util_test.cpp`。
- 新增 `SocketUtilTest.CreateNonBlockingSocketReturnsNonblockingFd`。
- 新增 `SocketUtilTest.SetNonBlockingMarksPlainSocketNonblocking`。
- 新增 `SocketUtilTest.SocketOptionsCanBeEnabled`。
- 新增 `SocketUtilTest.InvalidFdReturnsError`。
- 新增 `SocketUtilTest.CloseFdInvalidatesDescriptorAndCanBeRepeated`。
- 新增 `SocketUtilTest.GetSocketErrorReturnsCurrentSoError`。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，73/73 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): add socket utility functions`。

## 2026-05-06 Step 10 Epoller

本次进入 `Step 10: implement Epoller`。

开始状态：

- LiteIM 仓库当前 HEAD 是 `cdfaa14 feat(net): define reactor core interfaces`。
- 工作区干净。
- `session-catchup.py` 提示的未同步内容仍来自旧的纯概念问答，不对应当前 Step 10 代码改动。

概念进行中：

- `Epoller` 是 Reactor 的系统调用层，只封装 `epoll_create1()`、`epoll_ctl()` 和 `epoll_wait()`。
- `Channel` 仍是 fd/event 状态对象，不拥有 fd。
- 本 Step 会补最小 `Channel.cpp` 状态函数以支撑 `Epoller` 测试，但不实现 `Channel::handleEvent()` 回调分发或自动 `EventLoop` 更新。
- 为满足“无效操作返回错误”，`Epoller` 接口会改为返回 `Status`，并通过输出参数返回 active channel list。

TDD RED：

- 更新 `tests/net/epoller_header_test.cpp`，要求 `Epoller::poll()`、`updateChannel()`、`removeChannel()` 返回 `Status`。
- 新增 `tests/net/epoller_test.cpp`，使用真实 `pipe()` fd 覆盖 add、mod、del、timeout 和无效操作。
- 更新 `tests/CMakeLists.txt` 注册 `epoller_test.cpp`。
- 运行 `cmake --build build`，预期失败于 `EpollerHeaderIsSelfContained` 的 `static_assert`，证明当前接口还不满足 Step 10 错误返回要求。

代码完成：

- 更新 `include/liteim/net/Epoller.hpp`，让 `poll()`、`updateChannel()`、`removeChannel()` 返回 `Status`。
- 新增 `src/net/Epoller.cpp`，实现 `epoll_create1(EPOLL_CLOEXEC)`、`EPOLL_CTL_ADD`、`EPOLL_CTL_MOD`、`EPOLL_CTL_DEL` 和 `epoll_wait()`。
- 新增 `src/net/Channel.cpp`，只实现构造、fd/event/revent 访问、读写事件 mask 修改和回调 setter。
- 更新 `src/net/CMakeLists.txt`，把 `Channel.cpp` 和 `Epoller.cpp` 加入 `liteim_net`。
- 本 Step 仍未实现 `Channel::handleEvent()`、`Channel::update()`、`EventLoop::loop()` 或 `eventfd`。

TDD GREEN：

- 运行 `cmake --build build`：通过。
- 运行 `ctest --test-dir build --output-on-failure -R Epoller`：通过，6/6 tests passed。

文档完成：

- 更新 README，把当前状态切到 Step 10，并补充 `Epoller.cpp`、`Channel.cpp`、LT 模式、`Status` 返回和 81 个测试总数。
- 更新 `docs/architecture.md`，补充 `Epoller` 系统调用层行为和当前边界。
- 更新 `docs/project_layout.md`，补充 Step 10 新增源码、测试文件和教程。
- 更新 `docs/tutorials/README.md`，登记 Step 10 教程。
- 更新 `docs/tutorials/step09_reactor_interfaces.md` 中 `Epoller` 的当前 `Status` 接口签名，避免教程和代码漂移。
- 新增 `docs/tutorials/step10_epoller.md`，按概念、接口、实现规则、测试和面试问答展开说明。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 10 测试清单。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，81/81 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): implement epoller wrapper`。

## 2026-05-06 Step 9 Reactor Interfaces

本次进入 `Step 9: define Epoller / Channel / EventLoop interface`。

开始状态：

- 工作区干净。
- 当前新路线中 Step 8 `SocketUtil` 已完成，下一步是 Reactor 核心接口。
- `session-catchup.py` 提示的未同步消息来自之前纯概念问答，没有项目文件改动，本次不把它当作待同步实现内容。

概念进行中：

- `Channel` 表示一个 fd 的事件代理，保存关注事件和回调接口，但本 Step 不实现回调分发。
- `Epoller` 表示 epoll 系统调用封装边界，但本 Step 不调用 `epoll_create1()`、`epoll_ctl()` 或 `epoll_wait()`。
- `EventLoop` 表示 Reactor 调度层接口，拥有 `Epoller` 并管理 `Channel`，但本 Step 不实现阻塞事件循环和 `eventfd` 唤醒。

TDD RED：

- 新增 `tests/net/channel_header_test.cpp`、`tests/net/epoller_header_test.cpp`、`tests/net/event_loop_header_test.cpp`。
- 更新 `tests/CMakeLists.txt` 注册三个接口编译测试。
- 运行 `cmake --build build`，预期失败于 `fatal error: liteim/net/Channel.hpp: No such file or directory`，证明测试能捕获 Step 9 头文件尚未定义的问题。

代码完成：

- 新增 `include/liteim/net/Channel.hpp`，声明 fd、关注事件、实际事件、事件开关、回调设置和 `handleEvent()` 接口。
- 新增 `include/liteim/net/Epoller.hpp`，声明 `poll()`、`updateChannel()` 和 `removeChannel()` 接口。
- 新增 `include/liteim/net/EventLoop.hpp`，声明 `loop()`、`quit()`、`updateChannel()`、`removeChannel()` 和线程归属检查接口。
- 本 Step 没有新增 `src/net/Epoller.cpp`、`Channel.cpp` 或 `EventLoop.cpp`，也没有实现 epoll 系统调用。

TDD GREEN：

- 运行 `cmake --build build`：通过。
- 运行 `ctest --test-dir build --output-on-failure -R ReactorInterfaceTest`：通过，3/3 tests passed。

文档完成：

- 更新 README，把当前状态切到 Step 9，并补充 `Channel` / `Epoller` / `EventLoop` 接口说明和 76 个测试总数。
- 更新 `docs/architecture.md`，补充当前网络层中的 Reactor 接口边界。
- 更新 `docs/project_layout.md`，补充 Step 9 新增头文件、测试文件和教程。
- 更新 `docs/tutorials/README.md`，登记 Step 9 教程。
- 新增 `docs/tutorials/step09_reactor_interfaces.md`，按概念、接口、边界、测试、面试讲法展开说明。
- 更新 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 9 文件清单和测试清单。

错误记录：

- 一次 stale 文案扫描命令把包含反引号的 pattern 放在双引号里，shell 将反引号内容当作命令替换，出现 `Step: command not found`。已改用单引号重新执行，确认只有 Step 8 历史教程保留自身测试说明，不属于 stale 当前状态。
- `/home/yolo/jianli` 不是 Git 仓库，`PROJECT_MEMORY.md` 是工作区级元数据，不能纳入 `LiteIM` 仓库 commit；LiteIM commit 只会包含仓库内 Step 9 文件。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，76/76 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): define reactor core interfaces`。

## 2026-05-06 Step 11 Channel

本次进入 `Step 11: implement Channel`。

开始状态：

- 当前新路线中 Step 10 `Epoller` 已完成并提交。
- 工作区干净。
- `session-catchup.py` 提示的未同步内容来自旧的纯概念问答，不对应当前 Step 11 代码改动。
- 旧记忆里的 Step 11 `Session` 属于重启前路线；本次以 `/home/yolo/jianli/PROJECT_MEMORY.md` 当前 Step 11 `Channel` 为准。

概念完成：

- `Channel` 是 Reactor 中的 fd 事件分发器，不拥有 fd，不关闭 fd。
- `events_` 表示希望监听的事件，`revents_` 表示本轮 epoll 实际返回的事件。
- `handleEvent()` 根据 `revents_` 分发 read/write/close/error callback。
- `enableReading()`、`enableWriting()` 等关注事件变化函数需要通过 `EventLoop` 更新 epoll 关注状态。
- 本 Step 只补最小 `EventLoop` 更新桥接，不实现 `EventLoop::loop()`、`eventfd` 或跨线程任务队列。

TDD RED：

- 新增 `tests/net/channel_test.cpp`，覆盖事件 mask 修改和 read/write/close/error 回调分发。
- 更新 `tests/CMakeLists.txt` 注册 `channel_test.cpp`。
- 运行 `cmake --build build`，预期失败于 `undefined reference to liteim::Channel::handleEvent()`，证明新增测试能捕获当前缺失行为。

代码完成：

- 更新 `src/net/Channel.cpp`，实现 `handleEvent()` 和私有 `update()`。
- `handleEvent()` 处理 `EPOLLHUP`、`EPOLLERR`、`EPOLLIN`、`EPOLLPRI`、`EPOLLRDHUP` 和 `EPOLLOUT`。
- `handleEvent()` 只拷贝 active events，不复制 callbacks；owner 生命周期由 Step 13 的 `Channel::tie()` 接管，未 `tie()` 的 callback 必须保证不会销毁当前 `Channel` 或重置正在执行的 callback。
- `enableReading()`、`disableReading()`、`enableWriting()`、`disableWriting()` 和 `disableAll()` 修改 `events_` 后调用 `update()`。
- 新增 `src/net/EventLoop.cpp`，只实现 `updateChannel()` / `removeChannel()` 到 `Epoller` 的桥接、`quit()` 和线程归属检查。
- 更新 `src/net/CMakeLists.txt`，把 `EventLoop.cpp` 加入 `liteim_net`。

TDD GREEN：

- 运行 `cmake --build build`：通过。
- 运行 `ctest --test-dir build -R ChannelTest --output-on-failure`：通过，7/7 tests passed。

文档完成：

- 更新 README，把当前状态切到 Step 11，并补充 `Channel::handleEvent()`、`Channel::update()`、`EventLoop.cpp` 最小桥接和 88 个测试总数。
- 更新 `docs/architecture.md`，补充 `Channel` 事件分发和当前 `EventLoop` 边界。
- 更新 `docs/project_layout.md`，补充 Step 11 新增源码、测试文件和教程。
- 更新 `docs/tutorials/README.md`，登记 Step 11 教程。
- 新增 `docs/tutorials/step11_channel.md`，按概念、接口、实现规则、测试和面试问答展开说明。
- 更新 `docs/process/findings.md`、`docs/process/task_plan.md` 和 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 11 记录。

错误记录：

- 一次 stale 文案扫描命令又把包含反引号的 pattern 放进双引号，shell 将反引号内容当成命令替换，出现 `Step: command not found`、`Channel::handleEvent: command not found` 等输出。已改用单引号重新执行，确认命中项只是 Step 10 历史进度和 Step 10 计划段落，不是当前状态文案。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，88/88 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): implement channel event dispatching`。

## 2026-05-06 Step 12 EventLoop + eventfd

本次进入 `Step 12: implement EventLoop + eventfd task dispatch`。

开始状态：

- 当前新路线中 Step 11 `Channel` 已完成并提交。
- LiteIM 工作区干净。
- `session-catchup.py` 提示的未同步内容来自旧的纯概念问答，不对应当前 Step 12 代码改动。
- 旧记忆里的 Step 12 `TcpServer` 属于重启前路线；本次以 `/home/yolo/jianli/PROJECT_MEMORY.md` 当前 Step 12 `EventLoop + eventfd` 为准。

概念完成：

- `EventLoop` 是 Reactor 调度层，负责持有 `Epoller`、阻塞等待 fd 事件、遍历活跃 `Channel` 并调用 `handleEvent()`。
- 每个 `EventLoop` 内置一个 `eventfd` wakeup channel，用于跨线程唤醒阻塞中的 `epoll_wait()`。
- `runInLoop()` 在所属线程立即执行任务，跨线程调用时转入 `queueInLoop()`。
- `queueInLoop()` 把任务放进 mutex 保护的队列，并在需要时写 eventfd 唤醒 loop。
- 本 Step 不实现 `Acceptor`、`Session`、`TcpServer`、业务线程池、MySQL 或 Redis。

TDD RED：

- 更新 `tests/net/event_loop_header_test.cpp`，要求 `EventLoop::Functor`、`runInLoop()` 和 `queueInLoop()` 存在。
- 新增 `tests/net/event_loop_test.cpp`，使用真实线程、真实 pipe fd 和真实 eventfd wakeup 路径验证事件循环。
- 更新 `tests/CMakeLists.txt` 注册 `event_loop_test.cpp`。
- 运行 `cmake --build build`，预期失败于 `EventLoop::Functor does not name a type`、`runInLoop is not a member of EventLoop` 和 `queueInLoop is not a member of EventLoop`。

代码完成：

- 更新 `include/liteim/net/EventLoop.hpp`，新增 `Functor`、`runInLoop()`、`queueInLoop()`、wakeup fd、wakeup channel、pending task 队列、mutex 和 pending-task 执行标记。
- 更新 `src/net/EventLoop.cpp`，实现 `eventfd` 创建、wakeup channel 注册、`loop()`、`quit()`、`runInLoop()`、`queueInLoop()`、`wakeup()`、`handleWakeup()` 和 `doPendingTasks()`。
- `loop()` 先执行 pending tasks，再阻塞 `Epoller::poll(-1)`，返回后遍历活跃 `Channel` 并调用 `handleEvent()`，随后再次执行 pending tasks。
- `quit()` 跨线程调用时写 eventfd，保证阻塞中的 loop 能醒来退出。
- `EventLoop` 析构时从 `Epoller` 移除内部 wakeup channel，并关闭 wakeup fd。

TDD GREEN：

- `cmake --build build`：通过。
- `ctest --test-dir build -R EventLoop --output-on-failure`：通过，5/5 tests passed。

文档完成：

- 新增 `docs/tutorials/step12_event_loop.md`，说明 EventLoop、eventfd、任务队列、线程边界、测试和面试问答。
- 更新 `docs/process/findings.md` 记录 Step 12 约束和设计结论。
- 更新 README，把当前状态切到 Step 12，并补充 `EventLoop::loop()`、`runInLoop()`、`queueInLoop()`、eventfd wakeup 和 92 个测试总数。
- 更新 `docs/architecture.md`，补充当前网络层中的 `EventLoop` 职责、eventfd wakeup 和线程边界。
- 更新 `docs/project_layout.md`，补充 Step 12 新增测试文件和教程。
- 更新 `docs/tutorials/README.md`，登记 Step 12 教程。
- 更新 `docs/process/task_plan.md` 和 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 12 记录。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，92/92 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): add event loop with eventfd task queue`。

## 2026-05-07 Step 13 Acceptor

本次进入 `Step 13: implement Acceptor`。

开始状态：

- 当前新路线中 Step 12 `EventLoop + eventfd` 已完成并提交。
- 用户随后单独提交了 EventLoop 注释改动：`docs(net): add event loop explanatory comments`。
- `session-catchup.py` 提示的未同步内容来自旧的纯概念问答，不对应当前 Step 13 代码改动。
- 旧记忆里的 Step 13 可能和重启前路线不一致；本次以 `/home/yolo/jianli/PROJECT_MEMORY.md` 当前 Step 13 `Acceptor` 为准。

概念完成：

- `Acceptor` 是主 Reactor 上的监听器，只负责 listen socket 和新连接接收。
- `Acceptor` 不创建 `Session`，不解析 Packet/TLV，不做登录、聊天、MySQL 或 Redis。
- 构造时创建非阻塞 listen fd，设置 socket option，完成 `bind()` / `listen()`，并用 `Channel` 注册读事件到所属 `EventLoop`。
- listen fd 可读时循环 `accept4()`，直到 `EAGAIN` / `EWOULDBLOCK`。
- 新连接 fd 通过 callback 交给后续 `TcpServer`；如果没有 callback，立即关闭 accepted fd，避免泄漏。

TDD RED：

- 新增 `tests/net/acceptor_header_test.cpp`，要求 `liteim/net/Acceptor.hpp` 自包含，并声明 `NewConnectionCallback`、`listenFd()`、`port()`、`listening()` 和 `close()`。
- 新增 `tests/net/acceptor_test.cpp`，使用真实 `127.0.0.1` socket 连接验证监听、callback、多个 pending connection 和 close 行为。
- 更新 `tests/CMakeLists.txt` 注册 Acceptor 测试。
- 运行 `cmake --build build`，预期失败于 `fatal error: liteim/net/Acceptor.hpp: No such file or directory`。

代码完成：

- 新增 `include/liteim/net/Acceptor.hpp`，声明 `Acceptor`、`NewConnectionCallback`、状态查询和关闭接口。
- 新增 `src/net/Acceptor.cpp`，实现 `createNonBlockingSocket()`、`setReuseAddr()`、`setReusePort()`、`bind()`、`listen()`、listen channel 注册和 `accept4(SOCK_NONBLOCK | SOCK_CLOEXEC)` 循环。
- 更新 `src/net/CMakeLists.txt`，把 `Acceptor.cpp` 加入 `liteim_net`。
- `Acceptor::close()` 从所属 `EventLoop` 移除 listen channel，并关闭 listen fd。
- `handleRead()` 不标记 `noexcept`，因为它会调用用户提供的 callback，异常不应在库边界被强制转成 `std::terminate()`。

TDD GREEN：

- `cmake --build build`：通过。
- `ctest --test-dir build -R Acceptor --output-on-failure`：通过，5/5 tests passed。

文档完成：

- 新增 `docs/tutorials/step13_acceptor.md`，说明 Acceptor、listen fd、`accept4()`、callback、close 边界和测试。
- 更新 README，把当前状态切到 Step 13，并补充 Acceptor 文件、职责和 97 个测试总数。
- 更新 `docs/architecture.md`，补充 `Acceptor` 在当前网络层中的职责边界。
- 更新 `docs/project_layout.md`，补充 Step 13 新增头文件、源码、测试文件和教程。
- 更新 `docs/tutorials/README.md`，登记 Step 13 教程。
- 更新 `docs/process/findings.md`、`docs/process/task_plan.md` 和 `/home/yolo/jianli/PROJECT_MEMORY.md` 的 Step 13 记录。

阶段验证结果：

- `cmake -S . -B build`：通过。
- `cmake --build build`：通过。
- `./build/server/liteim_server`：通过，输出 `LiteIM server scaffold is running on 0.0.0.0:9000`。
- `ctest --test-dir build --output-on-failure`：通过，97/97 tests passed。
- `git diff --check`：通过。
- `find . -path ./build -prune -o -path ./.git -prune -o -name .gitkeep -print`：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 路径残留。

收尾完成：

- 提交完成：`feat(net): implement nonblocking acceptor`。

## 2026-05-09 Markdown Cleanup Correction

User confirmed that the previous markdown simplification deleted too much useful memory. This correction does not rewrite history or reset the branch; it adds a follow-up commit after `d6a3830 docs: simplify LiteIM markdown memory`.

Changes made:

- Restored `docs/debug_cases/thread_pool_worker_stop.md` from `HEAD^`.
- Restored `docs/debug_cases/net_lifecycle_review_hardening.md` from `HEAD^`.
- Restored `docs/process/task_plan.md`, `docs/process/findings.md`, and `docs/process/progress.md` from `HEAD^`, then appended this correction record.
- Kept `docs/architecture.md`, `docs/project_layout.md`, and `docs/roadmap.md` deleted.
- Deleted `docs/tutorials/README.md` and removed current tutorial references to it.
- Rewrote `README.md` as a GitHub-facing LiteIM overview without `Current Status` / `当前状态` headings and without Codex process memory as public documentation.
- Updated `/home/yolo/jianli/PROJECT_MEMORY.md` and `/home/yolo/jianli/AGENTS.md` to state that `docs/debug_cases/` is useful internal retrospective material.

Verification completed:

- `find docs -type f | sort` printed only the two debug case files.
- `find docs/tutorials -maxdepth 1 -type f -name 'README.md'` produced no output.
- `rg -n "Current Status|当前状态" README.md` produced no output.
- `rg -n "docs/tutorials/README|docs/architecture|docs/project_layout|docs/roadmap" README.md docs/tutorials /home/yolo/jianli/AGENTS.md /home/yolo/jianli/PROJECT_MEMORY.md` produced no output.
- `cmake -S . -B build` passed.
- `cmake --build build` passed.
- `./build/server/liteim_server` passed and printed `LiteIM server scaffold is running on 0.0.0.0:9000`.
- `ctest --test-dir build --output-on-failure` passed `164/164`.
- `git diff --check` produced no output.
- `.gitkeep` and stale SQLite / `InMemoryStorage` / old `server/net` path scans produced no output.

## 2026-05-16 Step 40 HeartbeatService

本次进入 `Step 40: implement HeartbeatService`。

开始状态：

- 当前新路线中 Step 39 `HistoryService` 已完成。
- `session-catchup.py` 提示的未同步内容来自纯概念问答，不对应当前 Step 40 代码改动。
- 工作区进入 Step 40 前已有用户侧未提交注释改动：`include/liteim/service/GroupService.hpp`、`src/service/GroupService.cpp`、`src/storage/GroupDao.cpp`。本 Step 不覆盖这些改动，提交时保持和 Step 40 行为改动分离。
- 用户确认采用方案 B：Redis TTL 刷新失败时记录降级 warning，仍返回 `HeartbeatResponse`。

概念计划：

- `HeartbeatResponse` 只表示服务端成功收到并处理合法心跳包，不保证 Redis 在线状态 TTL 一定刷新成功。
- 未登录心跳直接返回成功，不写 Redis。
- 已登录心跳在 business 线程池中调用 `OnlineService::refreshUserOnline(user_id, session_id)` 刷新 Redis 在线 TTL。
- Redis TTL 刷新失败只记录 warning，不返回 `ErrorResponse`，不关闭连接，不清理 SessionManager 绑定。
- `HeartbeatService` 不直接更新 `Session::last_active_time`；完整合法入站 packet 已经在 `Session` 读路径刷新连接活跃时间。

TDD RED：

- 新增 `tests/service/heartbeat_service_test.cpp`，覆盖 header 自包含、未登录心跳成功、已登录刷新 Redis TTL、响应保留 `seq_id`、Redis 刷新失败仍返回 `HeartbeatResponse`、router 注册后覆盖默认 inline 心跳并刷新 TTL。
- 更新 `tests/CMakeLists.txt` 注册新测试文件。
- 运行 `cmake --build build --target liteim_tests -j2`，预期失败于 `fatal error: liteim/service/HeartbeatService.hpp: No such file or directory`。

代码完成：

- 新增 `include/liteim/service/HeartbeatService.hpp` 和 `src/service/HeartbeatService.cpp`。
- `HeartbeatService` 注册 `HeartbeatRequest` 到 business thread dispatch。
- `handleHeartbeat()` 对未登录连接直接返回 `HeartbeatResponse`，对已登录连接尝试刷新 Redis TTL。
- Redis TTL 刷新失败时通过 `Logger` 记录 warning，仍返回成功。
- 更新 `src/service/CMakeLists.txt` 和 `server/main.cpp`，把 `HeartbeatService` 编进 `liteim_service` 并在运行时注册。

TDD GREEN：

- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -R HeartbeatService --output-on-failure`：通过，6/6 tests passed。

文档进行中：

- 更新 `README.md`：当前 runtime 切到 Step 40，记录 `HeartbeatService` 和 Redis TTL 降级语义。
- 新增 `docs/tutorials/step40_heartbeat_service.md`，按固定 0-10 模板讲解心跳业务层语义。
- 更新 `docs/process/task_plan.md` / `docs/process/findings.md` / `docs/process/progress.md` 记录 Step 40 边界、设计发现和 RED/GREEN 过程。

阶段验证结果：

- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -R HeartbeatService --output-on-failure`：通过，6/6 tests passed。
- `cmake --build build -j2`：通过。
- `ctest --test-dir build -R "HeartbeatService|MessageRouter|OnlineService" --output-on-failure`：通过，22/22 tests passed。
- `docker compose -f docker/docker-compose.yml up -d --wait`：通过，MySQL / Redis 均 healthy。
- `ctest --test-dir build --output-on-failure`：通过，327/327 tests passed。
- `git diff --check`：通过。
- `.gitkeep` 检查：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 残留。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" README.md docs/tutorials/step40_heartbeat_service.md`：无输出。
- `rg -n "^## " docs/tutorials/step40_heartbeat_service.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。
- `timeout 2s ./build/server/liteim_server || test $? -eq 124`：通过，server 监听 `0.0.0.0:9000` 后收到 SIGTERM 并通过 signalfd 退出。

收尾注意：

- Step 40 提交应排除进入本 Step 前已有的用户侧注释改动：`include/liteim/service/GroupService.hpp`、`src/service/GroupService.cpp`、`src/storage/GroupDao.cpp`。

收尾完成：

- 提交完成：`feat(service): add heartbeat ttl refresh`。


## 2026-05-16 Step 41 CLI Client

本次进入 `Step 41：实现 CLI 测试客户端`。

开始状态：

- 旧 assistant 路线后来已被移除。
- 工作区仍保留 进入该任务前已有的用户侧注释改动：`include/liteim/service/GroupService.hpp`、`src/service/GroupService.cpp`、`src/storage/GroupDao.cpp`。本 Step 不应把这些注释改动混入提交。
- Step 41 目标是先提供协议调试 CLI，不提前实现 Python E2E、bench、Qt 或 PersonaAgent。

概念计划：

- 新增 `client_cli/` 目录。
- `liteim_client_cli` 静态 helper 库承载命令解析、TLV Packet 构造、Packet 描述和阻塞 TCP client。
- `liteim_cli` 可执行文件负责解析 `--host` / `--port`，启动接收线程、心跳线程，并从 stdin 读取命令。
- 第一版 CLI 采用普通文本命令和调试输出，不做 curses/TUI、本地状态数据库或复杂重连。

TDD RED：

- 新增 `tests/client_cli/cli_protocol_test.cpp`，覆盖 login/private/history 命令解析、push 描述输出，以及 `ProtocolClient` 连接本地 fake TCP server 并发送 heartbeat packet。
- 更新根 `CMakeLists.txt` 和 `tests/CMakeLists.txt`。
- `cmake --build build --target liteim_tests -j2` 按预期失败：`add_subdirectory given source "client_cli" which is not an existing directory`。

代码完成：

- 新增 `client_cli/CMakeLists.txt`，生成 `liteim_client_cli` 静态库和 `liteim_cli` 可执行文件。
- 新增 `buildPacketFromLine()`，Step41 第一版支持 register/login/add-friend/friends/private/create-group/join-group/groups/group/history/offline/heartbeat 命令；后续可靠性和 Post-Step58 audit 已补 register/login/logout/add-friend/accept-friend/reject-friend/friends/private/private-id/create-group/join-group/groups/group/history/offline/offline-ack/delivery-ack/heartbeat 命令构造普通 TLV `Packet`。
- 新增 `describePacket()`，把 response / push 中常见 TLV 字段打印成人可读调试文本。
- 新增 `ProtocolClient`，提供阻塞 TCP connect/send/read/close，`close()` 使用 `shutdown()` 方便退出时打断接收线程。
- 新增 `client_cli/main.cpp`，支持 `--host` / `--port`、交互/stdin 命令、后台接收线程和 30 秒心跳线程。

TDD GREEN：

- `cmake --build build --target liteim_tests -j2`：通过。
- `./build/tests/liteim_tests --gtest_filter='ClientCli*'`：5/5 通过。
- `cmake --build build --target liteim_cli -j2`：通过。
- `./build/client_cli/liteim_cli --help`：通过，输出用法。

文档完成：

- 更新 `README.md`：记录 `liteim_cli`、命令示例、Step 41 runtime 和测试命令。
- 新增 `docs/tutorials/step41_cli_client.md`，按固定 0-10 模板讲解 CLI 边界、接口、运行流程、测试设计和面试追问。
- 更新 `docs/process/task_plan.md` / `docs/process/findings.md` / `docs/process/progress.md` 记录 Step 41 过程。

最终验证：

- `cmake --build build -j2`：通过。
- `docker compose -f docker/docker-compose.yml up -d --wait`：通过，MySQL / Redis healthy。
- `ctest --test-dir build --output-on-failure`：通过，338/338 tests passed。
- `git diff --check`：通过。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" docs/tutorials/step41_cli_client.md README.md`：无输出。
- `rg -n "^## " docs/tutorials/step41_cli_client.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。
- `.gitkeep` 和旧 `server/net`、`server/protocol`、SQLite、`InMemoryStorage`、`step15_sqlite` 路径扫描无输出。
- `timeout 2s ./build/server/liteim_server || test $? -eq 124`：通过，server 监听 `0.0.0.0:9000` 后收到 SIGTERM 并通过 signalfd 退出。

收尾注意：

- Step 41 提交需要继续排除进入本 Step 前已有的用户侧注释改动：`include/liteim/service/GroupService.hpp`、`src/service/GroupService.cpp`、`src/storage/GroupDao.cpp`。

收尾完成：

- 提交完成：`feat(client): add command line im client`。

## 2026-05-16 Step 39 HistoryService

本次进入 `Step 39: implement HistoryService recent history pagination`。

开始状态：

- 当前新路线中 Step 38 `GroupService` 已完成。
- `session-catchup.py` 提示的未同步内容来自纯概念问答，不对应当前 Step 39 代码改动。
- 工作区进入 Step 39 前已有用户侧未提交注释改动：`include/liteim/service/GroupService.hpp`、`src/service/GroupService.cpp`、`src/storage/GroupDao.cpp`。本 Step 不覆盖这些改动，提交时保持和 Step 39 行为改动分离。
- Step 39 采用现有 TLV 协议，不新增协议枚举：请求读取 `ConversationType`、`ConversationId`、可选 `MessageId` 作为 `before_message_id`、可选 `Limit`；响应继续返回重复消息字段。

概念计划：

- `HistoryService` 只负责鉴权、解析请求、调用 `IStorage::getHistory()` 和编码响应。
- 历史查询仍由 `MessageDao` / `MySqlStorage` 走 `(conversation_type, conversation_id, message_id)` 索引；service 不拼 SQL。
- 默认返回最近 20 条，超过 50 条截断，`limit=0` 拒绝。
- 群聊历史必须验证当前用户是群成员；私聊历史用当前私聊 conversation id 规则验证当前用户是否参与该会话。

TDD RED：

- 新增 `tests/service/history_service_test.cpp`，覆盖 header 自包含、未登录拒绝、私聊默认 limit、limit 最大 50 截断、`before_message_id` 游标、`limit=0` 拒绝、私聊非参与者拒绝、群聊非成员拒绝、群成员查询成功、router 注册发回 `HistoryResponse`。
- 更新 `tests/CMakeLists.txt` 注册新测试文件。
- 运行 `cmake --build build`，预期失败于 `fatal error: liteim/service/HistoryService.hpp: No such file or directory`。

代码完成：

- 新增 `include/liteim/service/HistoryService.hpp` 和 `src/service/HistoryService.cpp`。
- `HistoryService` 注册 `HistoryRequest` 到 `MessageRouter` 的 business thread dispatch。
- 请求解析 `ConversationType` / `ConversationId` / 可选 `MessageId` 游标 / 可选 `Limit`，默认 limit 为 20，超过 50 截断，`limit=0` 拒绝。
- 私聊历史按当前私聊 conversation id 规则验证当前用户是否参与；群聊历史通过 `findGroupById()` 和 `getGroupMembers()` 验证成员身份。
- 响应使用重复 TLV 字段返回 `MessageId`、`ConversationType`、`ConversationId`、`SenderId`、`ReceiverId`、`MessageText`、`TimestampMs`。
- 更新 `src/service/CMakeLists.txt` 和 `server/main.cpp`，把 `HistoryService` 编进 `liteim_service` 并在运行时注册。

TDD GREEN：

- `cmake --build build`：通过。
- `ctest --test-dir build -R HistoryService --output-on-failure`：通过，9/9 tests passed。

文档完成：

- 更新 `README.md`：当前 runtime 切到 Step 39，记录 `HistoryService` 和 HistoryService 验证命令。
- 新增 `docs/tutorials/step39_history_service.md`，按固定 0-10 模板讲解历史分页、请求字段、权限校验、测试设计和面试追问。
- 更新 `docs/process/task_plan.md` / `docs/process/findings.md` / `docs/process/progress.md` 记录 Step 39 边界、设计发现和 RED/GREEN 过程。

阶段验证结果：

- `cmake --build build`：通过。
- `ctest --test-dir build -R HistoryService --output-on-failure`：通过，9/9 tests passed。
- `docker compose -f docker/docker-compose.yml up -d --wait`：通过，MySQL / Redis 均 healthy。
- `ctest --test-dir build --output-on-failure`：通过，321/321 tests passed。
- `git diff --check`：通过。
- `.gitkeep` 检查：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 残留。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" docs/tutorials/step39_history_service.md README.md`：无输出。
- `rg -n "^## " docs/tutorials/step39_history_service.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。
- `timeout 2s ./build/server/liteim_server || test $? -eq 124`：通过，server 监听 `0.0.0.0:9000` 后收到 SIGTERM 并通过 signalfd 退出。

收尾注意：

- Step 39 提交应排除进入本 Step 前已有的用户侧注释改动：`include/liteim/service/GroupService.hpp`、`src/service/GroupService.cpp`、`src/storage/GroupDao.cpp`。

收尾完成：

- 提交完成：`feat(chat): add recent history pagination`。

## 2026-05-16 Step 43 Benchmark Tool

本次进入 `Step 43：实现自研压测工具`。

开始状态：

- Step 42 已提交：`64ec246 test(e2e): add python end to end tests`。
- 工作区仍保留 进入该任务前已有的用户侧改动：`include/liteim/service/GroupService.hpp`、`src/service/GroupService.cpp`、`src/storage/GroupDao.cpp`。本 Step 不应把这些改动混入提交。
- 用户的 MySQL / Redis 在 Docker 环境运行；Step 43 的真实运行验证继续使用 `docker compose -f docker/docker-compose.yml up -d --wait`。

概念计划：

- 新增 `bench/` 目录和 `liteim_bench` 可执行程序。
- 把参数解析、延迟分位数、报告格式和资源采样拆成可单测 helper。
- `liteim_bench` 启动多个长连接，注册/登录唯一 bench 用户，并向一个接收用户发送普通私聊消息。
- 输出 JSON 或 Markdown 报告，包含 connection success、QPS、平均延迟、p50/p95/p99、错误数、内存和 CPU 使用。

TDD RED：

- 新增 `tests/bench/benchmark_test.cpp`，覆盖参数解析、私聊压测最低连接数、p50/p95/p99 统计、JSON 报告字段和固定大小 payload。
- 更新根 `CMakeLists.txt` 与 `tests/CMakeLists.txt`，注册 `bench` 子目录和 `liteim_bench_core`。
- `cmake --build build --target liteim_tests -j2` 按预期失败：`add_subdirectory given source "bench" which is not an existing directory`。

代码完成：

- 新增 `bench/CMakeLists.txt`，生成 `liteim_bench_core` 静态库和 `liteim_bench` 可执行程序。
- 新增 `bench/Benchmark.hpp` / `bench/Benchmark.cpp`，实现参数解析、payload 生成、nearest-rank 分位数统计、JSON/Markdown 报告、进程 RSS/CPU 采样和真实私聊压测 runner。
- 新增 `bench/liteim_bench.cpp`，作为命令行入口：解析参数、运行 benchmark、输出报告。
- 压测 runner 使用 1 个接收者连接和 `connections - 1` 个发送者连接；所有用户注册唯一账号并登录，发送者走普通 `PrivateMessageRequest`，接收者后台读取 push 防止接收端输出缓冲干扰测试。

TDD GREEN：

- `cmake --build build --target liteim_tests -j2`：通过。
- `ctest --test-dir build -R Benchmark --output-on-failure`：通过，5/5 tests passed。

阶段验证：

- `cmake --build build --target liteim_bench -j2`：通过。
- `./build/bench/liteim_bench --help`：通过，输出可用参数。
- `docker compose -f docker/docker-compose.yml up -d --wait`：通过，MySQL / Redis healthy。
- 手动启动 `./build/server/liteim_server` 后运行小规模 smoke：`./build/bench/liteim_bench --host 127.0.0.1 --port 9000 --connections 4 --message-size 64 --interval-ms 20 --duration-sec 1 --format json`：通过，`connection_success=4/4`、`request_success=114`、`error_count=0`、`p99_us=9403`。

最终验证：

- `cmake --build build -j2`：通过。
- `ctest --test-dir build --output-on-failure`：通过，349/349 tests passed。
- `git diff --check`：通过。
- `.gitkeep` 检查：无输出。
- 旧路线路径检查：无 `server/net`、`server/protocol`、`SQLite`、`InMemory`、`step15_sqlite` 残留。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" docs/tutorials/step43_benchmark_tool.md README.md`：无输出。
- `rg -n "^## " docs/tutorials/step43_benchmark_tool.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。
- `timeout 2s ./build/server/liteim_server || test $? -eq 124`：通过，server 监听 `0.0.0.0:9000` 后收到 SIGTERM 并通过 signalfd 退出。

收尾注意：

- Step 43 提交需要继续排除进入本 Step 前已有的用户侧改动：`include/liteim/service/GroupService.hpp`、`src/service/GroupService.cpp`、`src/storage/GroupDao.cpp`。

收尾完成：

- 提交完成：`feat(bench): add liteim benchmark tool`。

## 2026-05-20 Post-Step53 Review Hardening

- 采纳并修复外部审阅中成立的问题：`README.md` / `docs/process/task_plan.md` / `/home/yolo/jianli/PROJECT_MEMORY.md` 补齐 Qt `chat/` 目录说明。
- 修复离线消息拉取的一致性顺序：先标记 MySQL offline delivered，再 best-effort 清 Redis unread；新增测试覆盖操作顺序和 mark 失败不清 unread。
- 修复 Qt `AuthController` 发送前失败时的 pending 泄漏；新增 Qt Step47 测试覆盖超长字段构包失败后 `pendingCount()==0`。
- 补齐 `Channel::kReadEvent` 的 `EPOLLRDHUP` 订阅，并用 `ChannelTest` 验证 read interest mask。
- 为 `Session` 增加 peer IP，`TcpServer` 从 `Acceptor` 的 `sockaddr_in` 传入真实 IPv4，`AuthService` 登录限流优先按真实 peer IP 计数。
- 收紧 `OfflineMessageDao::markOfflineDelivered()`：每个 message id 必须更新 1 行，否则事务回滚并返回 `NotFound`。
- 给 `server/main.cpp` 的 `server.start()` / `loop.loop()` 外层增加异常保护，启动或事件循环异常时统一 stop server、业务线程池、Redis/MySQL pool 和 signal watcher。
- 当前验证已通过：`cmake --build build --target liteim_tests -j2`、`ctest --test-dir build -R "ChannelTest|AuthService|OfflineMessageService|MessageDao" --output-on-failure`、`cmake --build build --target liteim_server -j2`、`cmake --build build-qt --target liteim_qt_client_tests -j2`、`ctest --test-dir build-qt -R "LiteIMQtClient.Step47" --output-on-failure`、新增 `TcpServerTest.AcceptedLoopbackSessionStoresPeerIp` 和 `AuthServiceFixture.LoginFailureUsesSessionPeerIpWhenAvailable` targeted test、`ctest --test-dir build -R "LiteIME2E.test_backpressure" --output-on-failure`、顺序重跑 `ctest --test-dir build --output-on-failure` 384/384、重建 Qt build 的默认测试后 `ctest --test-dir build-qt --output-on-failure` 391/391、`git diff --check`、关键 Qt 目录和 peer-IP 过期措辞扫描。

## 2026-05-20 Step 55 Private Delivery ACK

本次进入 `Step 55：私聊 delivery ACK`。

开始状态：

- Step 53 离线 ACK 和 Step 54 `client_msg_id` 幂等已经提交。
- 当前 Step 只补私聊接收方 delivery ACK，不做 read receipt、群聊全员 ACK 或跨节点确认。
- Step55 复用 Step53 的 `message_deliveries` 表，不新增 schema migration。

TDD RED：

- 更新 `tests/protocol/message_type_test.cpp`，要求 `DeliveryAckRequest` / `DeliveryAckResponse` 有可读名和请求/响应分类。
- 更新 `tests/service/chat_service_test.cpp`，新增 `handleDeliveryAck` 签名检查、receiver ACK 成功、非 receiver 拒绝、重复 ACK 幂等测试。
- 更新 `tests/storage/mysql_storage_test.cpp`，新增 MySQL delivery 状态写入和非 receiver 拒绝集成测试。
- 更新 CLI 和 Python E2E 测试，覆盖 `delivery-ack <message_id>` 构包和 Bob 收到 `PrivateMessagePush` 后 ACK。
- `cmake --build build --target liteim_tests -j2` 按预期失败于缺少 `DeliveryAckRequest` / `DeliveryAckResponse`、`ChatService::handleDeliveryAck()` 和 `IStorage::ackPrivateMessageDelivery()`。

代码完成：

- 协议层新增 `DeliveryAckRequest = 506` 和 `DeliveryAckResponse = 507`，并补齐 `toString()`、request/response/push 分类。
- `IStorage` / `MySqlStorage` 新增 `ackPrivateMessageDelivery(user_id, message_id, message)`；实现中查消息、校验私聊 receiver、upsert `message_deliveries.status = delivered`。
- `ChatService` 注册 `DeliveryAckRequest` 到 business dispatch，并在 `handleDeliveryAck()` 中从 session 取当前用户、解析 `MessageId`、调用 storage、返回 `DeliveryAckResponse` 和 `DeliveryStatus=2`。
- `client_cli` 新增 `delivery-ack <message_id>` 命令。
- `tests/mocks/MockStorage.hpp` 和所有 `IStorage` fake 同步新接口。

验证：

- `cmake --build build --target liteim_tests liteim_server -j2`：通过。
- `ctest --test-dir build -R "MessageType|ClientCliCommandTest|ChatService|MySqlStorageIntegrationTest|LiteIME2E.test_private_chat" --output-on-failure`：通过，35/35 tests passed。
- `ctest --test-dir build --output-on-failure`：通过，395/395 tests passed。
- `python3 -m py_compile tests/e2e/liteim_e2e.py tests/e2e/test_private_chat.py`：通过。
- `ctest --test-dir build -R "LiteIME2E.test_private_chat" --output-on-failure`：通过，1/1 tests passed。
- `git diff --check`：通过。
- `rg -n "提交信息|commit message|## 11|Current Status|当前状态" README.md docs/tutorials/step55_private_delivery_ack.md docs/tutorials/step03_protocol_types.md docs/tutorials/step41_cli_client.md`：无输出。
- `rg -n "^## " docs/tutorials/step55_private_delivery_ack.md`：标题顺序为 0-10，最后一节是 `面试常见追问`。

## 2026-05-20 Step 56 Bounded ThreadPool And Limiter Verification

本次进入 `Step 56：ThreadPool 队列上限和限流验证`。

开始状态：

- Step55 已提交为 `300441a feat(chat): add private delivery ack`。
- post-review hardening 已把真实 peer IP 接入 `AuthService`，本 Step 只补真实 Redis integration 验证，不重写限流逻辑。
- 当前 Step 不做复杂熔断、令牌桶、全局 QPS 限流或网络 output high-water mark 改动。

TDD RED：

- `tests/concurrency/thread_pool_test.cpp` 新增满队列拒绝和 0 上限无界兼容测试。
- `tests/service/message_router_test.cpp` 新增业务队列满时 Router 返回 `ErrorResponse` 的测试。
- `tests/base/config_test.cpp` 新增 `server.business_queue_capacity` 默认值、覆盖和 0 值拒绝测试。
- `tests/service/auth_service_test.cpp` 新增真实 Redis + peer IP 登录失败窗口隔离测试。
- `cmake --build build --target liteim_tests -j2` 按预期失败于缺少 `ThreadPool(worker_count, limit)`、`ResourceExhausted` 和 `Config::business_queue_capacity`。

代码完成：

- `ErrorCode` 新增 `ResourceExhausted`。
- `ThreadPool` 新增 `max_pending_tasks_`、二参构造和 `maxPendingTaskCount()`；pending 队列达到上限时 `submit()` 返回 `ResourceExhausted`。
- `Config` 新增 `business_queue_capacity{1024}`，配置 key 为 `server.business_queue_capacity`，服务端配置拒绝 0。
- `server/main.cpp` 使用 `ThreadPool(config.business_threads, config.business_queue_capacity)`。
- `MessageRouter` 复用已有 submit 失败路径，把 `ResourceExhausted` 编码成 `ErrorResponse`。

验证：

- `cmake --build build --target liteim_tests liteim_server -j2`：通过。
- `ctest --test-dir build -R "ErrorCodeTest|ThreadPool|MessageRouter|ConfigTest|AuthService" --output-on-failure`：通过，51/51 tests passed。
- `ctest --test-dir build --output-on-failure`：通过，400/400 tests passed。
