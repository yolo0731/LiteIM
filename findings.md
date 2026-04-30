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

- `Buffer` belongs to `server/net/`, not `server/protocol/`.
- `Buffer` is a generic byte container for future `Session` input and output buffers.
- `Buffer` must not call `read()`, `write()`, or know about `Packet`.
- `FrameDecoder` may keep its own internal protocol buffer for now; Step 4 does not refactor it.
- Use a simple `std::string` plus `read_index_` design to avoid erasing from the front on every partial retrieve.
- `Buffer::retrieve(len)` clears the whole buffer when `len >= readableBytes()`.
- `Buffer::append(nullptr, 0)` is allowed as a no-op; `append(nullptr, nonzero)` throws `std::invalid_argument`.
- Step 4 introduces `liteim_net` as a separate static library for network-layer components.

## Step 5 Design Notes

- `SocketUtil` belongs to `server/net/` and should be part of `liteim_net`.
- `SocketUtil` only wraps low-level Linux socket helpers; it must not implement `Acceptor`, `Session`, or epoll.
- `createNonBlockingSocket()` should create an IPv4 TCP socket with nonblocking behavior.
- `setNonBlocking()` should use `fcntl()` so it can also be applied to accepted connection fds later.
- `setReuseAddr()` and `setReusePort()` should wrap `setsockopt()`.
- `getSocketError()` should wrap `getsockopt(SO_ERROR)`.
- System call failures should print `errno` and a readable error message.

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
