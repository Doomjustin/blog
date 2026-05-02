# Making a TCP connection

> **源文件**：[examples/tcp_echo/echo_client.cpp](../../examples/tcp_echo/echo_client.cpp)  
> **用法**：`example.tcp_echo.client <host> <port> <message>`

这个示例展示如何在协程里完成一次完整的 TCP 请求：连接、发送、接收、打印。

## 先看运行效果

```
$ example.tcp_echo.client 127.0.0.1 12345 hello
[info] Connected to 127.0.0.1:12345
[info] Sent 5 bytes
[info] Received 5 bytes
hello
```

## 建立连接

首先，从地址字符串构造一个 endpoint，然后创建 socket 并同步连接：

```cpp
auto endpoint = net::ip::tcp::endpoint::from_string(host, port);
auto socket = net::ip::tcp::socket{ endpoint.protocol() };
socket.connect(endpoint);
```

`endpoint::from_string` 同时接受 IPv4（`"127.0.0.1"`）和 IPv6（`"::1"`）地址。

## 发送数据

发送前，需要把数据包装成 `async::buffer`——它持有一段连续内存的视图，不发生拷贝：

```cpp
co_await net::send(socket, async::buffer(message));
```

`net::send` 返回 `std::expected<std::size_t, std::error_code>`。`co_await` 挂起协程直到发送完成，返回实际写入的字节数。

## 接收响应

```cpp
std::string response(message.size(), '\0');
co_await net::receive(socket, async::buffer(response));
```

`net::receive` 是 **receive-all**：它会在内部循环提交 `recv`，直到 buffer 被填满、出错或对端关闭连接，才让协程恢复。这对于"已知响应长度"的场景（如本示例）非常方便。

如果不知道数据长度，或者需要流式处理，应当使用 `socket.async_receive_some(buffer)` 获取单次到达的数据，或者使用 `receive_stream()` 让库来管理 buffer（参要2 [tcp_echo server](04_tcp_echo_server.md)）。

## 错误处理

`net::send` 和 `net::receive` 都通过 `std::expected` 返回错误，不抛异常。检查方式如下：

```cpp
auto result = co_await net::send(socket, async::buffer(message));
if (!result) {
    log::error("send failed: {}", result.error());
    co_return;
}
```

## 下一步

客户端只负责发起连接。要让这个 echo 真正跑起来，还需要一个服务端：[tcp_echo server](04_tcp_echo_server.md)。
