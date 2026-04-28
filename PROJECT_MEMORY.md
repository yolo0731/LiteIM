# LiteIM Project Memory

这个文件用于保存 LiteIM 项目的长期协作约束。每次重新进入本项目时，先阅读本文件，再继续开发。

## 当前项目目标

正在实现一个分阶段项目：

1. LiteIM：基于 C++17 / Qt / epoll 的类微信即时通讯聊天室。
2. PersonaAgent：基于 LangGraph + RAG 的个性化 AI 聊天机器人。

当前优先级是 LiteIM。不要同时推进两个项目。先把 LiteIM 服务端做扎实，再接 Qt 客户端和 PersonaAgent。

## Step 开发规则

每个 Step 必须做到：

1. 当前 Step 完成后项目能编译。
2. 当前 Step 涉及的测试必须能通过。
3. 不要提前实现后续 Step 的复杂功能。
4. 不要使用 Boost.Asio。
5. C++ 服务端使用 C++17、CMake、Linux socket、epoll、SQLite、nlohmann_json。
6. 每个 Step 只修改与当前任务相关的文件。
7. 每个 Step 结束后给出本次新增文件、修改文件、如何编译、如何测试。

## 教学方式

从 Step 1 开始，每一步都按下面顺序推进：

1. 先讲概念：解释为什么需要这个模块，它解决什么问题。
2. 再手写代码：逐个文件说明代码位置、代码内容和代码用途。
3. 再测试验证：运行编译和测试命令，确认当前 Step 可用。
4. 最后提交 Git：用清晰的提交信息保存当前 Step。

## 当前推荐推进顺序

LiteIM 当前按下面顺序推进：

1. Step 1：CMake 工程初始化。
2. Step 2：协议基础 Packet。
3. Step 3：FrameDecoder。
4. Step 4：Buffer。
5. Step 5：SocketUtil。
6. Step 6：Epoller。
7. Step 7：Channel。
8. Step 8：EventLoop。
9. Step 9：Acceptor。
10. Step 10：Session。
11. Step 11：TcpServer。
12. Step 12：MessageRouter 和心跳响应。
13. Step 13：SQLite。
14. Step 14：注册和登录。
15. Step 15：私聊。

先完成前 15 步，形成 C++ 服务端 MVP，再继续群聊、历史消息、心跳、命令行客户端、Qt 客户端和 PersonaAgent。

