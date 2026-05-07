# 4.4 跨任务通信：`Channel<T>` 与 `ChannelPipe<T>`

> 前置知识：本章假设你已读完 [4.3（all / any）](10_any.md)，理解结构化并发与协作取消。

---

## 为什么这一章有两个原语

在真实系统里，生产者-消费者并不总是同一个线程：

- 有时它们都在同一个 IOContext 内，只需要低开销地传值。
- 有时它们分属两个 IOContext（网络线程和工作线程），需要跨线程传值与背压。

因此本章分两类：

- `Channel<T>`：单 IOContext 内任务协作。
- `ChannelPipe<T>`：跨 IOContext（跨线程）SPSC 管道。

`ThreadSafeChannel<T>` 目前是实验实现，不作为教程主线。

---

## 先给结论：怎么选

| 场景 | 选型 |
|---|---|
| 同一个 IOContext 内的分层任务（解析、业务、回包都在同线程） | `Channel<T>` |
| 网络线程与工作线程解耦（跨 IOContext） | `ChannelPipe<T>` |
| 想先做性能基线，排除跨线程调度噪声 | 先 `Channel<T>`，后再切 `ChannelPipe<T>` |

口诀：同线程用 `Channel<T>`，跨线程用 `ChannelPipe<T>`。

---

## 示例 A：同线程协作（`Channel<T>`）

现实类比：单连接协议栈里，`parser -> business` 都在同一个事件循环。

```cpp
#include <blog.h>

auto producer(async::Channel<int>& ch) -> async::Task<>
{
    for (int i = 0; i < 5; ++i)
        co_await ch.send(i);
    ch.close();
}

auto consumer(async::Channel<int>& ch) -> async::Task<>
{
    while (auto v = co_await ch.receive()) {
        log::info("value={}", *v);
    }
}

auto run_same_context() -> async::Task<>
{
    async::Channel<int> ch{ 8 };
    co_await async::all(producer(ch), consumer(ch));
}

int main()
{
    async::run(run_same_context);
    return 0;
}
```

要点：

- `close()` 之后是 drain 语义，consumer 会先读完缓冲再收到 `ChannelError::Closed`。
- `timeout(ch.receive(), dur)` 可直接组合。

对应可执行示例：`tutorial/15_channel/main.cpp`  
运行命令：`./build/tutorial/15_channel/tutorial.15_channel`

---

## 示例 B：跨线程解耦（`ChannelPipe<T>`）

现实类比：接入线程收包（生产快），工作线程做 CPU 密集计算（消费慢），通过容量为 4 的有界管道桥接。

```cpp
#include <blog.h>
#include <sleep_for.h>

using namespace std::chrono_literals;

// 消费者：模拟后台计算线程
auto worker_task(async::ChannelReceiver<std::string> rx) -> async::Task<>
{
    log::info("[Worker] started, waiting for requests...");

    // 优雅停机循环：发送端 close 且通道内积压的数据被完全抽干后，
    // receive() 返回 Closed 错误，循环自然退出。
    while (auto task_data = co_await rx.receive()) {
        log::info("[Worker] processing: {}", *task_data);
        co_await async::sleep_for(200ms); // 模拟繁重计算
        log::info("[Worker] done: {}", *task_data);
    }

    log::info("[Worker] pipe closed, all pending tasks drained, exiting.");
}

// 生产者：模拟网络 I/O 线程
auto net_task(async::ChannelSender<std::string> tx) -> async::Task<>
{
    log::info("[Net] started, receiving frontend requests...");

    for (int i = 1; i <= 6; ++i) {
        std::string data = "Request-" + std::to_string(i);
        log::info("[Net] received {}, forwarding to worker...", data);

        // 管道（容量 4）满时 send 挂起当前协程，但 Net 线程的 io_uring 继续运行。
        auto result = co_await tx.send(data);
        if (!result) break;

        co_await async::sleep_for(50ms); // 模拟高频收包（生产 > 消费）
    }

    log::info("[Net] all requests dispatched, closing sender.");
    tx.close(); // 触发 Worker 的优雅停机
}

int main()
{
    async::IOContext ctx_net;    // Thread A: 网络 IO 上下文
    async::IOContext ctx_worker; // Thread B: 密集计算上下文

    // 容量为 4 的跨线程有界管道（自带背压）
    auto [tx, rx] = async::make_channel<std::string>(4, ctx_net, ctx_worker);

    // 将 Worker 协程部署到 ctx_worker，由独立的 OS 线程驱动
    async::co_spawn(worker_task(std::move(rx)), ctx_worker);
    std::jthread worker_thread([&ctx_worker] { ctx_worker.run(); });

    // 将 Net 协程部署到 ctx_net，在主线程驱动
    async::co_spawn(net_task(std::move(tx)), ctx_net);
    ctx_net.run();
    // ctx_net.run() 返回后，jthread RAII 自动 join，等待 Worker 退出
    return 0;
}
```

