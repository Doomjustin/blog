# Concurrent tasks

> **源文件**：[examples/concurrent_tasks/main.cpp](../../examples/concurrent_tasks/main.cpp)

协程的强大之处在于可以在单个线程上同时运行多个任务。`async::co_spawn` 允许你启动独立的协程任务，它们在事件循环中并发执行，无需创建线程。

## 并发运行多个独立任务

```cpp
auto task(int id, std::chrono::milliseconds delay) -> async::Task<>
{
    log::info("task {} started", id);
    co_await async::sleep_for(delay);
    log::info("task {} done after {}ms", id, delay.count());
}

auto run_all() -> async::Task<>
{
    using namespace std::chrono_literals;

    async::co_spawn(task(1, 300ms));
    async::co_spawn(task(2, 100ms));
    async::co_spawn(task(3, 200ms));

    log::info("all tasks spawned, main coroutine exiting");
    co_return;
}
```

关键点：

1. **`async::co_spawn()`** — 启动一个分离的协程任务（不需要等待）
2. **主协程立即返回** — 不阻塞，三个 task 在后台独立运行
3. **事件循环管理生命周期** — 当所有任务完成后自动退出

## 执行时序

```
[17:33:50.430] task 1 started     ┐
[17:33:50.430] task 2 started     ├─ 同时启动，main 随即退出
[17:33:50.430] task 3 started     │
[17:33:50.430] all tasks spawned
[17:33:50.530] task 2 done        ← 100ms 先完成
[17:33:50.630] task 3 done        ← 200ms 后完成
[17:33:50.730] task 1 done        ← 300ms 最后完成
```

三个任务完全**并发运行**，没有线程同步、没有互斥锁 — 只有一个线程和事件循环。

## 与线程的区别

| 特性 | `async::co_spawn` | `std::thread` |
|------|-------------------|--------------|
| 开销 | 极低（只是 coroutine frame） | 高（完整线程栈） |
| 上下文切换 | 由应用程序控制（yield 点） | 由操作系统调度 |
| 同步机制 | lock-free（单线程）| 需要互斥锁、信号量 |
| 可生成数量 | 数千/数百万 | 几百～数千 |

## 下一步

现在我们可以并发运行多个任务。下一步是在网络服务中使用超时机制控制空闲连接：[timeout_echo server](06_timeout_echo_server.md)。
