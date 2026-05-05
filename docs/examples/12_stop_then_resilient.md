# 12. 带重试与超时的韧性客户端（stop_then + timeout）

> **源文件**：[examples/stop_then_resilient/main.cpp](../../examples/stop_then_resilient/main.cpp)

上一篇的 client 遇到任何错误都直接退出。真实场景中，服务端可能短暂繁忙或网络有抖动——这时应该重试，而不是立即放弃。但重试不能是无限的，外部取消信号（`stop_token`）必须能立刻终止整个重试链。

本篇把三件事组合在一起：

| 问题 | 手段 |
|---|---|
| 服务端响应慢 | `async::timeout`：单次 receive 超过 250ms 就超时重试 |
| 瞬时错误 | 重试循环，最多 3 次，退避间隔递增 |
| 外部取消 | `stop_token` 优先级最高，立即终止整个重试链 |

## 示例结构

```
demo()
├── jthread(timer)                         — 900ms 后调用 request_stop()
├── co_spawn(server(port, stop))           — accept 循环；收到 stop 后退出
└── co_await client(port, stop)
      └── for each job in [fast-a, slow-b, fast-c, slow-d, ...]:
            └── one_rpc(endpoint, job, stop)
                  ├── send_with_policy(sock, text, stop)
                  │     └── stop_then(async_send_some, stop)  ← 可取消的 send
                  └── recv_with_policy(sock, buf, stop)
                        └── timeout(async_receive_some, 250ms) + 手动 stop 检查
```

服务端对 "slow" 请求延迟 700ms，对其他请求延迟 80ms，制造两类 job：在 stop 前能完成的（fast-a、slow-b、fast-c）和会被取消的（slow-d）。

## 带重试的 send

```cpp
auto send_with_policy(net::ip::tcp::socket& sock,
                      std::string_view text,
                      std::stop_token stop)
    -> async::Task<std::expected<std::size_t, std::error_code>>
{
    auto bytes = async::buffer(text);

    for (int attempt = 1; attempt <= 3; ++attempt) {
        auto sent = co_await async::stop_then(sock.async_send_some(bytes), stop);
        if (sent)
            co_return sent;                                    // 成功

        if (sent.error() == std::errc::operation_canceled)
            co_return std::unexpected(sent.error());           // stop 触发，不重试

        if (attempt < 3)
            co_await async::sleep_for(std::chrono::milliseconds{ 50 * attempt });
    }
    co_return std::unexpected(std::make_error_code(std::errc::io_error));
}
```

关键点：

1. **无需手动预检 stop** — `stop_then` 的 `await_ready()` 内部已检查 token；token 已触发时直接返回 `operation_canceled`，不提交任何 io_uring SQE，不消耗任何重试次数
2. **`operation_canceled` 不参与重试** — stop 触发就是"放弃"的信号，立即向上传播；`io_error` 等瞬时错误才需要重试

## 带超时和重试的 receive

```cpp
auto recv_with_policy(net::ip::tcp::socket& sock,
                      std::span<std::byte> buffer,
                      std::stop_token stop)
    -> async::Task<std::expected<std::size_t, std::error_code>>
{
    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (stop.stop_requested())                             // 手动预检
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));

        auto recv = co_await async::timeout(sock.async_receive_some(buffer), 250ms);
        if (recv)
            co_return recv;                                    // 成功

        if (recv.error() != std::errc::timed_out)
            co_return std::unexpected(recv.error());           // 非超时错误不重试

        if (attempt < 3)
            co_await async::sleep_for(std::chrono::milliseconds{ 50 * attempt });
    }
    co_return std::unexpected(std::make_error_code(std::errc::timed_out));
}
```

关键点：

1. **`async::timeout(op, duration)`** — 固定时限，与 `stop_token` 完全无关；超时返回 `timed_out`，可以重试
2. **`timed_out` 可重试，其他错误不可** — 超时可能只是服务端短暂繁忙；`operation_canceled`、`connection_reset` 等应立即放弃
3. **这里需要手动预检** — `timeout` 不感知 stop_token，不会自动取消；每次超时重试前必须显式检查。这里选择用 `timeout` 而非 `stop_then` 是因为：receive 需要一个固定的超时上限，如果只用 `stop_then` 则在 stop 触发前永远等待

## 关于 async::retry

库中存在 `async::retry` API：

```cpp
co_await async::retry(3, async::retry_strategy::fixed(50ms), [&]{
    return sock.async_send_some(bytes);
});
```

`async::retry` 仍然是有价值的便捷 API，尤其适合"同构错误 + 固定策略"的场景。

本例选择手写循环，不是因为 `async::retry` 不好，而是因为这里要同时表达三类策略：

1. `timed_out` 可重试，`operation_canceled` 必须立即退出
2. send 与 receive 使用不同包装（`stop_then` vs `timeout`）
3. 每轮重试前后都要保留 stop 检查与退避等待

这类"混合策略"下，手写循环通常更直观；若你的场景是统一错误策略，优先考虑 `async::retry`。

## 服务端也需要感知 stop

```cpp
// demo() 里：同一个 stop token 同时传给 server 和 client
async::co_spawn(server(server_port, shutdown.get_token()));
co_await client(server_port, shutdown.get_token());
```

```cpp
auto server(std::uint16_t& out_port, std::stop_token stop) -> async::Task<>
{
    while (true) {
        auto client = co_await async::stop_then(acceptor.async_accept(), stop);
        if (!client) {
            if (client.error() == std::errc::operation_canceled)
                co_return;   // 正常退出
            log::error("[server] accept failed: {}", client.error());
            continue;
        }
        async::co_spawn(session(std::move(*client)));
    }
}
```

`server` 以 `co_spawn` 在后台运行，事件循环会等待**所有 task** 完成才退出。如果 server 的 `while (true)` 不感知 stop，client 退出后 `demo()` 返回，进程却永远挂着——server 仍在 `async_accept` 里等待新连接。

解决方法：同一个 stop token 传给 server，`acceptor.async_accept()` 收到 stop 后返回 `operation_canceled`，server 检测到后 `co_return`，事件循环中的所有 task 都结束，进程退出。

## 运行

```bash
./build/examples/stop_then_resilient/example.stop_then_resilient
```

一次实测输出（2026-05-06）：

```
[2026-05-06 01:18:01.313] [181089] [info] [client] job=fast-a ok -> ack:fast-a
[2026-05-06 01:18:02.014] [181089] [info] [client] job=slow-b ok -> ack:slow-b
[2026-05-06 01:18:02.094] [181089] [info] [client] job=fast-c ok -> ack:fast-c
[2026-05-06 01:18:02.133] [181090] [info] [timer] request_stop
[2026-05-06 01:18:02.397] [181089] [error] [client] job=slow-d failed: operation_canceled (generic:125)
[2026-05-06 01:18:02.397] [181089] [info] [demo] finished
```

时间线（约）：

```
  0ms  fast-a 发出（服务端 80ms 处理）
 80ms  fast-a 完成；slow-b 发出（服务端 700ms 处理）
780ms  slow-b 完成；fast-c 发出（服务端 80ms 处理）
860ms  fast-c 完成；slow-d 发出（服务端 700ms 处理）
900ms  timer 触发 request_stop()
900ms  slow-d 的 recv_with_policy 在循环顶部检查到 stop → operation_canceled
900ms  client co_return；demo() 返回；server 的 async_accept 被取消 → server co_return
       事件循环结束，进程退出
```

注意 slow-d 用的是 `log::error`——从 client 的角度看，这是一个未完成的 job，记录为 error 是合理的，尽管原因是外部取消而非真正的故障。