要点：

- 函数参数**按值持有** `ChannelSender`/`ChannelReceiver`，配合 `co_spawn` 时 `std::move` 传入，生命周期与协程共存亡，无需 lambda 捕获。
- `while (auto v = co_await rx.receive())` 是工业级 drain 惯用法：发送方 `close()` 后，消费者把缓冲内所有积压数据读完，循环才退出，无需手动捕获 `ChannelError::Closed`。
- 生产 50ms / 消费 200ms，管道容量 4：Net 迅速填满管道后自动背压挂起，而 Net 线程的 io_uring 仍在高效运行。

对应可执行示例：`tutorial/16_channel_pipe/main.cpp`  
运行命令：`./build/tutorial/16_channel_pipe/tutorial.16_channel_pipe`

一次真实运行输出（2026-05-07）：

```text
[2026-05-07 16:10:53.821] [116956] [info] === Demo: cross-thread pipeline with backpressure ===
[2026-05-07 16:10:53.821] [116956] [info] [Net] started, receiving frontend requests...
[2026-05-07 16:10:53.822] [116956] [info] [Net] received Request-1, forwarding to worker...
[2026-05-07 16:10:53.822] [116957] [info] [Worker] started, waiting for requests...
[2026-05-07 16:10:53.822] [116957] [info] [Worker] processing: Request-1
[2026-05-07 16:10:53.872] [116956] [info] [Net] received Request-2, forwarding to worker...
[2026-05-07 16:10:53.922] [116956] [info] [Net] received Request-3, forwarding to worker...
[2026-05-07 16:10:53.972] [116956] [info] [Net] received Request-4, forwarding to worker...
[2026-05-07 16:10:54.022] [116957] [info] [Worker] done: Request-1
[2026-05-07 16:10:54.022] [116957] [info] [Worker] processing: Request-2
[2026-05-07 16:10:54.022] [116956] [info] [Net] received Request-5, forwarding to worker...
[2026-05-07 16:10:54.072] [116956] [info] [Net] received Request-6, forwarding to worker...
[2026-05-07 16:10:54.122] [116956] [info] [Net] all requests dispatched, closing sender.
[2026-05-07 16:10:54.222] [116957] [info] [Worker] done: Request-2
[2026-05-07 16:10:54.222] [116957] [info] [Worker] processing: Request-3
[2026-05-07 16:10:54.422] [116957] [info] [Worker] done: Request-3
[2026-05-07 16:10:54.422] [116957] [info] [Worker] processing: Request-4
[2026-05-07 16:10:54.622] [116957] [info] [Worker] done: Request-4
[2026-05-07 16:10:54.622] [116957] [info] [Worker] processing: Request-5
[2026-05-07 16:10:54.822] [116957] [info] [Worker] done: Request-5
[2026-05-07 16:10:54.822] [116957] [info] [Worker] processing: Request-6
[2026-05-07 16:10:55.023] [116957] [info] [Worker] done: Request-6
[2026-05-07 16:10:55.023] [116957] [info] [Worker] pipe closed, all pending tasks drained, exiting.
```

日志中可以看到：Net 在 ~200ms 内连发 4 条（Request-1 到 4），在 Worker 消化第一条之前管道已满，发送第 5 条时 Net 协程挂起；Worker 每消化一条，Net 立即补入下一条。这就是背压在日志层面的物理体现。

---

## 与共享状态 + 锁的对比

| 维度 | 消息传递（`Channel` / `ChannelPipe`） | 共享状态 + 锁 |
|---|---|---|
| 数据流向 | 明确（单向流） | 模糊（任意读写） |
| 背压 | 原生支持 | 需要手写 |
| 取消与超时组合 | `Channel<T>` 可直接组合 `timeout/when_any`；`ChannelPipe<T>` 以关闭/协议边界控制为主 | 需要额外封装 |
| 调试复杂度 | 关注队列和生命周期 | 关注锁顺序与竞争 |

当你的问题本质是“把数据从 A 交给 B”，优先选消息传递。

---

## 本章小结

- `Channel<T>` 用在同一 IOContext 内，低开销、语义直接。
- `ChannelPipe<T>` 用在跨 IOContext/跨线程，支持背压与有界流控。
- `Channel<T>` 可直接组合 `timeout`；`ChannelPipe<T>` 在本实现中先以关闭与协议边界进行收敛。
- `ThreadSafeChannel<T>` 当前不在教程主线。

下一节：第 5 部分进入零开销抽象与性能边界。
