# Step 20：完善慢客户端回压保护

## 0. 本 Step 结论

- 目标：如果某个客户端一直不读服务端发给它的数据。
- 前置依赖：依赖 Step 0-19 已建立的工程、协议或运行时基础。
- 主要交付：`完善慢客户端回压保护` 的文件变化、接口契约、运行流程、测试和面试表达。
- 线程/生命周期边界：沿用 LiteIM 当前 owner-loop、RAII、业务线程隔离和抽象依赖规则。
- 范围控制：不实现 暂停读。

## 1. 为什么需要这个 Step

一句话就是：
如果某个客户端一直不读服务端发给它的数据，
服务端的输出缓冲区不能无限变大；
超过上限就关闭这个 Session，保护服务器内存。

“慢客户端”指的是：

服务端一直发送数据
客户端读得很慢 / 卡住 / 不读
网络发送缓冲区写不进去

本 Step 的目标是把已有的 `Session` 输出缓冲区 4MB 保护，整理成一个明确、可配置、可测试的慢客户端回压策略。

连接发送路径是：

```text
business / TcpServer
  -> Session::sendPacket()
  -> encode Packet
  -> owner EventLoop
  -> Session::sendEncodedInLoop()
  -> output_buffer_
  -> handleWrite()
```

如果客户端长期不读数据，非阻塞 `write()` 迟早会返回 `EAGAIN`，服务端剩余数据只能留在 `output_buffer_`。没有上限时，一个慢客户端就可能持续占用服务端内存。

Step 20 只做第一版硬保护：

```text
pending output + incoming encoded packet > high water mark
  -> log warning
  -> close Session
  -> TcpServer close callback removes sessions_ entry
```

它不做复杂限流，也不暂停读。

### 为什么出站写不算活跃

Step 20 接着之前的 P0 语义修复：`last_active_time` 只表示客户端入站完整 Packet 活跃。

服务端给客户端写数据不能刷新活跃时间。否则 Bot 回复、系统通知、群聊 push 等出站流量会把沉默客户端误判成活跃连接，削弱 heartbeat timeout。

所以 Step 20 的两条语义是分开的：

- 入站完整 Packet：刷新 `last_active_time`。
- 出站 pending output：只参与高水位回压，不刷新 `last_active_time`。

## 2. 本 Step 边界

### 本 Step 做

- 聚焦 `完善慢客户端回压保护` 这一层的当前交付，把前置能力接成可编译、可测试的模块。
- 明确新增/修改文件、核心接口、运行流程、边界条件和验证方式。
- 保持当前 Step 的实现范围，不把后续路线混入本 Step。

### 本 Step 不做

- 不实现 暂停读。
- 不实现 低水位恢复。
- 不实现 消息优先级丢弃。
- 不实现 群聊广播优化。
- 不实现 复杂 per-user / per-message backpressure 策略。
- 不实现 `Session::input_buffer_` 简化。
- 不实现 `Session` 状态机重构。

## 3. 文件变化

