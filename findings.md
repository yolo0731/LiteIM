# LiteIM Findings

## 权威来源

- `/home/yolo/jianli/PROJECT_MEMORY.md` 是 LiteIM 和 PersonaAgent 的唯一总方案来源。
- LiteIM 现在从 `Step 0` 重新开始。
- 当前路线是 `LiteIM High Performance + Qt Client + PersonaAgent Authorized Style RAG Edition`。
- 如果 `README.md`、`task_plan.md`、`progress.md`、教程或源码与 `PROJECT_MEMORY.md` 冲突，统一改回 `PROJECT_MEMORY.md` 的路线。

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
- `Session` 收到完整 `Packet` 后刷新 `last_active_time`，让应用层心跳包或任意有效业务包都能续期连接。

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
- `sendToUser()` 当前保留基础接口并返回 `NotFound`，因为 user-session 绑定属于后续登录业务 Step。

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
- 旧 `tutorials/step01-step15` 教程。
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
- `tutorials/README.md` 和 `tutorials/step00_reset.md`。

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
- 消息类型按范围分组：心跳、认证、好友、私聊、群聊、离线/历史、Bot 和错误响应。
- 私聊、群聊和 Bot 都保留 `Push` 类型，用于后续服务端向接收方主动推送消息；`Push` 既不是 request，也不是 response，但需要通过 `isPushType()` 显式识别，避免和 `Unknown` 混在一起。
- `isRequestType()` 只返回客户端或 BotClient 主动请求类型。
- `isResponseType()` 只返回服务端对请求的响应类型，`ErrorResponse` 归为 response，方便后续统一错误返回。
- `isPushType()` 只返回 `PrivateMessagePush`、`GroupMessagePush` 和 `BotMessagePush`。
- `ListGroupsRequest` / `ListGroupsResponse` 已放入 4xx 群聊类型段，避免 Step 37 再回头修改协议编号。
- 未知 `MessageType` 和 `TlvType` 都返回 `UNKNOWN`，后续解析到未注册类型时可以安全记录日志而不是崩溃。
- `TlvType` 先覆盖登录、用户、好友、群组、会话、消息、错误和 Bot/Persona 接入需要的字段类型。
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
- `tutorials/step11_channel.md`

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
- `tutorials/step12_event_loop.md`

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
- `tutorials/step13_acceptor.md`

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
- PersonaAgent 通过 Python BotClient 使用同一套 TLV 协议登录 LiteIM，作为普通 Bot 用户收消息、发消息。
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
- `tutorials/README.md` 不再维护；`tutorials/` 只保留每个 Step 的教程文件。
- `task_plan.md`、`findings.md`、`progress.md` 是过程记忆，应尽量保留历史内容；纠正记录只追加，不用摘要覆盖。
- 主 README 不写 `Current Status` / `当前状态` 标题，也不把 planning 三件套当成对外介绍内容。
