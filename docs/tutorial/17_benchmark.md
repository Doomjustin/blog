# 附录：跨线程通信基准测试——ChannelPipe vs Asio

> **源文件**：[tutorial/21_benchmark/post.cpp](../../tutorial/21_benchmark/post.cpp)
> **可执行文件**：`./build/tutorial/21_benchmark/tutorial.21_post`

---

## 对比对象

Asio 的跨线程任务投递使用 `asio::post(ctx, lambda)`：一次 post 等于在对端 `io_context` 的任务队列里塞入一个堆分配的闭包，然后通过 eventfd 唤醒对端。

我们的 `ChannelPipe<T>` 使用预分配节点池 + MPSC 无锁入队 + io_uring eventfd 唤醒，热路径零堆分配。

---

## 运行方式

```bash
./build/tutorial/21_benchmark/tutorial.21_post
```

实测输出（本次会话）：

```text
[info] 🚀 开始与工业界标杆进行吞吐量极限测试...
[info] === 选手 A: Boost.Asio (io_context::post) ===
[warning] [Boost.Asio] 发送 1000000 条跨线程消息，总耗时: 372 ms
[info] === 选手 B: async::ChannelPipe (TPC 无锁架构) ===
[info] [TPC 协程模型] 发送 1000000 条跨线程消息，总耗时: 206 ms
```

---

## 为什么 ChannelPipe 更快

Asio 每次 `post` 都要 `new` 一个持有 lambda 的对象，`io_context` 消费时再 `delete`。100 万次 post = 100 万次 `malloc/free`，这是主要差距来源。

`ChannelPipe` 在构造时一次性分配 capacity 个节点，发送时从 `free_list_` 弹出节点，接收侧消费完后批量归还（`credit_batch = capacity/4`）。整个热路径不触碰堆分配器。

---

## 为什么 ChannelPipe vs std::mutex 反而更慢

`std::mutex + condition_variable` 的微基准测出约 141ms，而 `ChannelPipe` 约 230ms。原因是两者的内核边界不对等：

- `std::mutex`：生产者疯狂 push 时消费者往往还在用户态 `wait`，两者可以在用户态直接握手，进内核的频率极低。
- `ChannelPipe`：每条消息都要经过 io_uring 的 CQE 派发路径，这是一次不可绕过的内核调度点。

这个差距在真实服务里无意义——mutex 版本无法感知 IO 事件，而 ChannelPipe 的事件循环同时处理网络 IO、定时器和 Channel 消息，mutex 根本没有同等能力的参照物。

---

# 附录：网络 Echo 基准测试——TPC vs Asio

> **源文件目录**：`tutorial/21_benchmark/`
> **客户端**：`./build/tutorial/21_benchmark/tutorial.21_network_client <port>`

---

## 测试设计

用 100 个并发连接，每条连接做 10000 次 Ping-Pong，共计 **100 万次往返**。

客户端由 TPC 框架统一实现（`network_client.cpp`），服务端替换为不同实现以隔离变量：

| 二进制 | 服务端实现 | 端口 |
|--------|-----------|------|
| `tutorial.21_network_asio` | Asio (epoll) | 10086 |
| `tutorial.21_network_asio_uring` | Asio (io_uring, `ASIO_DISABLE_EPOLL`) | 10086 |
| `tutorial.21_network_tpc` | TPC + Buffer Ring | 10087 |
| `tutorial.21_network_tpc_local_buf` | TPC + 本地栈上 buffer | 10088 |
| `tutorial.21_network_tpc_zc` | TPC + `IORING_OP_SEND_ZC` | 10089 |

两个关键修正确保测试公平有效：

1. **严格对齐收发长度**：`net::receive` 底层是 `ReceiveAllAwaiter`，语义是"填满 buffer 才返回"。若 `recv_buf` 比实际 payload 大，接收方会永久等待剩余字节，造成死锁。
2. **关闭 Nagle 算法**：服务端和客户端均设置 `TCP_NODELAY`，消除操作系统默认的 40ms 合包延迟。

---

## 运行方式

```bash
# 终端 1：启动服务端（以 TPC Buffer Ring 版为例）
./build/tutorial/21_benchmark/tutorial.21_network_tpc

# 终端 2：启动客户端
./build/tutorial/21_benchmark/tutorial.21_network_client 10087
```

---

## 实测结果

```text
[Asio epoll]                  完成 1000000 次网络请求，总耗时: 5094 ms
[Asio io_uring]               完成 1000000 次网络请求，总耗时: 4667 ms
[TPC send_zc]                 完成 1000000 次网络请求，总耗时: 3787 ms
[TPC 本地 buffer]              完成 1000000 次网络请求，总耗时: 3101 ms
[TPC Buffer Ring]             完成 1000000 次网络请求，总耗时: 3030 ms
```

TPC 比 Asio epoll 快约 **1.68×**。

---

## 结果分析

### Asio epoll vs Asio io_uring（5094 ms → 4667 ms）

仅加 `ASIO_HAS_IO_URING` 不够，还必须同时定义 `ASIO_DISABLE_EPOLL` 才能让 TCP socket 操作走 io_uring。即便如此，Asio 的 io_uring 路径仍以**单条 SQE 逐次提交**为主，每次 `async_read_some` / `async_write` 单独占一个提交槽，无法批量。提升约 8%，瓶颈不在 I/O 后端。

### Asio vs TPC 本地 buffer（4667 ms → 3101 ms）

差距来自 **事件循环架构**：

- Asio 的 `io_context` 内部有一把全局锁，每次 handler 入队出队都要争锁；协程恢复时会触发一次 `post`，再次争锁。
- TPC 的 `IOContext` 是单线程无锁的，SQE 提交和 CQE 收割都在同一个线程内完成，无锁争用。

### TPC 本地 buffer vs TPC Buffer Ring（3101 ms → 3030 ms）

差距仅 2%。Buffer Ring 的优势（消除 `recv` 的用户态 buffer 分配、支持 `recv_multishot`）在 22 字节小包的 Ping-Pong 测试里被放大空间有限。Buffer Ring 的真正价值在于**高并发流式接收**（如 HTTP/WebSocket），减少大量小包的堆分配累积压力。

### TPC 本地 buffer vs TPC send_zc（3101 ms → 3787 ms，**更慢**）

`IORING_OP_SEND_ZC` 需要等待内核通过额外一个 CQE 通知"DMA 完成，buffer 可以释放"。对于 22 字节的 echo，零拷贝节省的内存带宽远小于多一次 CQE 等待的开销。send_zc 的收益区间是**单次发送 ≥ 16KB** 的大包场景。
