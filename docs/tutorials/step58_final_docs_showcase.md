# Step 58：最终 README、架构图、Qt 截图、面试说明和压测报告

## 0. 本 Step 结论

Step58 是 LiteIM 第一阶段的最终展示材料收口：README、架构图、线程模型、协议/数据模型摘要、Qt 截图、压测报告、面试说明和已知限制统一反映 Step53-57 的可靠性、安全和权限 hardening 结果。

本 Step 不新增服务端业务行为。服务端功能已经在 Step53-57 完成：离线 ACK、`client_msg_id` 幂等、私聊 delivery ACK、业务线程池队列上限、真实 peer IP 登录限流验证、好友申请和私聊好友权限。

## 1. 为什么需要这个 Step

最终 README 不能只列功能，还要让面试官快速看懂项目边界：

- LiteIM 是 C++17 高性能后端实习项目，不是成熟生产级 IM 集群。
- 网络层体现 Reactor、`epoll`、`eventfd`、`timerfd`、`signalfd`、线程归属和生命周期控制。
- 业务层体现 MySQL/Redis 不进 I/O 线程、可靠投递 ACK、幂等、限流、回压和权限控制。
- 展示材料要明确 `server-stored`、`delivered`、`read` 的区别，避免把 TCP ACK 或服务端响应误说成用户已读。
- 压测数字要给环境、命令和风险边界，不能只报 QPS。

## 2. 本 Step 边界

本 Step 做：

- 刷新 `README.md` 当前能力说明。
- 补充 delivery semantics、known limitations 和 Future Work。
- 生成新的 2026-05-20 benchmark 报告。
- 刷新 Qt 客户端展示截图。
- 同步 process 文档，把 Step58 记为最终展示收口。

本 Step 不做：

- 不改 Packet/TLV 协议枚举。
- 不改 MySQL schema 或 Redis key。
- 不改服务端 ACK、好友、私聊、群聊、历史、心跳逻辑。
- 不实现 read receipt、多设备、群聊全员 ACK、TLS/token、跨节点路由或 PersonaAgent。

## 3. 文件变化

主要文件：

- `README.md`：最终项目介绍、架构图、线程模型图、协议/数据模型、benchmark、delivery semantics、known limitations 和 interview notes。
- `docs/reports/liteim_benchmark_report_2026-05-20.md`：当前 Step58 benchmark 报告。
- `docs/reports/qt_client_showcase.png`：Qt 三栏 IM 客户端展示截图。
- `docs/tutorials/step58_final_docs_showcase.md`：本教程。
- `docs/process/task_plan.md`、`docs/process/findings.md`、`docs/process/progress.md`：过程记忆同步。

## 4. 核心接口与契约

文档层最重要的契约是概念准确：

- `PrivateMessageResponse`：发送方请求处理成功，表示 server-stored。
- `ClientMessageId`：发送方幂等键，解决网络重试导致重复消息。
- `DeliveryAckRequest`：接收方收到在线私聊 push 后回 ACK，表示 delivered。
- `OfflineMessagesAckRequest`：客户端拉取离线消息后批量 ACK，ACK 后才 mark delivered。
- `message_deliveries.status = read`：当前只是预留状态，不代表已经实现 read receipt。

README 必须把这些语义写清楚，否则容易在面试里被追问“你说 ACK，到底确认的是哪一层”。

## 5. 运行流程

最终展示材料对应的运行链路如下：

```text
客户端登录
  -> 好友申请 / 接受
  -> 发送私聊 PrivateMessageRequest
  -> server 保存 messages
  -> 返回 PrivateMessageResponse(server-stored)
  -> 在线接收方收到 PrivateMessagePush
  -> 接收方回 DeliveryAckRequest(delivered)
```

离线链路如下：

```text
接收方离线
  -> sender 发送私聊
  -> server 保存 messages + offline_messages + pending delivery
  -> 接收方上线后 OfflineMessagesRequest 拉取 pending rows
  -> 客户端处理完成后 OfflineMessagesAckRequest
  -> server mark delivered
```

Qt 截图只展示客户端 UI 状态，不参与服务端可靠性判断。benchmark 报告只统计 sender request/response RTT，不统计接收方 push ACK 延迟。

## 6. 关键实现点

Step58 的关键不在代码复杂度，而在表达严谨：

