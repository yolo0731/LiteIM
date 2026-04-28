# LiteIM Architecture

This document will describe the server architecture after the Reactor components are implemented.

Planned modules:

- net: EventLoop, Epoller, Channel, Acceptor, Session, Buffer
- protocol: Packet, MessageType, FrameDecoder
- service: MessageRouter, AuthService, ChatService, GroupService, BotService
- storage: SQLiteDB and repositories
- timer: heartbeat timeout management

