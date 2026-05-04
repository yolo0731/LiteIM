# LiteIM Tutorials

这个目录是 LiteIM 的教学专用目录，和正式源码分开。

你学习这个项目时，建议按下面顺序看：

1. `00_roadmap.md`：先看项目总共分几步，每一步要学什么、验收什么。
2. `step01_project_init.md`：从空文件夹开始，手把手创建 CMake 工程骨架。
3. 后续每完成一个开发步骤，新增一个 `stepXX_xxx.md` 教学文件。

## 学习原则

每一步都只解决一个核心问题：

- 先理解为什么要做这个模块。
- 再知道这个模块放在哪个目录。
- 然后手写最小代码。
- 最后编译、运行、测试、提交。

不要一次性生成整个项目。这个项目的简历价值来自你能解释每一层为什么这样设计，而不是文件数量多。

## Step 教程写作要求

每个 `stepXX_*.md` 都要写清楚：

- 本 Step 新增功能解决什么问题。
- 新增文件各自负责什么。
- 新增类、函数、接口的作用，包括输入、输出、副作用和边界情况。
- 测试环节：测什么、为什么测、如何运行测试、测试通过说明什么。
- 面试讲法：本 Step 的设计思路、职责边界和常见追问。

教程不要求逐行解释所有语法，但必须让你能在面试中讲清楚“为什么这么设计”。

## 当前教程进度

| 教程 | 状态 | 对应能力 |
| --- | --- | --- |
| Step 1 工程初始化 | 已完成 | CMake、目录组织、构建目标、CTest |
| Step 2 协议基础 Packet | 已完成 | 二进制协议、Header 编码、字段校验 |
| Step 3 FrameDecoder | 已完成 | TCP 粘包/半包处理 |
| Step 4 Buffer | 已完成 | 网络输入/输出缓冲区 |
| Step 5 SocketUtil | 已完成 | Linux socket 工具函数 |
| Step 6 Reactor 接口 | 已完成 | EventLoop、Epoller、Channel 职责拆分 |
| Step 7 Epoller | 已完成 | epoll_create1、epoll_ctl、epoll_wait、LT 模式 |
| Step 8 EventLoop | 已完成 | 事件循环、Channel 分发、quit 退出 |
| Step 9 Channel | 已完成 | fd 事件代理、自动更新 EventLoop、回调分发 |
| Step 10 Acceptor | 已完成 | 非阻塞监听、accept 循环、新连接 callback |
| Step 11 Session | 已完成 | 单连接生命周期、读写缓冲、Packet 解码和发送 |
| Step 12 TcpServer | 已完成 | 组合 Acceptor/Session、连接表、按 fd/user 发送、signalfd 优雅关闭 |
| Step 13 MessageRouter | 已完成 | 按 msg_type 分发、心跳响应、未知类型错误响应 |
| Step 14 IStorage / ICache | 已完成 | 存储抽象、缓存抽象、NullCache no-op 实现 |
| Step 15 SQLiteStorage | 已完成 | SQLite schema、prepared statement、用户/好友/群组/消息持久化 |