| 文件 | 变化 | 作用 |
| --- | --- | --- |
| `include/liteim/base/Config.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/base/Config.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `include/liteim/net/Session.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/Session.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `include/liteim/net/TcpServer.hpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `src/net/TcpServer.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `server/main.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/base/config_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/session_header_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/session_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/tcp_server_header_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tests/net/tcp_server_test.cpp` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tutorials/step20_backpressure.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `README.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tutorials/step02_base.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `tutorials/step14_session.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `task_plan.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `findings.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `progress.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |
| `PROJECT_MEMORY.md` | 修改 | 承载本 Step 对应代码、测试或文档变化 |

## 4. 核心接口与契约

`Config.hpp` 的回压字段：

- `Config::session_output_high_water_mark` 是 server 级默认输出高水位，默认 `4 * 1024 * 1024`。
- `loadFromFile()` 支持 `server.output_high_water_mark_bytes`。
- key 值必须是正整数，0 返回 `InvalidArgument`。
- 该配置是启动期值，当前不做运行时动态调整。

`Session.hpp` 的回压接口：

- `kSessionDefaultOutputHighWaterMark` 是单连接默认阈值。
- `outputHighWaterMark()` 查询当前阈值。
- `setOutputHighWaterMark(high_water_mark)` 在 owner loop 线程设置阈值，拒绝 0。
- `pendingOutputBytes()` 查询当前待写字节，只能 owner-loop 调用。
- `sendPacket()` 编码成功后，最终在 `sendEncodedInLoop()` 中检查 `pending + incoming` 是否超过阈值。
- 关键成员 `output_buffer_` 保存待写字节，`output_high_water_mark_` 决定何时关闭慢客户端。

`TcpServer.hpp` 的回压接口：

- `setSessionOutputHighWaterMark(high_water_mark)` 设置新连接默认阈值。
- 该函数必须在 base loop 线程、`start()` 前调用，并拒绝 0。
- `createSessionInLoop()` 创建每个 Session 后，在目标 I/O loop 中调用 `session->setOutputHighWaterMark()`。
- `session_output_high_water_mark_` 是 TcpServer 保存的配置快照。

失败语义：

- 配置文件 0 值：`Config::loadFromFile()` 返回错误。
- Session 0 值：抛 `std::invalid_argument`。
- TcpServer 启动后再设置：抛 `std::logic_error`。
- 跨线程直接设置：`assertInLoopThread()` 抛异常。

线程和所有权边界：

- 高水位检查发生在 Session owner loop 中。
- TcpServer 只保存阈值并在创建连接时传递，不跨线程直接改已有 Session 内部 Buffer。
- 慢客户端关闭走 `Session::closeInLoop()`，再由 close callback 清理 `TcpServer::sessions_`。

## 5. 运行流程

### 1. 在 LiteIM 里的具体使用场景

慢客户端回压保护的是输出方向：客户端或网络异常导致长时间不读数据，而服务端仍持续 echo、push、Bot 回复或系统通知。非阻塞 `write()` 写不完时，剩余字节进入 `Session::output_buffer_`；超过高水位就记录 warning 并关闭连接，防止单个慢连接吃掉内存。

### 2. 上下层调用连接

```text
Config
    -> TcpServer::setSessionOutputHighWaterMark()
    -> TcpServer::createSessionInLoop()
    -> Session::setOutputHighWaterMark()
    -> Session::sendEncodedInLoop()
    -> output_buffer_ high-water check
    -> closeInLoop() / close callback / TcpServer::removeSession()
```

上游是启动配置和服务端发送行为，下游是 `Session` 输出 Buffer、Channel 写事件和关闭清理。

### 3. 整体运行链路

1. server 启动时读取 `Config::session_output_high_water_mark`。
2. `server/main.cpp` 调用 [TcpServer::setSessionOutputHighWaterMark()](../src/net/TcpServer.cpp)。
3. 新连接进入 `TcpServer::createSessionInLoop()` 后，server 在 I/O loop 中调用 [Session::setOutputHighWaterMark()](../src/net/Session.cpp)。
4. 业务或 echo 调用 `Session::sendPacket()`。
5. [sendPacket()](../src/net/Session.cpp) 先编码 Packet，再投递到 owner loop。
6. [sendEncodedInLoop()](../src/net/Session.cpp) 在 append 前读取 `output_buffer_.readableBytes()`。
7. 如果 `encoded.size() > high_water`，或 `pending > high_water - encoded.size()`，记录 warning 并 `closeInLoop()`。
8. 如果未超限，append 到 output buffer 并开启写事件。
9. [handleWrite()](../src/net/Session.cpp) 正常写出后 retrieve 字节，pending 降低。
10. `closeInLoop()` 触发 close callback，`TcpServer::removeSession()` 删除表项。

### 4. 自身内部运行流程

整体可以看成 4 步：配置传递、发送前检查、正常写出、超限关闭。

核心成员职责：

- `Config::session_output_high_water_mark` 保存启动配置。
- `TcpServer::session_output_high_water_mark_` 保存 server 级快照，要求 start 前设置。
- `Session::output_high_water_mark_` 是单连接阈值。
- `Session::output_buffer_` 保存真实 pending bytes。
- `Logger` 记录超限时的 session id、pending、incoming 和 limit。

核心函数流程：

- [Config::loadFromFile()](../src/base/Config.cpp)：解析 `server.output_high_water_mark_bytes`，拒绝 0。
- `TcpServer::setSessionOutputHighWaterMark()`：要求 base loop 线程、start 前调用、阈值大于 0。
- `TcpServer::createSessionInLoop()`：创建 Session 后立即把阈值设置进去。
- `Session::setOutputHighWaterMark()`：要求 owner loop 线程，拒绝 0。
- `Session::sendEncodedInLoop()`：append 前检查高水位，超限先关闭，不追加本次 encoded bytes。
- `Session::handleWrite()`：正常客户端持续读时，写出后 retrieve，降低 pending。

`sendEncodedInLoop()` 的高水位处理可以理解成：

```text
准备发送 encoded bytes
    ↓
