# 教程

本教程面向熟悉 C++ 但没有异步 I/O 经验的读者，逐章介绍如何用这个基于 io_uring + C++23
协程的库编写高性能网络程序。每一章只引入一个新概念，代码可直接编译运行。

## 第 0 部分：开始之前

- [0.1 机制概览](00_overview.md) — io_uring、事件循环、协程状态机
- [0.2 环境搭建](00_setup.md) — 依赖、构建、验证

## 第 1 部分：协程与控制流机制

- [1.1 第一个协程](01_hello.md) — `Task<>`、`async::run`
- [1.2 挂起与恢复](01_sleep.md) — `sleep_for`，事件循环如何接管执行权
- [1.3 值传递与错误模型](01_return_value.md) — `Task<T>`、`std::expected<T,E>`
- [1.4 并发调度与生命周期](01_co_spawn.md) — `co_spawn`，协程帧所有权

## 第 2 部分：结构化网络 I/O

- [2.1 TCP 客户端](02_tcp_client.md) — `connect`、`net::send`、`net::receive`，结合 `expected` 的标准错误处理
- [2.2 TCP 服务端](02_tcp_server.md) — `acceptor`，多 session 并发，SIGINT 优雅停机
- [2.3 流式读取](03_receive_stream.md) — `receive_stream`，io_uring Provided Buffers，RAII 内存池
- [2.4 协议分帧](04_line_protocol.md) — 行协议解析，跨 CQE 缓冲，TCP half-close 与 EOF 处理

## 第 3 部分：系统韧性

- [3.1 外部取消模型](05_stop_then.md) — `stop_then`、协作式取消、协程发起者控制的精细化取消
- [3.2 局部时间约束](06_timeout.md) — `timeout`、io_uring 链接超时、`timed_out` 错误码
- [3.3 错误分类与重试策略](07_error_handling.md) — `operation_canceled` vs `timed_out`、指数退避、组合技巧

## 第 4 部分：并发编排与资源收敛

- [4.1 显式状态聚合：when_all](08_when_all.md) — `when_all`、并发操作、`tuple<expected...>` 结果合并
- [4.2 竞速与抢占：when_any](09_when_any.md) — `when_any`、多副本冗余查询、取最快响应
- [4.3 任务树的协作取消：any](10_any.md) — `all`、`any`、任务级并发与取消信号传播
- [4.4 跨任务通信：Channel](11_channel.md) — `Channel<T>`、`ChannelPipe<T>`、owner-thread 与 backpressure

## 第 5 部分：零开销抽象与性能边界

- [5.1 消除系统调用：Scatter/Gather I/O](12_scatter_gather.md)
- [5.2 消除 CPU 拷贝：Zero-copy 发送](13_zero_copy.md)
- [5.3 环形缓冲区调优：buffer_ring 容量规划与内存布局](14_buffer_ring.md)

## 第 6 部分：生产级架构管控

- [6.1 线程模型与局部性：多线程部署策略](15_threading.md)

## 附录

- [附录：跨线程通信基准测试——ChannelPipe vs Asio](16_benchmark.md)
- [附录：CPU 密集型任务的工作线程适配](17_custom_awaiter.md)
