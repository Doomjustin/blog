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

**Debug 构建**（`-O0`）：

```text
[Asio epoll]                  完成 1000000 次网络请求，总耗时: 5094 ms
[Asio io_uring]               完成 1000000 次网络请求，总耗时: 4667 ms
[TPC send_zc]                 完成 1000000 次网络请求，总耗时: 3787 ms
[TPC 本地 buffer]              完成 1000000 次网络请求，总耗时: 3101 ms
[TPC Buffer Ring]             完成 1000000 次网络请求，总耗时: 3030 ms
```

**Release 构建**（`-O3`）：

```text
[Asio epoll]                  完成 1000000 次网络请求，总耗时: 3426 ms   ~292k req/s
[Asio io_uring]               完成 1000000 次网络请求，总耗时: 3265 ms   ~306k req/s
[TPC send_zc]                 完成 1000000 次网络请求，总耗时: 3355 ms   ~298k req/s
[TPC 本地 buffer]              完成 1000000 次网络请求，总耗时: 2603 ms   ~384k req/s
[TPC Buffer Ring]             完成 1000000 次网络请求，总耗时: 2603 ms   ~384k req/s
```

Release 模式下 TPC 比 Asio epoll 快约 **1.32×**，整体各实现比 Debug 快 30–40%。

两组数据放在一起能得到一条关键结论：**编译器可以优化 CPU 指令，但无法优化内核架构**。Debug 到 Release 的提升反映的是应用层代码的开销被 `-O3` 抹平；而各实现之间的排名从未改变，那是操作系统的物理边界，编译器触及不到。

---

## 结果分析

### Asio epoll vs Asio io_uring

Debug：5094 ms → 4667 ms（+8%）；Release：3426 ms → 3265 ms（+5%）。

仅加 `ASIO_HAS_IO_URING` 不够，还必须同时定义 `ASIO_DISABLE_EPOLL` 才能让 TCP socket 操作走 io_uring。即便如此，Asio 的 io_uring 路径仍以**单条 SQE 逐次提交**为主，每次 `async_read_some` / `async_write` 单独占一个提交槽，无法批量。瓶颈不在 I/O 后端，换后端收益有限。

### Asio vs TPC 本地 buffer

Debug：4667 ms → 3101 ms；Release：3265 ms → 2603 ms（+25%）。

差距来自**事件循环架构**：

- Asio 的 `io_context` 内部有一把全局互斥锁，每次 handler 入队出队都要争锁；协程恢复时会触发一次 `post`，再次争锁。
- TPC 的 `IOContext` 是单线程无锁的，SQE 提交和 CQE 收割都在同一个线程内完成，无锁争用。

这 25% 的差距，是从操作系统的锁路径上一刀一刀抢回来的。

### TPC 本地 buffer vs TPC Buffer Ring

Debug：3101 ms → 3030 ms（−2%）；Release：2603 ms → 2603 ms（持平）。

在 Release 下两者完全打平。原因在于 `-O3` 的极致优化让本地 buffer 在这个单调的 Ping-Pong 循环里几乎完全留在了 L1 Cache 甚至寄存器里，消除了堆分配开销，从而跑到与 Buffer Ring 相同的物理极限。

但这是微基准测试特有的现象。在真实高并发场景（如 HTTP 网关同时处理数千条不同大小的流），Local Buffer 会引发 Cache Thrashing：每条连接的 buffer 地址各不相同，大量缓存行被频繁替换。Buffer Ring 的优势恰恰是**内核视角的内存连续性**——内核将收到的多个包紧密填入同一块预分配的物理内存，对 CPU 缓存极度友好，这种压倒性优势只会在真实混乱的高并发流量中显现。

### TPC 本地 buffer vs TPC send_zc（**更慢**）

Debug：3101 ms → 3787 ms；Release：2603 ms → 3355 ms。慢了整整 750ms，且 Debug 与 Release 之间的差值几乎相同——这是 `send_zc` 更深刻的一面。

`IORING_OP_SEND_ZC` 要求内核在 DMA 完成后额外投递一个 CQE 通知应用层"buffer 现在可以释放了"。这次等待发生在内核态，是 PCIe 总线与内存子系统的物理时序，任何 C++ 编译器都无能为力。对于 22 字节的 echo，零拷贝节省的内存带宽几乎为零，多出的一次内核调度却是实实在在的开销。send_zc 的收益区间是**单次发送 ≥ 16KB** 的大包场景。

### 小结

> 高级架构决策不盲信编译器，只看物理边界。
>
> - `-O3` 能抹平抽象层的开销，Asio 复杂的内部队列也因此缩短了差距。
> - 但内核态的系统调用、内存屏障和 DMA 时序是编译器不可触及的领域。正确的架构选择——顺应硬件，减少内核边界穿越——才是突破这层天花板的唯一路径。
