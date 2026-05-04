# LiteIM Findings

## Project Memory

- Root project memory lives at `/home/yolo/jianli/PROJECT_MEMORY.md`.
- Step workflow: concept first, code second, tests third, git commit last.
- Each Step must explain the test section: what the new tests verify, why those cases matter, and how to run them.
- `docs/` should primarily use Chinese explanations.
- Current LiteIM Step 4 commit message should be `feat(net): add buffer abstraction`.

## Planning With Files

- Skill installed at `/home/yolo/.codex/skills/planning-with-files/SKILL.md`.
- The skill requires `task_plan.md`, `findings.md`, and `progress.md` in the project root.
- `session-catchup.py` was run for `/home/yolo/jianli/LiteIM` and produced no output.

## Step 4 Design Notes

- `Buffer` belongs to the net module: header in `include/liteim/net/`, implementation in `src/net/`.
- `Buffer` is a generic byte container for future `Session` input and output buffers.
- `Buffer` must not call `read()`, `write()`, or know about `Packet`.
- `FrameDecoder` may keep its own internal protocol buffer for now; Step 4 does not refactor it.
- Use a simple `std::string` plus `read_index_` design to avoid erasing from the front on every partial retrieve.
- `Buffer::retrieve(len)` clears the whole buffer when `len >= readableBytes()`.
- `Buffer::append(nullptr, 0)` is allowed as a no-op; `append(nullptr, nonzero)` throws `std::invalid_argument`.
- Step 4 introduces `liteim_net` as a separate static library for network-layer components.

## Step 5 Design Notes

- `SocketUtil` belongs to the net module and should be part of `liteim_net`.
- `SocketUtil` only wraps low-level Linux socket helpers; it must not implement `Acceptor`, `Session`, or epoll.
- `createNonBlockingSocket()` should create an IPv4 TCP socket with nonblocking behavior.
- `setNonBlocking()` should use `fcntl()` so it can also be applied to accepted connection fds later.
- `setReuseAddr()` and `setReusePort()` should wrap `setsockopt()`.
- `getSocketError()` should wrap `getsockopt(SO_ERROR)`.
- System call failures should print `errno` and a readable error message.

## Step 6 Design Notes

- Step 6 only defines the Reactor core header interfaces. Do not implement epoll operations or callback dispatch yet.
- `Epoller` should own the future `epoll` fd and expose `updateChannel()`, `removeChannel()`, and `poll()`.
- `Channel` should bind one fd to its interested events, returned events, and callbacks. It should not own the fd.
- `EventLoop` should own an `Epoller` and expose `loop()`, `quit()`, `updateChannel()`, and `removeChannel()`.
- Use forward declarations between `EventLoop`, `Epoller`, and `Channel` to avoid header include cycles.
- Because methods are declared but not implemented in Step 6, tests must not construct these classes or call methods requiring definitions.
- Interface tests should include all three headers together, use type traits/static assertions, and verify event constants. Passing this test proves the headers are self-consistent and ready for later implementation.

## Layout Refactor Design Notes

- Mature C++ projects commonly separate headers from implementation:
  - `include/liteim/...` exposes include paths used by other targets.
  - `src/...` contains `.cpp` implementation files for libraries.
  - `server/main.cpp` remains the executable entry point.
- Use project-qualified include paths such as `liteim/net/Buffer.hpp` instead of `net/Buffer.hpp`.
- Build libraries from `src/CMakeLists.txt` and link them from `server` and `tests`.
- Keep behavior unchanged: no Step 7 epoll implementation, no new networking runtime behavior.
- Documentation must be updated with the new structure because stale path docs hurt teaching and interview review.

## Step 7 Design Notes

- The current authoritative Step 7 is `Epoller`, based on `/home/yolo/jianli/PROJECT_MEMORY.md` and the active `task_plan.md`.
- `tutorials/00_roadmap.md` still has older wording that maps Step 6 to `Epoller` and Step 7 to `Channel`; update it during Step 7 documentation so it matches the actual completed Step 6 interface split.
- `Epoller` owns only the `epoll` fd. It does not own `Channel` objects or socket fds.
- Store `Channel*` in `epoll_event.data.ptr`, because `EventLoop` will later dispatch events through `Channel` instead of raw fd-only state.
- Use `epoll_create1(EPOLL_CLOEXEC)` so child processes do not inherit the epoll fd after future `exec`.
- Use LT mode only. Do not add `EPOLLET` in `Epoller`; callers provide plain interested events such as `EPOLLIN` and `EPOLLOUT`.
- `updateChannel()` should use `EPOLL_CTL_ADD` the first time a fd appears and `EPOLL_CTL_MOD` after that.
- `removeChannel()` should erase registered fd state after `EPOLL_CTL_DEL`; repeated remove on an unknown fd should be a no-op.
- `poll()` should handle `EINTR` by returning an empty active-event list, so future signal interruptions do not crash the event loop.
- Step 7 needs minimal `Channel` state definitions to construct test channels and expose fd/event masks to `Epoller`; callback setters are simple state setters, while `handleEvent()` dispatch and automatic `EventLoop` updates remain later-step work.

