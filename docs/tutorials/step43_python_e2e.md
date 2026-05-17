# Step 43：Python 端到端测试

## 0. 本 Step 结论

- 目标：Step 43 用 Python 黑盒 E2E 测试验证 `liteim_server` 的真实 TCP/TLV 聊天闭环。
- 前置依赖：依赖 Step 42 CLI 已确认的协议流程，以及 Step 34-41 服务端业务 handler。
- 主要交付：新增 `tests/e2e/` Python 测试、最小 TLV codec/client、server 启停 helper 和 CTest wiring。
- 依赖边界：MySQL / Redis 使用 Docker Compose 默认环境，Python 测试不直接访问数据库或 Redis。
- 协议边界：不改 C++ MessageType、TlvType、Packet 格式、schema、Redis key 或服务端 handler。

## 1. 为什么需要这个 Step

Step 42 的 CLI 已经可以手工调试协议，但手工命令不能稳定覆盖回归风险。Step 43 增加 Python E2E，是为了从“真实客户端视角”验证服务端：

- 能从 TCP 连接读入 TLV 请求。
- 能把请求分发到业务线程池。
- 能经过 MySQL / Redis 完成真实业务操作。
- 能把 response / push 按同一个协议发回客户端。
- 能覆盖多个客户端之间的在线、离线、历史、心跳和慢客户端场景。

这些测试不替代 C++ 单元测试。它们补的是“模块都单独正确，但串起来可能坏”的黑盒链路。

## 2. 本 Step 边界

### 本 Step 做

- 新增 Python 最小 Packet/TLV 编解码。
- 新增 Python 阻塞 TCP client，支持请求、响应匹配和 push 缓冲。
- 新增 server 启停 helper，CTest 通过 `LITEIM_SERVER_BIN` 指向构建出的 `liteim_server`。
- 新增 auth、private chat、group chat、offline、heartbeat、backpressure 六组 E2E 测试。
- CTest 注册 `LiteIME2E.*` 测试，并用资源锁串行运行，避免默认端口 `9000` 冲突。

### 本 Step 不做

- 不实现 Python BotClient 或 PersonaAgent。
- 不引入 pytest、requests、asyncio 或第三方 Python 依赖。
- 不直接访问 MySQL / Redis 做断言。
- 不修改 C++ 服务端 public API、协议字段或数据库 schema。
- 不解决动态端口、多 server 并行 E2E、测试数据清理和压测统计；这些留给后续测试/bench 细化。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `tests/e2e/liteim_e2e.py` | 新增 | Python Packet/TLV codec、TCP client、server helper 和测试基类 |
| `tests/e2e/test_auth.py` | 新增 | 覆盖注册、登录、好友列表、错误密码和登录限流 |
| `tests/e2e/test_private_chat.py` | 新增 | 覆盖两个客户端私聊、在线 push 和私聊历史 |
| `tests/e2e/test_group_chat.py` | 新增 | 覆盖三用户群聊、在线群 push 和群历史 |
| `tests/e2e/test_offline.py` | 新增 | 覆盖离线用户重新登录后拉取离线消息 |
| `tests/e2e/test_heartbeat.py` | 新增 | 覆盖未登录和已登录心跳响应 |
| `tests/e2e/test_backpressure.py` | 新增 | 覆盖慢客户端接收端被输出高水位关闭 |
| `tests/CMakeLists.txt` | 修改 | 注册 Python `unittest` 到 CTest |
| `README.md` / planning 文件 | 更新 | 记录 E2E 运行方式、边界和验证结果 |

## 4. 核心接口与契约

### `Packet`

Python helper 里的 `Packet` 只表达测试需要的协议视图：

```python
@dataclass
class Packet:
    msg_type: MessageType
    seq_id: int
    fields: Dict[int, List[bytes]]
```

契约：

- header 使用 C++ 同款 20 字节格式。
- TLV body 解析为 `type -> repeated values`。
- `uint64()` / `string()` 用于读取单值字段。
- `message_records()` 把重复消息字段按下标重新组装成消息列表。

### `LiteIMClient`

```python
class LiteIMClient:
    def request(self, msg_type, fields=(), expected=None) -> Packet
    def expect_push(self, msg_type, timeout=5.0) -> Packet
```

契约：

- 每个请求自动递增 `seq_id`。
- 收到相同 `seq_id` 的包视为 response。
- 收到其他包先缓存到 `pushes`，后续 `expect_push()` 再消费。
- 不理解业务线程池、MySQL、Redis 或 `Session` 内部实现，只按 TCP/TLV 客户端行为测试。

### `LiteIMServer`

```python
class LiteIMServer:
    def start(self) -> None
    def stop(self) -> None
```

契约：

- 默认启动 `LITEIM_SERVER_BIN` 指向的 `liteim_server`。
- 默认连接 `127.0.0.1:9000`。
- 如果服务端因 MySQL/Redis 不可用而启动失败，E2E 测试 skip，而不是把无关单元测试判失败。
- 也支持 `LITEIM_E2E_USE_EXISTING_SERVER=1` 连接已有 server。

## 5. 运行流程

### 1. CTest 启动 E2E

```text
ctest -R LiteIME2E
    -> Python unittest module
    -> E2ETestCase.setUpClass()
    -> 启动 build/server/liteim_server
    -> 等待 127.0.0.1:9000 可连接
    -> 测试用例创建多个 LiteIMClient
    -> 发送 TLV 请求并断言 response / push
    -> tearDownClass() 发送 SIGTERM 停止 server
```

