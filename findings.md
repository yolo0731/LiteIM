# LiteIM Findings

## Project Memory

- Root project memory lives at `/home/yolo/jianli/PROJECT_MEMORY.md`.
- Step workflow: concept first, code second, tests third, git commit last.
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
