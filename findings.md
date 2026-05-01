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

## Testing Explanation Requirement

- Future Step tutorials and final responses must include a short testing explanation.
- The testing explanation should cover test files, test purpose, normal cases, edge/error cases, commands, and what passing tests prove.
- Do not only list `ctest`; explain why the tests exist.

## Tutorial Depth Requirement

- Future `tutorials/stepXX_*.md` files must explain each new public function/interface in that Step.
- Function explanations should include purpose, inputs, outputs, side effects, and edge/error behavior.
- Do not use interface overview tables in Step tutorials. Explain functions one by one in the `.hpp` / `.cpp` sections instead.
- The interview section should explain the Step's design idea, not only provide a short quote.
- Each Step should include common interview follow-up questions with short answers.
