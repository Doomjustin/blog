# Writing a TCP echo server

> **源文件**：[examples/tcp_echo/echo_server.cpp](../../examples/tcp_echo/echo_server.cpp)  
> **用法**：`example.tcp_echo.server <port>`

这是一个完整的 TCP echo 服务端：它持续接受连接，对每个客户端开启独立的协程会话，将收到的数据原样回写，并在收到 Ctrl-C 时干净地退出。

## 先看运行效果

```
$ example.tcp_echo.server 12345
[info] Listening on port 12345
[info] Client connected: 127.0.0.1:54321
[info] Client disconnected: 127.0.0.1:54321
[info] Shutting down...
```

## 第一部分：接受连接

服务端的核心是一个 acceptor 循环。`async_accept` 挂起协程，直到有新连接到来，并把对端地址写入 `peer`：

```cpp
while (true) {
    net::ip::tcp::endpoint peer;
    auto client = co_await acceptor.async_accept(peer);
    if (!client) {
        if (client.error() == std::errc::operation_canceled)
            co_return;  // 收到停止信号，正常退出
        log::error("Accept error: {}", client.error());
        continue;
    }
    async::co_spawn(session(std::move(*client), peer));
}
```

注意最后一行——`async::co_spawn` 把 `session` 作为独立的 detached 协程调度。这意味着 acceptor 循环**不会等待** session 完成，而是立刻回到 `co_await acceptor.async_accept` 继续接受下一个连接。

另外注意错误处理中的 `operation_canceled`：当调用 `async::stop()` 时，所有挂起的 io_uring 操作都会被取消，`async_accept` 以此错误码返回。这不是真正的错误，而是退出信号，应该 `co_return` 而不是 `log::error`。

## 第二部分：处理单个连接

每个 session 协程独立地读取和回写数据：

```cpp
auto stream = client.receive_stream();
while (true) {
    auto read_result = co_await stream.next();
    if (!read_result) {
        if (read_result.error() != std::errc::operation_canceled)
            log::error("Receive error from {}: {}", peer, read_result.error());
        co_return;
    }

    auto data = read_result->data();
    if (data.empty()) {
        log::info("Client disconnected: {}", peer);
        co_return;
    }

    co_await net::send(client, data);
}
```

`receive_stream()` 在内部使用 io_uring 的 `recv_multishot` 操作：一次提交就能持续产出数据，不需要每次读取都重新提交 SQE。`stream.next()` 每次调用返回下一块到达的数据；空 span 表示对端关闭了连接。

## 第三部分：优雅退出

服务端需要能响应 Ctrl-C（SIGINT）或 `kill`（SIGTERM）。做法是在启动主逻辑前，额外 `co_spawn` 一个监控协程：

```cpp
auto shutdown_monitor() -> async::Task<>
{
    async::SignalSet signals{ async::signals::interrupt, async::signals::terminate };
    co_await signals.async_wait();
    log::info("Shutting down...");
    async::stop();
}

int main(int argc, char* argv[])
{
    auto port_result = numeric_cast<std::uint16_t>(argv[1]);
    if (!port_result)
        throw std::system_error(port_result.error(), "invalid port");
    auto port = *port_result;

    async::co_spawn(shutdown_monitor());   // 先注册，与主逻辑并发
    async::run(1, echo_server, port);
}
```

`shutdown_monitor` 挂起在信号上。收到信号后，它调用 `async::stop()`，事件循环随即取消所有挂起的操作，等全部操作完成后 `async::run` 返回，进程干净退出。

## 下一步

现在我们可以处理单个 session 了。下一步学习如何让多个任务真正并发运行：[concurrent_tasks](05_concurrent_tasks.md)。