## Step 8 Design Notes

- The authoritative Step 8 is `EventLoop` skeleton from `/home/yolo/jianli/PROJECT_MEMORY.md`.
- `EventLoop` is the Reactor scheduling layer: it owns `Epoller`, polls active events, and asks each active `Channel` to handle its event.
- `EventLoop` does not own `Channel` objects or socket fds. Later `Acceptor` and `Session` objects will own their channels/fds and unregister before destruction.
- `quit()` should be safe to call from a callback while `loop()` is running. Use an atomic stop flag so tests and future cross-thread shutdown paths do not introduce a data race.
- `quit()` does not add a wakeup fd in Step 8. If another thread calls `quit()` while `epoll_wait()` is blocked, the loop exits after the next event or timeout. A future `eventfd`/`signalfd` wakeup can improve this.
- `Channel::handleEvent()` needs a concrete callback dispatch implementation in Step 8 because `EventLoop::loop()` calls it and tests should verify read callbacks can stop the loop.
- Keep Step 9 meaningful by not wiring `Channel::enableReading()` / `enableWriting()` to automatically call `EventLoop::updateChannel()` yet. Step 8 uses explicit `loop.updateChannel(&channel)`.
- EventLoop tests should use `pipe()` as a real fd source, matching Step 7 Epoller tests, so they verify actual `epoll_wait()` integration rather than only in-memory state.

## Step 9 Design Notes

- The authoritative Step 9 is `Channel` plus automatic `EventLoop` integration from `/home/yolo/jianli/PROJECT_MEMORY.md`.
- Most `Channel` state and callback dispatch already landed in Step 7 and Step 8; Step 9 should focus on the missing private `Channel::update()` bridge.
- `enableReading()`, `enableWriting()`, `disableWriting()`, and `disableAll()` should remain the public semantic API. Callers should not need to manually call `loop.updateChannel(&channel)` after Step 9.
- When a `Channel` still has interested events, `update()` should call `EventLoop::updateChannel(this)`.
- When a `Channel` has no interested events, `update()` should call `EventLoop::removeChannel(this)` so epoll stops tracking that fd instead of registering a zero event mask.
- `Channel` does not own the fd or the loop. Future `Acceptor` and `Session` must unregister their channel before closing the fd or destroying the channel.
- Existing low-level Epoller tests construct `Channel(nullptr, fd)` to test `Epoller` directly. Step 9 should preserve that by letting null-loop channels update local event masks without touching an `EventLoop`; production objects should pass a real loop.
- Step 9 tests should include real `pipe()` fd behavior to prove automatic registration/removal works through `EventLoop` and `Epoller`, not only direct `handleEvent()` calls.

## Step 10 Design Notes

- The authoritative Step 10 is `Acceptor` from `/home/yolo/jianli/PROJECT_MEMORY.md`.
- `Acceptor` should own only the listen fd and the listen `Channel`.
- Accepted client fds should be created with `accept4(..., SOCK_NONBLOCK | SOCK_CLOEXEC)`.
- `handleRead()` should loop accept until `EAGAIN` / `EWOULDBLOCK`; this avoids leaving pending connections queued after one readiness notification.
- `EINTR` during accept should retry, and `ECONNABORTED` can be skipped because the peer may close before accept completes.
- If no new-connection callback is installed, accepted fds should be closed immediately to avoid leaks.
- Once a callback is called successfully, ownership of the accepted fd moves to the callback. Future `TcpServer` will create `Session` objects from those fds.
- Tests should bind to `127.0.0.1:0` so the OS chooses an available local port, then query the actual port from `Acceptor`.
- Tests should use real localhost TCP connections, not mocks, so they verify `socket` / `bind` / `listen` / `accept4` / `EventLoop` integration.

## Step 11 Design Notes

