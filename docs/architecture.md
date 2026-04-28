# LiteIM 架构说明

本文档用于记录 LiteIM 的整体架构。后续实现 Reactor、协议层、业务层、存储层和定时器模块时，会逐步补充详细说明。

规划模块：

- `net`：网络层，包含 `EventLoop`、`Epoller`、`Channel`、`Acceptor`、`Session`、`Buffer`。
- `protocol`：协议层，包含 `Packet`、`MessageType`、`FrameDecoder`。
- `service`：业务层，包含 `MessageRouter`、`AuthService`、`ChatService`、`GroupService`、`BotService`。
- `storage`：存储层，包含 `IStorage`、`SQLiteStorage`、`ICache`、`NullCache`。
- `timer`：定时器和心跳超时清理，后续会接入 `timerfd`。

文档目标：

- 说明每一层负责什么。
- 说明模块之间如何依赖。
- 说明为什么网络层、协议层、业务层和存储层要解耦。
