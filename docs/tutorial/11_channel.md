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

现实类比：接入线程收包，工作线程做 CPU 密集计算。

```cpp
#include <blog.h>

auto sender(async::ChannelSender<int> tx) -> async::Task<>
{
    co_await tx.send(42);
    log::info("net send: 42");
    tx.close();
    log::info("net close sender");
}

auto receiver(async::ChannelReceiver<int> rx) -> async::Task<>
{
    auto v = co_await rx.receive();
    if (v) log::info("worker recv: {}", *v);
    log::info("worker done");
}

int main()
{
    async::IOContext ctx_net;
    async::IOContext ctx_worker;
    auto [tx, rx] = async::make_channel<int>(16, ctx_net, ctx_worker);

    async::co_spawn(receiver(std::move(rx)), ctx_worker);
    std::jthread worker_thread([&] { ctx_worker.run(); });

    async::co_spawn(sender(std::move(tx)), ctx_net);
    ctx_net.run();
    return 0;
}
```

要点：

- `ChannelPipe<T>` 是跨 IOContext 的 SPSC 模型，依靠 credit/backpressure 控制发送节奏。
- 当 receiver 侧处理慢时，sender 会自然背压而不是无限堆积。
- 该实现当前不直接满足 `timeout(rx.receive(), dur)` 的约束；教程主线先聚焦跨线程消息传递语义。

对应可执行示例：`tutorial/16_channel_pipe/main.cpp`  
运行命令：`./build/tutorial/16_channel_pipe/tutorial.16_channel_pipe`

一次真实运行输出（2026-05-07）：

```text
[2026-05-07 14:01:35.518] [77524] [info] === demo 1: channel_pipe cross-thread pipeline ===
[2026-05-07 14:01:35.518] [77524] [info] net send: 42
[2026-05-07 14:01:35.518] [77524] [info] net close sender
[2026-05-07 14:01:35.518] [77525] [info] worker recv: 42
[2026-05-07 14:01:35.518] [77525] [info] worker done
```

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