- The authoritative Step 11 is `Session` from `/home/yolo/jianli/PROJECT_MEMORY.md`.
- `Session` should own one connected client fd and one `Channel`; it should unregister before closing.
- Use an explicit `start()` method so future `TcpServer` can set message/close callbacks before read interest is registered.
- `handleRead()` should loop `read()` until `EAGAIN` / `EWOULDBLOCK`; `EINTR` retries; read 0 means peer closed.
- Read bytes should go into `FrameDecoder`; complete packets should call `MessageCallback`.
- If `FrameDecoder` enters error state, close the session. Protocol behavior remains unchanged.
- `sendPacket()` should call `encodePacket()`, append to `output_buffer_`, and enable write interest. It should not bypass the output buffer.
- `handleWrite()` should write from `output_buffer_.peek()` and call `retrieve(n)` for successful writes; disable writing when the buffer is empty.
- A close callback should be fd-based so future `TcpServer` can erase the session from its map without exposing ownership details in Step 11.
- Tests can use `socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)` as a connected stream fd pair; this verifies nonblocking read/write/event-loop behavior without requiring full `TcpServer`.
- Preserve the uncommitted `tutorials/step10_acceptor.md` wording change as pre-existing user/worktree state and do not stage it into the Step 11 commit.

## Testing Explanation Requirement

- Future Step tutorials and final responses must include a short testing explanation.
- The testing explanation should cover test files, test purpose, normal cases, edge/error cases, commands, and what passing tests prove.
- Do not only list `ctest`; explain why the tests exist.

## Tutorial Depth Requirement

- Future `tutorials/stepXX_*.md` files must explain each new public function/interface in that Step.
- Function explanations should include purpose, inputs, outputs, side effects, and edge/error behavior.
- Do not use interface overview tables in Step tutorials. Explain functions one by one in the `.hpp` / `.cpp` sections instead.

## Step 12 Design Notes

- The authoritative Step 12 is `TcpServer` from `/home/yolo/jianli/PROJECT_MEMORY.md`.
- `TcpServer` should coordinate `EventLoop`, `Acceptor`, and `Session`; it should not implement `MessageRouter`, login, chat, or storage.
- The existing roadmap expects `TcpServer server(&loop, "0.0.0.0", 9000); server.start(); loop.loop();`, so `TcpServer` should hold an `EventLoop*` rather than create a private blocking loop.
- `Acceptor` owns the listen fd and should expose an orderly close path so `TcpServer::stop()` can stop accepting before the object is destroyed.
- `TcpServer` should store sessions as `std::shared_ptr<Session>` keyed by fd, but close callbacks must not destroy the current `Session` while `Session::closeConnection()` is still on the stack. Retire closed sessions briefly before cleanup.
- `sendToSession()` should look up an active session by fd and call `Session::sendPacket()`.
- `sendToUser()` can be a foundation API backed by an explicit `user_id -> session_fd` binding map; Step 12 should not infer user identity from packets.
- `signalfd` should be registered through `Channel` in the same `EventLoop`, with `SIGINT` and `SIGTERM` blocked via `sigprocmask` so the signals are delivered through the fd instead of default process termination.
- On signal shutdown, `TcpServer` should close active sessions, stop accepting, disable the signal channel/fd, and call `EventLoop::quit()`.
- The interview section should explain the Step's design idea, not only provide a short quote.
- Each Step should include common interview follow-up questions with short answers.

## Step 13 Design Notes

- The authoritative Step 13 is `MessageRouter` heartbeat foundation from `/home/yolo/jianli/PROJECT_MEMORY.md`.
- `MessageRouter` belongs to a new `service` module: header in `include/liteim/service/`, implementation in `src/service/`.
- `MessageRouter` should receive `net::Session&` and `protocol::Packet`, inspect only `packet.header.msg_type`, and reply through `Session::sendPacket()`.
- The first supported route is `HEARTBEAT_REQ -> HEARTBEAT_RESP`.
- Unknown message types should return `ERROR_RESP` so clients get deterministic feedback instead of silent drops.
- Router-generated responses should preserve the request `seq_id` so clients can correlate request/response pairs.
- Step 13 should integrate router wiring in `server/main.cpp` by installing a `TcpServer::setMessageCallback()` handler.
- Step 13 must not implement registration, login, storage, private chat, group chat, friend list, history, or heartbeat timeout cleanup.
- `MessageRouter` must not operate on raw fd values or manage sessions; it depends on `Session::sendPacket()` as the network-layer boundary.