读取 pending：output_buffer_ 已积压字节
    ↓
读取 incoming：本次要追加的字节
    ↓
读取 limit：output_high_water_mark_
    ↓
pending + incoming 超过 limit 时记录 warning 并关闭连接
    ↓
未超限时 append 到 output_buffer_
    ↓
开启 Channel 写事件，等待 handleWrite() 慢慢写出
```

这一步必须发生在 append 前，因为一旦先追加再判断，慢客户端已经占用了更多内存。日志里的 session id、pending、incoming 和 limit 用来定位具体是哪条连接触发了保护。

### 5. 该项目代码在实际应用中的具体数据例子

假设 Bob 的 `session_id=43` 一直不读 socket，服务端连续给他推送离线消息。当前 pending output 已有 `4194200` 字节，再追加一条 `PRIVATE_MESSAGE_PUSH message_id=5001` 的 encoded Packet 需要 200 字节，就会超过默认 `4194304` 字节高水位。`Session` 记录 pending/incoming/limit 后关闭慢连接；这个出站压力不会刷新 `last_active_time`。

## 6. 关键实现点

### Session 高水位语义

`Session` 保留默认 4MB：

```cpp
inline constexpr std::size_t kSessionDefaultOutputHighWaterMark = 4 * 1024 * 1024;
```

每个 `Session` 持有自己的阈值：

```cpp
std::size_t output_high_water_mark_{kSessionDefaultOutputHighWaterMark};
```

公开两个小接口：

```cpp
void setOutputHighWaterMark(std::size_t high_water_mark);
std::size_t outputHighWaterMark() const noexcept;
```

`setOutputHighWaterMark()` 必须在 `Session` owner loop 线程调用，并且拒绝 0。当前 `TcpServer::createSessionInLoop()` 在 I/O loop 中创建 `Session`，所以它可以在同一线程设置阈值。

真正的保护点在 `sendEncodedInLoop()`：

```text
encoded.size() > output_high_water_mark_
或者
pending_bytes > output_high_water_mark_ - encoded.size()
  -> closeInLoop()
```

这里故意在 append 前检查，避免先把超限数据放进 buffer 再清理。

### TcpServer 如何传递配置

`TcpServer` 新增启动前配置接口：

```cpp
void setSessionOutputHighWaterMark(std::size_t high_water_mark);
```

约束：

- 必须在 base loop 线程调用。
- 必须在 `start()` 前调用。
- `high_water_mark` 必须大于 0。

新连接进来后：

```text
base loop accept
  -> choose I/O loop
  -> createSessionInLoop()
  -> Session(...)
  -> session->setOutputHighWaterMark(session_output_high_water_mark_)
  -> session->start()
