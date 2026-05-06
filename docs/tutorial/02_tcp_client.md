# 2.1 TCP 客户端：连接、发送与接收

> **前置知识**：本章假设你已完成 2.1 节，服务端 `tutorial.05_tcp_server` 已编译。

---

## 完整代码

```cpp
#include <blog.h>

namespace {

constexpr std::string_view MESSAGE = "hello, tutorial";

auto client() -> async::Task<>
{
    // 1. 构造目标地址
    auto endpoint = net::ip::tcp::endpoint::from_string("127.0.0.1", 12345);

    // 2. 创建 socket 并建立连接（同步；连接失败抛 std::system_error）
    auto socket = net::ip::tcp::socket{ endpoint.protocol() };
    try {
        socket.connect(endpoint);
    }
    catch (const std::exception& ex) {
        log::error("connect failed: {}", ex.what());
        co_return;
    }

    log::info("connected to {}", endpoint);

    // 3. 异步发送
    auto send_result = co_await net::send(socket, async::buffer(MESSAGE));
    if (!send_result) {
        log::error("send failed: {}", send_result.error());
        co_return;
    }

    log::info("sent {} bytes", *send_result);

    // 4. 异步接收回显
    std::string buf(MESSAGE.size(), '\0');
    auto recv_result = co_await net::receive(socket, async::buffer(buf));
    if (!recv_result) {
        log::error("recv failed: {}", recv_result.error());
        co_return;
    }

    if (*recv_result == 0) {
        log::warning("server closed connection before replying");
        co_return;
    }

    log::info("received {} bytes: {}", *recv_result,
              std::string_view{ buf.data(), *recv_result });
}

} // namespace

int main()
{
    async::run(client);
}
```

### 运行方式

终端 1——启动下一节（2.2）会详细讲解的服务端（此处直接编译运行）：

```bash
./build/tutorial/05_tcp_server/tutorial.05_tcp_server
```

终端 2——运行客户端：

```bash
./build/tutorial/06_tcp_client/tutorial.06_tcp_client
```

两个终端的输出合并如下（时间戳与 PID 因运行环境而异）：

```
# 服务端
[info] listening on 0.0.0.0:12345
[info] connected: 127.0.0.1:54608
[info] disconnected: 127.0.0.1:54608

# 客户端
[info] connected 127.0.0.1:54608 -> 127.0.0.1:12345
[info] sent 15 bytes
[info] received 15 bytes: hello, tutorial
```

---

## 逐步解析

### 1. 构造地址

```cpp
auto endpoint = net::ip::tcp::endpoint::from_string("127.0.0.1", 12345);
```

`from_string` 将字符串 IP 解析为内部地址结构，与服务端看到的 `AddressV4::any()` 互补。

### 2. 连接（同步）

```cpp
auto socket = net::ip::tcp::socket{ endpoint.protocol() };
try {
    socket.connect(endpoint);
}
catch (const std::exception& ex) {
    log::error("connect failed: {}", ex.what());
    co_return;
}
```

`connect` 是**同步调用**，失败时抛 `std::system_error`。在调用点 catch、打 log 后 `co_return`，与后续 `co_await` 操作的错误处理风格保持一致——错误在发生处就地处理，不向上传播。

`connect` 成功后，内核已完成端口绑定和路由。可以用 `local_endpoint` / `remote_endpoint` 查询本端和对端地址：

```cpp
if (auto local = local_endpoint(socket), remote = remote_endpoint(socket); local && remote)
    log::info("{}  ->  {}", *local, *remote);
```

两者均返回 `std::expected<endpoint_type, std::error_code>`。`local_endpoint` 反映内核自动分配的本地端口，`remote_endpoint` 反映已连接的对端地址，与构造 `endpoint` 时传入的值一致。

### 3. `net::send`——全量发送

```cpp
auto send_result = co_await net::send(socket, async::buffer(MESSAGE));
```

`async::buffer` 将 `std::string_view` 转为 `std::span<const std::byte>`，这是所有异步 I/O 函数接受的通用缓冲区视图。

`net::send` 是**全量发送**：循环重试直到整个缓冲区发完或出错。返回 `std::expected<std::size_t, std::error_code>`。

### 4. `net::receive`——全量接收

```cpp
std::string buf(MESSAGE.size(), '\0');
auto recv_result = co_await net::receive(socket, async::buffer(buf));
```

`net::receive` 是**全量接收**：读满缓冲区或出错为止。这里分配了与消息等长的缓冲区，因为我们知道回显的确切大小。

> `net::receive` vs `async_receive_some`：上一节服务端用的是 `async_receive_some`（单次、收多少算多少），客户端这里用 `net::receive`（读满为止）。两者适用场景不同——服务端处理未知长度的流式数据，客户端等待已知大小的回复。

### EOF 检测

```cpp
if (*recv_result == 0) {
    log::warning("server closed connection before replying");
    co_return;
}
```

服务端 `close()` 或 `shutdown(SHUT_WR)` 时，`recv` 返回 0。这是正常的半关闭信号，不是错误，需单独处理。

### 错误处理的形状

每个可能失败的操作紧跟一个 `if (!result)` 检查，失败时 `co_return` 提前退出。这种"向下流动"的控制流比 try/catch 更直观，在代码审查时也更容易发现遗漏的错误处理。

---

## 连接失败时

如果服务端没有运行，`socket.connect(endpoint)` 抛出异常，调用点的 catch 块捕获：

```
[error] connect failed: Failed to connect socket: Connection refused
```

---

## 本章小结

`net::send` 和 `net::receive` 通过 `co_await` 挂起协程并向 io_uring 提交操作，完成后以 `std::expected` 返回结果。与第 1 部分相比，唯一新增的模式是：**每次 `co_await` 之后检查 expected，失败则 `co_return`**。

---

下一节：[2.2 TCP 服务端：acceptor 与会话并发](02_tcp_server.md)
