# 3.1 外部取消模型：stop_then

> **前置知识**：本章假设你已读完 [1.4（co_spawn 与生命周期）](01_co_spawn.md)，理解 `co_spawn`、`std::expected` 与 `operation_canceled` 错误码的含义。
> **源文件**：[tutorial/09_stop_then/main.cpp](../../tutorial/09_stop_then/main.cpp)
> **下一节**：[3.2 局部时间约束：timeout](06_timeout.md)

---

## 问题：如何中断一个已挂起的 I/O 操作

前面各章的异步操作都有确定的完成时间——定时器到期、连接建立、数据到达。但有两类场景无法适配这个模型：

1. **用户取消**：客户端中途放弃了某个耗时操作（如大文件下载），后端需要立即终止挂起的网络读取。
2. **进程停机**：运维下发 SIGTERM，accept 循环挂起在等待新连接，需要干净地退出 `while(true)`，而不是 `kill -9`。

这两个场景的共同特征是：**触发时刻不可预测，触发源在 I/O 操作之外**。`stop_then` 将任意 `cancelable_operation` 与一个 `std::stop_token` 绑定，一旦 token 收到停止请求，底层 io_uring 操作立即被取消，协程以 `operation_canceled` 恢复。

---

## 完整代码

```cpp
#include <cstdlib>
#include <stop_token>

#include <blog.h>

using namespace std::chrono_literals;

namespace {

auto demo_user_cancellation() -> async::Task<>
{
    log::info("=== Demo 1: 纯异步协作式取消 (告别 OS 线程) ===");

    std::stop_source stop_src;

    // 伴随协程：300ms 后触发停止信号，模拟用户点击"取消"
    async::co_spawn([](std::stop_source src) -> async::Task<> {
        co_await async::sleep_for(300ms);
        log::warning("[UI] 收到取消指令，触发 StopToken。");
        src.request_stop();                                 // <-- 触发取消
    }(stop_src));

    log::info("[Downloader] 开始下载大文件 (预计需要 10 秒)...");

    auto result = co_await async::stop_then(
        async::sleep_for(10s),       // <-- 被监控的操作
        stop_src.get_token()         // <-- 绑定取消信号源
    );

    if (!result && result.error() == std::errc::operation_canceled)
        log::warning("[Downloader] 下载被安全中断，底层 io_uring 已回收挂起操作。\n");
    else
        log::error("[Downloader] 未预期的结果。");
}

auto demo_server_loop() -> async::Task<>
{
    log::info("=== Demo 2: 核心循环的优雅停机 (Graceful Shutdown) ===");

    std::stop_source stop_src;

    // 伴随协程：800ms 后触发停机指令，模拟运维下发 SIGTERM
    async::co_spawn([](std::stop_source src) -> async::Task<> {
        co_await async::sleep_for(800ms);
        log::warning("[Admin] 下发平滑停机指令。");
        src.request_stop();
    }(stop_src));

    log::info("[Server] 开始监听新连接 (死循环)...");

    int conn_count = 0;
    while (true) {
        auto result = co_await async::stop_then(
            async::sleep_for(200ms),     // <-- 模拟 accept(2) 挂起
            stop_src.get_token()
        );

        if (!result) {
            if (result.error() == std::errc::operation_canceled) {
                log::info("[Server] 收到停机指令，阻塞等待已被打断。");
                log::info("[Server] 拒绝新请求并等待旧请求排空...");
                break;                   // <-- 干净退出循环
            }
            log::error("[Server] 等待连接时出现错误: {}", result.error());
            break;
        }

        log::info("[Server] 成功处理了第 {} 个客户端连接...", ++conn_count);
    }

    log::info("[Server] 资源清理完毕，进程即将退出。");
}

} // namespace

int main()
{
    async::run(demo_user_cancellation);
    async::run(demo_server_loop);
    return EXIT_SUCCESS;
}
```

```bash
./build/tutorial/09_stop_then/tutorial.09_stop_then
```

