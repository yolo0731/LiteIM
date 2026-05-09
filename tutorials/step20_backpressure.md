# Step 20：完善慢客户端回压保护

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

## 1. 本 Step 修改文件

```text
include/liteim/base/Config.hpp
src/base/Config.cpp
include/liteim/net/Session.hpp
src/net/Session.cpp
include/liteim/net/TcpServer.hpp
src/net/TcpServer.cpp
server/main.cpp
tests/base/config_test.cpp
tests/net/session_header_test.cpp
tests/net/session_test.cpp
tests/net/tcp_server_header_test.cpp
tests/net/tcp_server_test.cpp
tutorials/step20_backpressure.md
```

同时同步：

```text
README.md
tutorials/step02_base.md
tutorials/step14_session.md
task_plan.md
findings.md
progress.md
PROJECT_MEMORY.md
```

## 2. Session 高水位语义

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

## 3. 为什么出站写不算活跃

Step 20 接着之前的 P0 语义修复：`last_active_time` 只表示客户端入站完整 Packet 活跃。

服务端给客户端写数据不能刷新活跃时间。否则 Bot 回复、系统通知、群聊 push 等出站流量会把沉默客户端误判成活跃连接，削弱 heartbeat timeout。

所以 Step 20 的两条语义是分开的：

- 入站完整 Packet：刷新 `last_active_time`。
- 出站 pending output：只参与高水位回压，不刷新 `last_active_time`。

## 4. TcpServer 如何传递配置

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

## 5. Config 配置键

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

## 6. 本 Step 不做什么

本 Step 不实现：

- 暂停读。
- 低水位恢复。
- 消息优先级丢弃。
- 群聊广播优化。
- 复杂 per-user / per-message backpressure 策略。
- `Session::input_buffer_` 简化。
- `Session` 状态机重构。

这些会改变更多连接状态和业务语义，应该后续单独做。

## 7. 测试设计

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
```

测试覆盖四层语义：

- `Config` 能解析高水位，并拒绝 0。
- `Session` 默认 4MB，且支持自定义阈值。
- 正常小包 echo 不触发高水位。
- 服务端 push 超过阈值时连接关闭，`TcpServer::sessions_` 会被 close callback 清理。

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

## 9. 面试时怎么讲

可以这样讲：

> LiteIM 的 `Session` 是单连接 owner，输出路径在 owner I/O loop 中执行。非阻塞写遇到慢客户端时，剩余数据会进入 `output_buffer_` 等下一次 `EPOLLOUT`。为了避免慢客户端无限占用内存，我给每个 `Session` 设置了输出高水位，默认 4MB，也可以通过 `Config` 和 `TcpServer` 配置。每次追加输出前先判断 pending bytes 加本次编码 Packet 是否超过阈值，超过就记录日志并关闭连接。这个策略不暂停读、不做低水位恢复，是第一版简单可靠的硬保护。

重点是：

- 回压检查必须在 owner loop 中读写 `output_buffer_`。
- 超限关闭后通过 `close_callback_` 清理 `TcpServer::sessions_`。
- 服务端出站写不刷新 `last_active_time`。
- 第一版只做高水位硬关闭，复杂恢复策略后续再做。
