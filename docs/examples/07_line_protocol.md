# Line protocol streaming

> **源文件**：[examples/line_protocol/main.cpp](../../examples/line_protocol/main.cpp)  
> **用法**：`example.line_protocol <port>`

这个示例展示如何用 `receive_stream()` 实现流式协议解析。相比单次 recv，`recv_multishot` 在一次提交后能持续产出多个数据包，适合处理长连接上的分帧协议（如行协议、JSON lines、Protocol Buffers 等）。

## 流式读取的核心差异

与之前示例的对比：

| 示例 | recv 方式 | 适用场景 |
|------|---------|---------|
| [tcp_echo_server](04_tcp_echo_server.md) | `receive_stream().next()` | 高吞吐数据流 |
| [timeout_echo_server](06_timeout_echo_server.md) | `async_receive_some()` | 单次读 + 超时控制 |
| **line_protocol** | `receive_stream().next()` | 分帧协议解析 |

`receive_stream()` 用 `recv_multishot` 让内核在一个 SQE 上产出多个 CQE，每个 CQE 对应一块到达的数据。关键特性：

1. **极低开销**：一次提交，内核持续产出
2. **自动缓冲管理**：使用 buffer ring 避免重复映射
3. **零拷贝视图**：返回 `PooledBuffer` 的 span，完全零拷贝

## 行协议解析

在处理每一块数据前，需要提取有效的一行文本。`process_line()` 完成这个工作：

```cpp
auto process_line(std::span<const std::byte> line) -> std::string
{
    auto view = as_string(line);

    // 移除末尾 \r\n
    while (!view.empty() && (view.back() == '\r' || view.back() == '\n'))
        view.remove_suffix(1);

    return std::string(view);
}
```

关键点：
- `as_string()` 零拷贝转换：byte span → string_view
- `remove_suffix()` 修改 view（不涉及拷贝），剥离末尾换行符
- 最后才构造 `std::string`（一次拷贝），避免中间转换

## 行协议服务端

```cpp
constexpr auto idle_timeout = 5s;

auto session(net::ip::tcp::socket client, net::ip::tcp::endpoint peer) -> async::Task<>
{
    log::info("Client connected: {}", peer);

    auto stream = client.receive_stream();
    while (true) {
        auto read_result = co_await stream.next();

        if (!read_result) {
            if (read_result.error() != std::errc::operation_canceled)
                log::error("Receive error from {}: {}", peer, read_result.error());
            co_return;
        }

        auto buffer = read_result->data();
        if (buffer.empty()) {
            log::info("Client disconnected: {}", peer);
            co_return;
        }

        auto line = process_line(buffer);
        log::info("Received from {}: {}", peer, line);

        // 回显这一行
        auto response = line + "\n";
        auto write_result = co_await net::send(client, async::buffer(response));
        if (!write_result) {
            if (write_result.error() != std::errc::operation_canceled)
                log::error("Send error to {}: {}", peer, write_result.error());
            co_return;
        }
    }
}
```

核心步骤：

1. **`stream.next()`** — 等待下一块数据到达
   - 内部 `recv_multishot` 持续运行
   - 数据到达时立即返回 `PooledBuffer`
   
2. **`buffer.empty()`** — 对端关闭连接
   - 从 buffer ring 返回 0 字节表示 EOF
   
3. **`process_line(buffer)`** — 提取并清理文本行
   - 使用 `as_string()` 零拷贝转 string_view
   - 剥离末尾 `\r\n`
   - 最后构造返回 string

## 运行示例

```bash
./example.line_protocol 12345
```

客户端连接后发送多行：

```bash
$ nc 127.0.0.1 12345
hello world
hello world        ← 回显
foo bar
foo bar            ← 回显
^C
```

服务器日志：

```
[info] Line protocol server listening on port 12345
[info] Client connected: 127.0.0.1:54321
[info] Received from 127.0.0.1:54321: hello world
[info] Received from 127.0.0.1:54321: foo bar
[info] Client disconnected: 127.0.0.1:54321
```

## 重要限制：半包和粘包处理

这个示例**没有处理 TCP 的半包和粘包问题**：

- **半包**：`stream.next()` 可能返回 `"hello\nwo"`（不完整），剩余 `"rld\n"` 下次才来
- **粘包**：`stream.next()` 可能返回 `"hello\nworld\nfoo\n"`（多行合并在一块）

生产环境需要：
1. 维护一个接收缓冲区（accumulator）
2. 每次 `stream.next()` 时，追加新数据到缓冲区
3. 循环查找 `\n`，逐行提取并处理
4. 保留未完成的行，等待下次数据到达

这个示例只是为了展示 `receive_stream()` 的基本用法，实际协议实现需要自己处理分帧。

## 为什么不能对 `receive_stream().next()` 加超时？

看起来可以对任何 awaiter 加 `async::timeout`，但 `NextAwaiter` 不满足任何 timeout 的 concept：

- 不是 `single_shot_only_operation`（没有 `prepare()` / `set_result()` 接口）
- 不是 `cancelable_operation`（没有 `context()` 和 parent 指针机制）

如果真的需要超时，应该改用 `async_receive_some()` —— 它满足 `single_shot_only_operation`，可以直接套 `async::timeout`。

## 下一步

掌握了流式读取，下一步是学习写路径优化：多缓冲区聚集写（scatter-gather）和零拷贝发送。

👉 [08_scatter_gather.md](08_scatter_gather.md)
