# Timeout echo server

> **源文件**：[examples/timeout_echo/timeout_echo_server.cpp](../../examples/timeout_echo/timeout_echo_server.cpp)

在生产环境中，长连接的空闲检测至关重要。这个示例展示如何用 `async::timeout` 为 I/O 操作添加超时控制，在客户端空闲超过指定时间后自动断开。

## 添加空闲超时

```cpp
constexpr auto idle_timeout = 5s;

auto session(net::ip::tcp::socket client, net::ip::tcp::endpoint peer) -> async::Task<>
{
    log::info("Client connected: {}", peer);

    std::array<std::byte, 4096> buffer{};

    while (true) {
        auto read_result = co_await async::timeout(
            client.async_receive_some(buffer),
            idle_timeout
        );

        if (!read_result) {
            if (read_result.error() == std::errc::timed_out)
                log::info("Client {} idle timeout, disconnecting", peer);
            else if (read_result.error() != std::errc::operation_canceled)
                log::error("Receive error from {}: {}", peer, read_result.error());

            co_return;
        }

        // 回显收到的数据
        auto bytes = *read_result;
        if (bytes == 0) {
            log::info("Client disconnected: {}", peer);
            co_return;
        }

        co_await net::send(client, std::span{ buffer }.first(bytes));
    }
}
```

关键点：

1. **`async::timeout(operation, duration)`** — 为操作添加超时
   - 若超时，返回 `std::errc::timed_out` 错误
   - 若操作成功，返回其原始结果
   
2. **为什么用 `async_receive_some` 而非 `receive_stream().next()`？**
   - `async_receive_some` 是单次 recv，满足 `single_shot_only_operation` concept
   - `timeout` 用 `IOSQE_IO_LINK` 链接两个 SQE（recv + linked_timeout）
   - `receive_stream().next()` 内部使用 `recv_multishot`，不支持 `timeout` 包裹

3. **error 处理**
   - `timed_out` — 正常情况，记录 log 后断开
   - `operation_canceled` — `async::stop()` 触发，静默返回
   - 其他错误 — 真正的 I/O 故障，记录 warning

## 运行

```bash
./examples/timeout_echo/example.timeout_echo 12345
```

连接后 5 秒不发数据，服务器自动断开：

```
[info] Listening on port 12345 (idle timeout: 5s)
[info] Client connected: 127.0.0.1:54321
# （5 秒无数据）
[info] Client 127.0.0.1:54321 idle timeout, disconnecting
```

发送数据后立即复位超时计时器：

```
[info] Client connected: 127.0.0.1:54321
# （客户端发送 "hello"）
[info] Client 127.0.0.1:54321 idle timeout, disconnecting  # 5s 后断开
```

## `async::timeout` 的两个重载

| 操作类型 | 如何工作 | 示例 |
|---------|---------|------|
| `single_shot_only_operation` | 用 `IOSQE_IO_LINK` 链接 recv + timeout | `async_receive_some(buf)` |
| `cancelable_operation` | 独立 timer SQE + 互相取消 | `net::send(socket, buf)` |

这个示例用的是第一种（linked timeout），开销极低。

## 下一步

掌握了超时控制后，下一步学习流式协议解析：用 `receive_stream()` 实现按行或按分帧读取：[line_protocol](07_line_protocol.md)。
