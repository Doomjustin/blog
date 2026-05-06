# 10. 可取消的等待（stop_then）

> **源文件**：[examples/stop_then_sleep/main.cpp](../../examples/stop_then_sleep/main.cpp)

协程挂起在 `co_await` 时，对外部世界是"不可见"的。下面这段代码：

```cpp
co_await async::sleep_for(3s);
```

一旦挂起，就只能等 3 秒后内核定时器到期才会继续执行。即便此时用户按了 Ctrl+C、上层设置了 deadline、或服务需要关闭，协程也无法提前退出。

`async::stop_then` 解决这个问题：给任意 awaitable 附加一个 `std::stop_token`，当 token 被触发时立即打断正在等待的操作。

## 基本用法

```cpp
auto result = co_await async::stop_then(async::sleep_for(3s), stop_source.get_token());
if (!result) {
    if (result.error() == std::errc::operation_canceled)
        log::info("sleep cancelled by stop token");
    else
        log::error("stop_then sleep failed: {}", result.error());
}
else {
    log::info("sleep completed without cancellation");
}
```

关键点：

1. **`async::stop_then(op, token)`** — 包装任意 awaitable，附加取消能力
2. **返回值提升为 `std::expected`** — 原本返回 `void` 的操作（如 `sleep_for`）变为 `std::expected<void, std::error_code>`；正常完成时 `operator bool` 为 `true`，取消时为 `false`，`error()` 为 `operation_canceled`
3. **`operation_canceled` 是预期路径** — 它不是故障，应用 `log::info` 而非 `log::error`；其他错误才是真正的 I/O 问题

## 从另一个线程触发取消

`stop_token` 通常由另一个线程（信号处理线程、监控线程）触发，而 io_uring 不是线程安全的，不能从外部线程直接操作。`stop_then` 内部通过 `async::dispatch` 将取消操作安全地投递回 io_uring 所在的线程：

```cpp
std::stop_source stop_source;

std::jthread worker{ [&ctx, source = stop_source]() mutable {
    std::this_thread::sleep_for(200ms);

    // async::dispatch：将函数投递到 ctx 所在线程执行
    // 若已在目标线程则立即执行，否则通过 eventfd 唤醒目标线程
    async::dispatch(ctx, [] { log::info("dispatch from worker thread"); });

    source.request_stop();   // 触发取消
} };

auto result = co_await async::stop_then(async::sleep_for(3s), stop_source.get_token());
```

`async::dispatch` 与 `async::post` 的区别：

| | `dispatch` | `post` |
|---|---|---|
| 在目标线程上调用时 | 立即执行（同步） | 入队，下次事件循环迭代时执行 |
| 在其他线程调用时 | 通过 eventfd 唤醒，入队执行 | 同上 |

`stop_then` 内部用 `dispatch`（而非 `post`）是因为：当 stop callback 恰好在 io_uring 线程上触发时，`dispatch` 可以立即提交 cancel SQE，不需要多一次事件循环往返。

## 取消前已触发的情况

如果 token 在 `co_await` 到达之前就已经被触发，`stop_then` 会跳过提交操作，直接返回 `operation_canceled`：

```cpp
stop_source.request_stop();   // 已触发

// stop_then 检测到 token 已停止，不向 io_uring 提交任何 SQE
// 协程不挂起，直接在 await_resume() 里返回 operation_canceled
auto result = co_await async::stop_then(async::sleep_for(3s), stop_source.get_token());
// result.error() == errc::operation_canceled
```

这让循环顶部的 stop 检查不再必要——`stop_then` 自己处理了这种情况。

## 运行

```bash
example.stop_then_sleep
```

实际输出：

```
[2026-05-06 01:16:12.152] [180651] [info] waiting on cancellable sleep
[2026-05-06 01:16:12.354] [180651] [info] dispatch from worker thread
[2026-05-06 01:16:12.354] [180651] [info] sleep cancelled by stop token
```

时间线：
- **0ms**：协程进入 `co_await async::stop_then(sleep_for(3s), ...)`，挂起
- **200ms**：worker 线程醒来，通过 `dispatch` 打印一条 log，然后调用 `request_stop()`
- **200ms**：stop callback 触发，`dispatch` 将 cancel SQE 投递给 io_uring 线程
- **200ms**：io_uring 取消定时器，协程以 `operation_canceled` 恢复

程序在约 200ms 后退出，而不是等满 3 秒。

## 下一步

`stop_then` 不仅适用于 `sleep_for`，可以包装任意网络 I/O。下一篇展示如何在请求循环中对每个 I/O 步骤单独附加取消：[逐操作取消](11_stop_then_request.md)。