### 2. 私聊 E2E 数据例子

真实测试会生成唯一用户名，例如：

```text
username = e2e_alice_1715840000000_12345_000001
receiver_id = 10023
conversation_id = (10022 << 32) | 10023
MessageText = "hello bob from python e2e"
```

流程：

```text
Alice Register/Login
Bob Register/Login
Alice AddFriend(Bob)
Alice PrivateMessageRequest(Bob)
    -> Alice receives PrivateMessageResponse
    -> Bob receives PrivateMessagePush
Alice HistoryRequest(private conversation_id)
    -> HistoryResponse includes saved message
```

### 3. 离线 E2E 数据例子

```text
Receiver 注册后断开
Sender 登录并发送 PrivateMessageRequest
Receiver 重新登录
Receiver OfflineMessagesRequest(limit=10)
```

断言点是 `OfflineMessagesResponse` 中能看到 sender 发出的 `MessageText`。测试不直接查 MySQL `offline_messages` 表，因为这是黑盒 E2E。

## 6. 关键实现点

### 1. Python codec 保持最小

Python 只实现 Step 43 需要的字段和类型，不复制整个 C++ 协议层。这样测试保持可读，也避免 Python helper 变成第二套业务实现。

### 2. 随机用户名避免 seed 密码问题

seed 里的 `alice` / `bob` 使用 dev hash，不一定能通过 `AuthService` 的 PBKDF2 校验。E2E 测试统一先注册唯一用户名，再登录真实密码，避免依赖 seed 密码。

### 3. 多模块串行启动 server

`liteim_server` 默认监听 `9000`。CTest 给 `LiteIME2E.*` 设置同一个 `RESOURCE_LOCK`，所以即使以后并行跑 CTest，这些 Python E2E 也不会同时抢同一个端口。

### 4. Backpressure 只验证黑盒结果

慢客户端测试不读取接收端 push，发送方连续发较大的私聊消息，让接收端输出缓冲持续积压。断言接收端最终被 peer close。这个测试不读取 `Session::pendingOutputBytes()`，因为那是 C++ 单元测试的职责。

## 7. 测试设计

| 风险 | 测试覆盖 |
| --- | --- |
| Python codec 和 C++ Packet/TLV 不兼容 | 所有 E2E 都通过真实 TCP 请求服务端 |
| AuthService 只在单元测试里通过 | 注册、登录、错误密码、登录限流 E2E |
| 在线私聊 push 丢失 | 两客户端私聊后接收端断言 `PrivateMessagePush` |
| 群聊成员投递异常 | 三客户端建群、加群、发群聊并断言两个成员收到 `GroupMessagePush` |
| 离线消息链路断裂 | 接收方离线后重新登录并主动 `OfflineMessagesRequest` |
| 心跳业务 handler 未接入 runtime | 未登录和已登录连接都发送 `HeartbeatRequest` |
| 慢客户端保护只在单元测试存在 | Python 慢接收端通过真实 server 被关闭 |

## 8. 验证命令

```bash
cmake --build build --target liteim_tests -j2
docker compose -f docker/docker-compose.yml up -d --wait
ctest --test-dir build -R LiteIME2E --output-on-failure
ctest --test-dir build --output-on-failure
git diff --check
```

## 9. 面试表达

> Step 43 补了 Python 黑盒 E2E。Python 只实现最小 TLV codec 和阻塞 TCP client，通过 CTest 启动当前构建出的 `liteim_server`，用真实协议覆盖注册登录、私聊、群聊、离线、历史、心跳、错误密码、登录限流和慢客户端回压。

展开说：

> C++ 单元测试验证模块内部语义，Python E2E 验证服务端 runtime 串联后的真实行为。测试不访问数据库和 Redis，而是像普通客户端一样发 Packet、按 seq_id 收 response、缓存 push。测试数据用动态注册用户，避免依赖 seed 密码 hash。因为 server 默认端口是 9000，CTest 给 E2E 加资源锁串行执行。

容易被追问：

> 为什么不用 pytest？第一版为了降低依赖，只用 Python 标准库 `unittest`。后续更复杂断言需要 fixture/marker 时，可以再引入 pytest。

## 10. 面试常见追问

### 为什么 E2E 不直接查 MySQL 表？

Step 43 是黑盒测试。它要验证客户端能否通过 TCP/TLV 协议拿到正确结果。MySQL 表结构、DAO 和事务细节已经由 C++ storage/service 测试覆盖；E2E 如果直接查表，会把黑盒测试变成半集成测试，也会增加耦合。

### 为什么每个测试模块都启动一次 server？

这样模块之间隔离更强，失败时更容易定位。当前 server 固定端口是 9000，所以 CTest 用 `RESOURCE_LOCK LiteIME2EServer` 串行运行这些模块，避免并行抢端口。

### 为什么不使用 seed 用户登录？

seed 数据里的密码字段是 dev hash，不一定匹配 `AuthService` 当前 PBKDF2 逻辑。E2E 需要验证真实注册和登录，所以测试里动态注册用户，并用同一个明文密码登录。

### Backpressure E2E 和 C++ backpressure 单元测试有什么区别？

C++ 单元测试能直接设置高水位、观察 session 表和 pending output；Python E2E 不能看内部状态，只能观察慢客户端最终被关闭。两者互补：单元测试定位机制，E2E 验证 runtime 行为。
