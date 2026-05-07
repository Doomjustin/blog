# 6.1 线程模型与局部性：多线程部署策略

> **前置知识**：本章假设你已读完第 4 部分（并发编排与资源收敛），尤其是 `ChannelPipe<T>` 的 owner-thread 与 backpressure 设计。
> **源文件**：[tutorial/20_threading/main.cpp](../../tutorial/20_threading/main.cpp)
> **可执行文件**：`./build/tutorial/20_threading/tutorial.20_threading`
> **下一节**：附录

---

## 当前示例对应的两种线程模式

[tutorial/20_threading/main.cpp](../../tutorial/20_threading/main.cpp) 演示了两条非常具体的路径：

1. **对称并发**：`async::run(4, ...)` 拉起 4 个 worker 并行执行。
2. **跨线程迁移**：通过 `shift_to` 在 net/worker 两个 IOContext 之间切换协程执行权。

关键代码：

```cpp
async::run(4, [&]() -> async::Task<> {
    int id = worker_id_allocator.fetch_add(1);
    co_await demo_symmetric_workers(id);
});
```

```cpp
auto& net_ctx = async::this_coroutine::context();
log::info("[Net] 收到用户请求，当前线程: {}", std::this_thread::get_id());

co_await async::shift_to(worker_ctx);   // 迁移到 Worker 线程
co_await async::sleep_for(200ms);       // 模拟重计算

co_await async::shift_to(net_ctx);      // 迁回 Net 线程
log::info("[Net] 渲染完毕，准备下发，当前线程: {}", std::this_thread::get_id());

// 通知 worker_ctx 可以退出
async::post(worker_ctx, [&worker_ctx] { worker_ctx.drop_work(); });
```

运行命令：

```bash
./build/tutorial/20_threading/tutorial.20_threading
```

实测输出（本次会话）：

```text
[info] === Demo 1: 对称多核架构 (async::run 启动 4 线程) ===
[info] [Worker 1] 启动于物理线程 131431638496960
[info] [Worker 2] 启动于物理线程 131431630104256
[info] [Worker 3] 启动于物理线程 131431645808576
[info] [Worker 4] 启动于物理线程 131431621711552
[info] [Worker 1] 执行完毕，无锁累加结果: 3
[info] [Worker 2] 执行完毕，无锁累加结果: 3
[info] [Worker 3] 执行完毕，无锁累加结果: 3
[info] [Worker 4] 执行完毕，无锁累加结果: 3
[info] ------------------------------------------------------
[info] === Demo 2: 跨线程调度 (shift_to) ===
[info] [Net] 收到用户请求，准备解析，当前线程: 131431645808576
[info] [Worker] 执行重度 CPU 渲染任务，当前线程: 131431621711552
[info] [Net] 渲染完毕，通过网卡下发给客户端，当前线程: 131431645808576
```

---

## 如何理解这个示例

- 场景 1 强调“同一协程在所属线程内运行”，局部状态无需跨线程同步。
- 场景 2 强调“重任务卸载”：网络线程收包，计算线程做重活，再切回网络线程收尾。

这就是线程局部性在业务控制流里的最小落地形态。

---

## 架构边界

- `shift_to` 有上下文切换成本，不适合把很小的任务频繁来回切。
- 线程数不应盲目增加，应结合 CPU 核数和 workload 做压测调优。
- 多线程目标通常是吞吐提升，不保证 tail latency 一定下降。

---

## 本章小结

6.2 的实用结论是：

- 用 `async::run(N, ...)` 做对称扩展。
- 用 `shift_to` 做异构分工（net vs worker）。
- 用压测数据决定 N 和迁移策略，而不是凭经验拍脑袋。

> **结束**：[返回大纲](README.md)