```

这样每条连接都继承同一个 server 级默认阈值。后续如果需要按用户、业务类型或连接类型设置不同阈值，可以在这个边界继续扩展。

### Config 配置键

`Config` 新增字段：

```cpp
std::size_t session_output_high_water_mark{4 * 1024 * 1024};
```

配置文件 key：

```text
server.output_high_water_mark_bytes = 4194304
```

`loadFromFile()` 会解析这个 key，0 会返回 `ErrorCode::InvalidArgument`。

`server/main.cpp` 用默认配置创建 server 后，把该字段传给 `TcpServer`：

```cpp
server.setSessionOutputHighWaterMark(config.session_output_high_water_mark);
```

## 7. 测试设计

| 风险 | 测试如何覆盖 |
| --- | --- |
| `完善慢客户端回压保护` 的核心契约只停留在接口说明里 | 用单元测试或集成测试固定 public API、正常路径和错误路径 |
| 边界条件回归后影响后续 Step | 用异常输入、重复调用、关闭/超时/缺失依赖等用例覆盖边界 |
| 上下游调用关系被后续重构改坏 | 保留跨模块测试、smoke 验证或协议字段测试 |

新增或更新的重点测试：

```cpp
TEST(ConfigTest, ZeroHighWaterMarkFails)
TEST(SessionTest, DefaultOutputHighWaterMarkIsFourMegabytes)
TEST(SessionTest, RejectsZeroOutputHighWaterMark)
TEST(SessionTest, CloseWhenPendingOutputExceedsConfiguredHighWaterMark)
TEST(TcpServerTest, RejectsZeroSessionOutputHighWaterMark)
TEST(TcpServerTest, SessionOutputHighWaterMarkMustBeSetBeforeStart)
TEST(TcpServerTest, NormalClientDoesNotTriggerConfiguredHighWaterMark)
TEST(TcpServerTest, SlowClientIsClosedWhenOutputExceedsHighWaterMark)
TEST(TcpServerTest, ClosedSlowClientIsRemovedFromSessionTable)
TEST(TcpServerTest, SlowClientAccumulatedSmallPacketsTriggerHighWaterMark)
```

测试覆盖四层语义：

- `Config` 能解析高水位，并拒绝 0。
- `Session` 默认 4MB，且支持自定义阈值。
- 正常小包 echo 不触发高水位。
- 服务端 push 超过阈值时连接关闭，`TcpServer::sessions_` 会被 close callback 清理。
- 多个单包都小于高水位的 server pushes，如果在 owner loop 写出前累计超过阈值，也会关闭连接。

已有测试继续保留：

```cpp
TEST(SessionTest, LargePacketLeavesPendingOutputWhenPeerDoesNotRead)
TEST(SessionTest, CloseWhenPendingOutputExceedsHighWaterMark)
TEST(SessionTest, PendingOutputBytesRequiresOwnerLoopThread)
TEST(TcpServerTest, ServerWritesDoNotRefreshHeartbeatActivity)
```

这些测试分别固定 pending output、默认 4MB 关闭、owner-loop 查询约束，以及“出站写不刷新 heartbeat 活跃时间”。

## 8. 验证命令

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build -R "(ConfigTest|SessionTest|TcpServerTest|ReactorInterfaceTest)" --output-on-failure
ctest --test-dir build --output-on-failure
timeout 1s ./build/server/liteim_server || test $? -eq 124
git diff --check
```

## 9. 面试表达

### 一句话

LiteIM 的 Session 是单连接 owner，输出路径在 owner I/O loop 中执行。

### 展开说

可以这样讲：

> LiteIM 的 `Session` 是单连接 owner，输出路径在 owner I/O loop 中执行。非阻塞写遇到慢客户端时，剩余数据会进入 `output_buffer_` 等下一次 `EPOLLOUT`。为了避免慢客户端无限占用内存，我给每个 `Session` 设置了输出高水位，默认 4MB，也可以通过 `Config` 和 `TcpServer` 配置。每次追加输出前先判断 pending bytes 加本次编码 Packet 是否超过阈值，超过就记录日志并关闭连接。这个策略不暂停读、不做低水位恢复，是第一版简单可靠的硬保护。

重点是：

- 回压检查必须在 owner loop 中读写 `output_buffer_`。
- 超限关闭后通过 `close_callback_` 清理 `TcpServer::sessions_`。
- 服务端出站写不刷新 `last_active_time`。
- 第一版只做高水位硬关闭，复杂恢复策略后续再做。

### 容易被追问

- 为什么超过高水位直接关闭连接？
- 为什么出站写不能刷新 heartbeat 活跃时间？

## 10. 面试常见追问

### Q1：为什么超过高水位直接关闭连接？

慢客户端如果长期不读，服务端继续堆 output buffer 会拖垮内存。第一版用关闭连接做明确保护，比复杂低水位恢复更容易验证。

### Q2：为什么出站写不能刷新 heartbeat 活跃时间？

活跃时间表示客户端最近一次完整合法入站 Packet。服务端持续推送消息并不能证明客户端还在读或还愿意响应心跳，所以不能用写出行为续期。