- README 首屏先说明 LiteIM 的定位和外部 PersonaAgent 边界。
- 架构图把 I/O 线程、业务线程池、MySQL、Redis、回包投递方向画清楚。
- 协议摘要列出 ACK、幂等、好友权限相关消息和 TLV。
- `Delivery Semantics` 表把 server-stored、delivered、read 拆开。
- benchmark 报告写明 setup 阶段会建立 accepted friendship，计时阶段只测私聊 request/response。
- `Known Limitations And Future Work` 主动承认单机、无多设备、无 TLS/token、无完整群 ACK 和无 read receipt。

## 7. 测试设计

Step58 是文档/showcase Step，测试重点是防止材料和代码漂移：

- 构建服务端、测试二进制和 benchmark 工具，确认当前代码仍可编译。
- 跑默认 CTest，确认文档改动没有伴随错误的代码改动。
- 跑 Qt 构建和 Step46-52 Qt CTest，确认截图对应的 Qt 代码仍可构建。
- 用真实 benchmark 生成 2026-05-20 数字，而不是复用旧报告数字。
- 检查教程标题必须保持 0-10 模板，且最后一节是 `面试常见追问`。
- 扫描 README 和教程中的 commit-message/status 噪声。

## 8. 验证命令

推荐验证命令：

```bash
cmake --build build --target liteim_tests liteim_server liteim_bench -j2
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build --output-on-failure
cmake --build build-qt --target liteim_qt_client_tests liteim_qt_client -j2
ctest --test-dir build-qt -R "LiteIMQtClient.Step46|LiteIMQtClient.Step47|LiteIMQtClient.Step48|LiteIMQtClient.Step49|LiteIMQtClient.Step50|LiteIMQtClient.Step51|LiteIMQtClient.Step52|LiteIMCMake.QtClientFoundation" --output-on-failure
git diff --check
```

benchmark 复现命令示例：

```bash
./build/bench/liteim_bench --host 127.0.0.1 --port 19058 --connections 10 --message-size 128 --interval-ms 10 --duration-sec 10 --format json
```

## 9. 面试表达

可以这样介绍：

> LiteIM 是一个单机 C++17 IM 后端项目。我没有把它包装成生产级集群，而是重点展示从非阻塞网络、Reactor、跨线程唤醒、业务线程池隔离，到 MySQL/Redis 真实持久化和 IM 可靠性语义的完整链路。现在私聊支持好友权限、发送方幂等、离线 ACK、在线 delivery ACK 和线程池队列上限。README 里也明确区分 server-stored、delivered、read，read receipt 目前只是预留状态。

压测可以这样说：

> 当前本机 Docker MySQL/Redis 环境下，10 连接、128B、10 秒约 562 QPS，p99 约 11ms；30 连接压力样本约 713 QPS，p99 约 50ms，三组测试 0 错误。我把机器、命令、配置和限制都写在报告里，这些数字用于回归对照，不作为生产容量声明。

## 10. 面试常见追问

**问：你这个项目算生产级 IM 吗？**  
答：不算。它是比较完整的单机 C++ IM 后端实习项目，重点是网络、线程、存储、缓存和可靠性语义；生产级还需要多节点路由、多设备、TLS/token、完整权限/风控、可观测性和部署体系。

**问：为什么 `PrivateMessageResponse` 不算 delivered？**  
答：它只说明 sender 的请求已经被 server 处理并持久化。接收方是否收到 push 要看接收方的 `DeliveryAckRequest`。

**问：TCP 已经有 ACK，为什么还要应用层 ACK？**  
答：TCP ACK 只确认字节传输，不确认客户端业务层已经解析、保存或展示消息。IM 需要应用层 ACK 才能表达 delivery 状态。

**问：为什么 read receipt 没做？**  
答：read receipt 是更高一层的用户行为语义，需要客户端打开会话、窗口活跃、多设备同步等规则。当前 Step 只做到 delivered，并在表里预留 read 状态。

**问：benchmark 数字为什么不高？**  
答：当前 benchmark 是真实 MySQL/Redis 持久化链路，不是纯内存 echo，也不是多节点 fanout 压测。这个项目更强调真实链路、可复现和不夸大。

**问：为什么 AI Agent 不写进 C++ server？**  
答：LiteIM 只做协议和消息系统。PersonaAgent 作为未来 Python BotClient 普通账号接入，LangGraph/RAG/LLM/safety 留在独立服务里，这样服务端边界更清晰。

