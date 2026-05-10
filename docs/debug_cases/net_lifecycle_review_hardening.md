# 网络生命周期评审加固复盘

日期：2026-05-09

背景：Step 17 完成后，外部评审指出了网络层中的若干生命周期和慢客户端风险。本复盘记录哪些问题被接受、如何用回归测试复现，以及最终做了哪些修复。

2026-05-10 后续 cleanup 已把当时暂缓的 owner-loop 和 API 简化继续收口：`Acceptor::close()` 现在是 owner-loop-only，`EventLoop::isStopped()` 已删除，`Session` 使用 `SessionState` 收敛启动/关闭状态，public `Session::fd()`、`TcpServer::sendToUser()` 占位接口和 `kSessionOutputHighWaterMark` 兼容别名都已删除。

## 已接受的问题

### 1. EventLoopThread 自线程 stop 的生命周期问题

旧结构：

```text
loop 线程调用 EventLoopThread::stop()
  -> stop() 调用 loop_->quit()
  -> stop() detach 自己管理的 std::thread
  -> stop() 清空 loop_ / running_
  -> threadFunc() 稍后也会清空 loop_ / running_
```

`detach` 路径避免了 self-join，但削弱了对象所有权边界。owner 线程可能以为清理已经完成，而 worker 线程其实还在执行。如果 `EventLoopThread` 对象在 `threadFunc()` 结束前析构，worker 线程仍可能继续访问 `this`。

修复：

- `stop()` 只请求 `loop_->quit()`。
- 如果 `stop()` 是在被管理的 worker 线程内部调用，立即返回。
- owner 线程调用 `stop()` 时负责 join worker。
- `loop_`、`thread_id_` 和 `running_` 只在 worker 真正退出时由 `threadFunc()` 清理。
- `join_started_` 防止多个外部 `stop()` 并发竞争同一个 `std::thread::join()`。

回归测试：

```cpp
TEST(EventLoopThreadTest, OwnerStopWaitsAfterStopIsRequestedInsideLoop)
```

测试会向 loop 投递一个任务，让这个任务在 loop 内部调用 `thread.stop()`，然后 sleep 一段时间。owner 再调用 `stop()` 时，必须等待该任务结束并等待 worker 线程退出。

### 2. Session 输出缓冲区没有高水位限制

旧结构：

```text
Session::sendEncodedInLoop()
  -> output_buffer_.append(encoded)
  -> enableWriting()
```

如果客户端停止读取，服务端反复发送会让 `output_buffer_` 无上限增长。

修复：

- 新增默认输出高水位；后续统一为 `kSessionDefaultOutputHighWaterMark = 4 * 1024 * 1024`。
- append 前检查 `pending bytes + encoded bytes`。
- 如果下一次 append 会超过 4MB，就在 Session 所属 I/O loop 内关闭连接。

回归测试：

```cpp
TEST(SessionTest, CloseWhenPendingOutputExceedsHighWaterMark)
```

测试让对端不读取，然后连续投递多个最大尺寸 Packet。Session 必须在超过高水位时关闭，并清空待发送输出。

### 3. TcpServer 使用 fd 作为 session 身份

旧结构：

```text
session_id = accepted_fd
sessions_[session_id] = session
close callback -> removeSession(session_id)
```

fd 是内核资源编号，不是稳定的连接身份。连接关闭后，Linux 可能很快复用同一个 fd。若旧连接的 close callback 延迟执行，而新连接刚好拿到同一个 fd，旧的 remove 可能误删新 Session。

修复：

- 新增 `next_session_id_`。
- `TcpServer::createSessionInLoop()` 分配单调递增的 `std::uint64_t` id。
- `Session` 保存该 id，并暴露 `id()`。
- `sessions_` 以逻辑 session id 为 key，不再以 fd 为 key。
- `sendToSession()` 改为接收 `std::uint64_t`。

回归覆盖：

```cpp
TEST(ReactorInterfaceTest, TcpServerHeaderIsSelfContained)
TEST(TcpServerTest, SendToSessionFromOtherThreadDeliversPacket)
```

API 和测试现在使用 `session->id()`，不再使用 `session->fd()` 作为业务身份。

### 4. Channel 遇到 EPOLLERR 后直接返回

旧结构：

```text
EPOLLERR -> error callback -> return
```

如果 epoll 同时返回 `EPOLLERR | EPOLLIN`，read callback 会被跳过。但此时 socket 里可能仍有可读数据，或者需要让 `Session::handleRead()` 消费 EOF 状态。

修复：

- `EPOLLERR` 仍然先执行 error callback。
- 不再在 error callback 后立即返回。
- 继续根据同一份 `revents_` 快照分发 read / write 事件。

