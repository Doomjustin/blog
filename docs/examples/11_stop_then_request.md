# 11. 逐操作取消（stop_then + 请求循环）

> **源文件**：[examples/stop_then_request/main.cpp](../../examples/stop_then_request/main.cpp)

上一篇用 `stop_then` 打断了一个 `sleep_for`。本篇把同样的技术用在真实的请求循环中：client 持续向 server 发请求，外部信号触发后立即停止，不等当前 RPC 完成。

## 示例结构

```
demo()
├── jthread(timer)                    — 1500ms 后调用 request_stop()
├── co_spawn(echo_server(port))       — 后台 echo 服务，每次响应前人为延迟 700ms
└── co_await client(stop, port)
      └── 循环发请求:
            └── one_request(sock, msg, stop)
                  ├── stop_then(async_send_some, stop)
                  └── stop_then(async_receive_some, stop)  ← 最可能的取消点
```

服务端故意加入 700ms 延迟，是为了让 client 在 receive 步骤挂起时，stop 信号（1500ms）能触发取消——如果服务端立即响应，client 在 stop 前就能完成好几次 RPC，取消效果不明显。

## 只在循环顶部检查 stop 有什么问题

最直觉的做法：

```cpp
for (int i = 0; ; ++i) {
    if (stop.stop_requested()) co_return;   // 只在这里检查
    co_await one_request(sock, msg);        // 内部没有取消能力
}
```

问题：协程一旦进入 `one_request` 就"消失"了——它挂起在 `async_receive_some` 等待服务端回复，而服务端需要 700ms。即便 stop 在 1ms 后触发，协程也只能等 700ms 后 RPC 完成，才能在下次循环顶部感知到取消。

`stop_then` 的做法是**在每个 I/O awaitable 上单独注册取消**，让取消信号在最近的内核交互点生效：

## 每步 I/O 单独包装

```cpp
auto one_request(net::ip::tcp::socket& sock,
                 std::string_view message,
                 std::stop_token stop)
    -> async::Task<std::expected<std::string, std::error_code>>
{
    auto payload = async::buffer(message);

    // step 1 — send 被包装：若 stop 在等待内核接受数据时触发，立即取消
    auto sent = co_await async::stop_then(sock.async_send_some(payload), stop);
    if (!sent)
        co_return std::unexpected(sent.error());

    // step 2 — receive 被包装：这是最常见的挂起点，服务端延迟 700ms
    std::array<std::byte, 256> buf{};
    auto recv = co_await async::stop_then(sock.async_receive_some(buf), stop);
    if (!recv)
        co_return std::unexpected(recv.error());

    co_return std::string{ reinterpret_cast<const char*>(buf.data()), *recv };
}
```

关键点：

1. **每步单独包装** — stop 信号到达时，无论协程挂在 send 还是 receive，都会立即响应，而不是等整个 RPC 完成
2. **错误向上传播** — `one_request` 统一返回 `std::expected`，调用方在一处处理所有错误，不需要在每一步内部重复判断

## 请求循环中处理取消

```cpp
for (int i = 0; ; ++i) {
    auto result = co_await one_request(sock, msg, stop);

    if (!result) {
        const auto& ec = result.error();
        if (ec == std::errc::operation_canceled)
            log::info("[client] request #{} cancelled — exiting cleanly", i);
        else
            log::error("[client] request #{} failed: {}", i, ec);
        co_return;
    }

    log::info("[client] echo: {}", *result);
    co_await async::sleep_for(std::chrono::milliseconds{ 50 });
}
```

关键点：

1. **`operation_canceled` 是正常退出路径** — 用 `log::info`，不是 `log::error`；这是预期行为，不是故障
2. **其他错误同样退出** — 真正的网络故障（`connection_reset` 等）也没有重试价值，直接退出（与下一篇对比：如果要重试，逻辑会更复杂）

## 运行

```bash
./build/examples/stop_then_request/example.stop_then_request
```

一次实测输出（2026-05-06）：

```
[2026-05-06 01:18:40.516] [181238] [info] [client] connected to :40613
[2026-05-06 01:18:41.216] [181238] [info] [client] echo: request-0
[2026-05-06 01:18:41.966] [181238] [info] [client] echo: request-1
[2026-05-06 01:18:42.016] [181239] [info] [timer] requesting stop
[2026-05-06 01:18:42.017] [181238] [info] [client] request #2 cancelled — exiting cleanly
[2026-05-06 01:18:42.017] [181238] [info] [demo] completed cleanly
```

时间线：
- **0ms**：client 连接，request-0 发出
- **750ms**（约）：request-0、1 依次完成（每次约 750ms = 700ms 处理 + 50ms 间隔）
- **1500ms**：timer 触发 `request_stop()`
- **1500ms**：client 正阻塞在 request-2 的 `async_receive_some`，stop_then 立即取消
- **1500ms**：`one_request` 返回 `operation_canceled`，client 打印后 `co_return`
- **1500ms+**：日志显示 `demo()` 返回，但进程未自动退出（server 仍在 `async_accept` 等待）

取消响应时间 ≈ 0ms（相比只在循环顶部检查最多延迟 700ms）。

## 下一步

如果需要在取消之外还支持**重试**（服务端短暂超时、网络抖动），可以在每步操作上组合 `timeout` 和重试循环，同时保持 `stop_token` 的最高优先级：[带重试的韧性客户端](12_stop_then_resilient.md)。