```text
[info] === Demo 1: 纯异步协作式取消 (告别 OS 线程) ===
[info] [Downloader] 开始下载大文件 (预计需要 10 秒)...
[warning] [UI] 收到取消指令，触发 StopToken。
[warning] [Downloader] 下载被安全中断，底层 io_uring 已回收挂起操作。

[info] === Demo 2: 核心循环的优雅停机 (Graceful Shutdown) ===
[info] [Server] 开始监听新连接 (死循环)...
[info] [Server] 成功处理了第 1 个客户端连接...
[info] [Server] 成功处理了第 2 个客户端连接...
[info] [Server] 成功处理了第 3 个客户端连接...
[warning] [Admin] 下发平滑停机指令。
[info] [Server] 收到停机指令，阻塞等待已被打断。
[info] [Server] 拒绝新请求并等待旧请求排空...
[info] [Server] 资源清理完毕，进程即将退出。
```

---

## 逐步解析

### `async::stop_then(op, token)`

```cpp
auto result = co_await async::stop_then(
    async::sleep_for(10s),
    stop_src.get_token()
);
```

`stop_then` 接受两个参数：被监控的 `cancelable_operation`（`sleep_for` 是其中一种）和一个 `std::stop_token`。协程挂起后，运行时同时监听 I/O 完成事件和 stop token 的触发状态：

- **I/O 先完成**：正常返回操作结果，stop token 监听器被清除。
- **stop token 先触发**：向 io_uring 提交 `IORING_OP_ASYNC_CANCEL`，取消对应的 SQE；协程以 `std::errc::operation_canceled` 恢复。

`result` 的类型与被监控操作的返回类型相同（`sleep_for` 返回 `std::expected<void, std::error_code>`，因此 `result` 也是 `expected<void, error_code>`）。取消路径和正常路径共用同一个类型，调用方通过错误码区分两种情况。

### 伴随协程（companion coroutine）

```cpp
async::co_spawn([](std::stop_source src) -> async::Task<> {
    co_await async::sleep_for(300ms);
    src.request_stop();                 // <-- 触发取消
}(stop_src));
```

这里用 `co_spawn` 在同一个 IOContext 内派生一个协程，而不是 `std::jthread`。两者在功能上等价（延时后触发一个信号），但 `std::jthread` 需要分配 OS 线程栈（通常 8 MiB）并经过内核调度；协程帧仅消耗若干字节堆内存，调度发生在用户态事件循环内部。

> **Note**：`std::stop_source` 的 `request_stop()` 是线程安全的，可以在任何线程调用。在单 IOContext 模型中，伴随协程与被监控协程运行在同一线程，但代码不应依赖这一点。

### `operation_canceled` 的确定性语义

```cpp
if (!result && result.error() == std::errc::operation_canceled) {
    // 此处可以安全清理业务状态
}
```

收到 `operation_canceled` 时，io_uring 层面对应的 SQE 已被内核取消，不会产生延迟的 CQE。资源释放是**确定性的**：协程帧在 `co_return` 后立即销毁，没有悬挂的内核操作。

### 循环中的退出模式

场景 2 的核心结构：

```cpp
while (true) {
    auto result = co_await async::stop_then(accept_op, token);
    if (!result) {
        if (result.error() == std::errc::operation_canceled)
            break;          // 干净退出
        break;
    }
    // 正常处理连接
}
```

`stop_then` 使得这个模式无需额外的原子标志或条件变量：stop token 触发后，下一次 `co_await` 立即以 `operation_canceled` 返回，控制流自然进入退出分支。已 `co_spawn` 出去的会话协程不受影响，继续运行直到完成。

---

## `stop_then` 与 `timeout` 的边界

| | `stop_then` | `timeout` |
|---|---|---|
| 触发源 | 外部 `std::stop_token` | 内部固定时间间隔 |
| 触发时刻 | 不可预测（由外部控制） | 确定（`duration` 到期） |
| 错误码 | `operation_canceled` | `timed_out` |
| 典型场景 | 用户取消、进程停机信号 | SLA 熔断、读写超时防护 |

---

## 本章小结

`stop_then` 将一个 `cancelable_operation` 与外部停止信号绑定：token 触发时，io_uring 操作被取消，协程以 `operation_canceled` 恢复。伴随协程通过 `co_spawn` 实现延时触发，不引入 OS 线程。

> **下一节**：[3.2 局部时间约束：timeout](06_timeout.md) — 给任意异步操作加上时间上限，超时返回 `timed_out`。