回归测试：

```cpp
TEST(ChannelTest, ErrorWithReadableEventInvokesErrorThenRead)
```

### 5. Acceptor close 在 loop 退出竞态下可能永久等待

外部评审建议把 `Acceptor::close()` 改成 owner-loop-only。第一轮 hardening 当时已有测试覆盖跨线程 close 契约，因此没有在那一轮直接改掉这个契约。但这个建议暴露了当时实现中的一个真实竞态：

```text
非 loop 线程调用 Acceptor::close()
  -> loop 当时还没停止
  -> close task 被排队
  -> close 等待 future
  -> loop 在执行 close task 前退出
  -> future 永远不会被 fulfilled
```

当时修复：

- 保留当时的跨线程 close 契约。
- close task 排队后，用短间隔等待。
- 如果 future ready 前 `EventLoop::isStopped()` 变为 true，就执行 stopped-loop fallback cleanup 并返回。

回归测试：

```cpp
TEST(AcceptorTest, CloseFromOtherThreadWhileLoopExitsWithQueuedCloseDoesNotBlock)
```

测试让 loop 卡在一个 pending task 内，随后从其他线程调用 `close()`，再让 pending task 在 queued close task 执行前调用 `quit()`。`close()` 必须返回，不能永久阻塞。

后续 cleanup 已删除这个跨线程 close 契约。当前代码用 owner-loop-only 规则直接避免这类等待分支：非 owner 线程调用 `Acceptor::close()` 会 `std::terminate()`，由 `AcceptorTest.CloseFromNonOwnerThreadTerminates` 覆盖。

## 未按原建议直接采纳的点

### Acceptor::close owner-loop-only 重写

这个建议在 2026-05-10 后续 cleanup 中已采纳。`Acceptor::close()` 不再投递任务、不等待 future，也不依赖 `EventLoop::isStopped()`；跨线程关闭由 `TcpServer` 或更上层先投递回 base loop，再在 owner loop 内同步关闭。

### ThreadPool swap-and-join 重写

外部评审建议用 swap-and-join 替代 `stop_mutex_`。当前代码已经序列化外部 `stop()` 调用，并且有并发 stop 回归测试。worker 内部调用 `stop()` 是刻意保留的 owner-cleanup 边界：worker 只请求停止，owner 后续 join 并清空 `workers_`。

不支持在 `ThreadPool` 自己的任务内部 delete 这个 `ThreadPool` 对象。即使 detach 当前 worker，也不能让这种做法安全，因为 worker 返回后仍会继续进入 `workerLoop()` 并使用 `this`。

### 大范围变量合并

一些风格建议适合作为后续清理，但不应该混入本轮生命周期 bugfix：

- 把 `Session` 多个 bool 收敛成完整 enum 状态机。（2026-05-10 已完成）
- 精简 `EventLoop` flag。（2026-05-10 已删除 `isStopped()` / `loop_exited_`）
- 重做 `FrameDecoder` 输入缓冲结构。
- 删除 `EventLoopThreadPool` 中偏测试用途的 getter。

这些修改需要单独测试，不能和生命周期 bugfix 混在一起。

## 验证

定向验证：

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R "(Acceptor|EventLoopThread|Session|TcpServer|Channel)"
```

预期结果：所有定向测试通过。

最终补丁还需要跑全量验证：

```bash
ctest --test-dir build --output-on-failure
./build/server/liteim_server
git diff --check
```

## 面试回答

一个简洁说法：

> 外部评审后，我没有盲目接受所有风格建议，而是先用回归测试复现了 5 个真实问题：`EventLoopThread` 自线程 stop 的生命周期问题、`Acceptor` queued close 在 loop 退出竞态下永久等待、`Session` 缺少输出缓冲高水位、`TcpServer` 用 fd 当 session id 导致 fd 复用误删风险，以及 `Channel` 遇到 `EPOLLERR | EPOLLIN` 时先 error 后直接返回导致 read 被吞掉。第一轮 hardening 中，我把 `EventLoopThread` 状态清理移动到 `threadFunc()` 退出路径，当时先给 `Acceptor` 跨线程 close 增加 stopped-loop fallback，给 `Session` 增加 4MB 高水位关闭路径，给 `TcpServer` 改成单调递增逻辑 session id，并让 `Channel` 在 error callback 后继续处理同一轮 read 事件。后续 cleanup 又把 `Acceptor::close()` 收紧为 owner-loop-only，删除 `EventLoop::isStopped()` / `loop_exited_`，并把 `Session` 生命周期状态收敛成 `SessionState`。
