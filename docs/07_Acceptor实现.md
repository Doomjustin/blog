在上一篇文章中，我们构建了 `StreamSocket`，它作为面向连接的流式套接字，完美解决了客户端的主动连接（`connect`）与边界安全的字节流传输问题。

然而，网络通信是双向的。对于服务端而言，我们需要一种截然不同的实体：它不负责读写数据，只负责被动监听并生产新的连接。在 POSIX 网络栈中，这就是监听套接字（Listening Socket）。

本篇文章将探讨服务端组件 `BasicAcceptor` 的设计，并在文章的最后，利用我们迄今为止构建的所有基础设施，跑通一个完整的、零运行时开销的全异步 Echo Server。

### 1. 架构修正：为什么将 `bind` 下沉至 `BaseSocket`？

在着手编写 `Acceptor` 之前，我们必须先纠正一个在上一章中犯的经验主义错误。

在许多入门教程中，`bind` 似乎永远是服务端的专利（配合 `listen` 使用），而客户端只需要 `connect`。如果按照这个逻辑，`bind` 应该被封装在 `Acceptor` 的初始化中。

但这在真实的工业级场景中是行不通的。客户端在 `connect` 之前常常需要显式 `bind` 到特定的源 IP 来控制出口路由（比如多网卡流量隔离），或者绑定固定端口来配合 P2P 打洞。从操作系统的视角看，任何初始状态的文件描述符都具备被 `bind` 的物理能力。因此，在开发 `Acceptor` 之前，我们将 `bind` 能力从特定组件中剥离，**下沉到了最底层的 `BaseSocket` 中**。

### 2. 核心定位：强类型约束的“连接工厂”

明确了基础设施后，我们来看 `BasicAcceptor` 的定义。它继承自 `BaseSocket`，剥离了一切读写接口，其唯一的职责是作为连接的生产工厂。

```cpp
template<typename Protocol, typename Context>
class BasicAcceptor: public BaseSocket<Protocol, Context> {
public:
    // 强制静态推导：产出物必须与协议完全匹配
    using socket_type = typename Protocol::template socket<Context>;
    using endpoint_type = typename Protocol::endpoint;
    using base_type = BaseSocket<Protocol, Context>;
    // ...
};
```

这里的Type Traits至关重要。`socket_type` 确保了 `ip::tcp::acceptor` 产出的永远是被强类型保护的 `ip::tcp::socket`（即 `StreamSocket`），从编译器层面切断了将 TCP 连接误用为 UDP 套接字的可能。

### 3. 固化初始化序列与 SO_REUSEADDR

服务端的启动逻辑必须遵循严格的物理规约：配置选项 -> `bind` -> `listen`。其中最隐蔽的陷阱是 `SO_REUSEADDR` 选项的时序问题。

当服务端进程崩溃或更新重启时，旧的 TCP 连接可能仍处于 `TIME_WAIT` 状态。此时如果直接 `bind`，内核会抛出 `EADDRINUSE` (Address already in use) 错误。为了提升系统的重启弹性，**`SO_REUSEADDR` 必须在 `bind` 之前设置。**

我们在 `BasicAcceptor` 的构造函数中，强制实行了这套安全的初始化流程：

```cpp
using reuse_address = BooleanOption<SOL_SOCKET, SO_REUSEADDR>;
using reuse_port = BooleanOption<SOL_SOCKET, SO_REUSEPORT>;

BasicAcceptor(Context& context, const endpoint_type& endpoint, bool enable_reuse_port = false)
    : base_type{ context, endpoint.protocol() }
{
    this->option(reuse_address{ true });

    if (enable_reuse_port)
        this->option(reuse_port{ true });

    this->bind(endpoint);
    listen(MAX_LISTEN_CONNECTIONS);
}
```
对于需要横向扩展的多 Worker 进程架构，我们还提供了一个额外的配置项，允许显式开启 `SO_REUSEPORT`，将底层的连接负载均衡交由 Linux 内核的 TCP/IP 协议栈处理。

除此之外，用户也可以通过延迟打开的方式，自己配置相对应的option
~~~c++
auto acceptor = ip::tcp::accptor{ io_contex };
// 手动打开socket
acceptor.open(ep.protocol());

// 配置需要的option
acceptor.option(reuse_address{ true });

// 注意bind的先后顺序
acceptor.bind(ep);
acceptor.listen();
~~~

### 4. 异步抽象：获取对端元数据

在很多业务场景（如访问控制、日志审计）中，服务端需要知道新连接的源 IP 和端口。此时，我们需要提供一个接收 `endpoint_type` 引用的 `async_accept` 重载版本。

为了支持这一特性，我们需要稍微扩展底层的 `AcceptAwaiter`。内核的 `accept` 系统调用要求传入一个 `sockaddr*` 指针和一个保存结构体长度的 `socklen_t*` 指针作为输入输出（In/Out）参数。

```cpp
template<typename Protocol, typename Context>
class AcceptAwaiter: public Operation {
public:
    using socket_type = typename Protocol::template socket<Context>;
    using endpoint_type = typename Protocol::endpoint;
    // ...

    AcceptAwaiter(Context& context, int fd, endpoint_type* peer = nullptr)
      : context_{ context }, fd_{ fd }, peer_{ peer }
    {
        if (peer_)
            addrlen_ = peer_->capacity(); // 获取底层缓冲区的最大安全容量
    } 

    void prepare(::io_uring_sqe* sqe) noexcept override
    {
        // addrlen_ 将由内核覆写为实际的地址长度
        ::io_uring_prep_accept(sqe, 
                               fd_, 
                               peer_ ? peer_->data() : nullptr, 
                               peer_ ? &addrlen_ : nullptr, 
                               0);
    }

    void complete(int result, std::uint32_t flags) noexcept override
    {
        set_result(result, flags);

        // 返回前要 resize endpoint。ip::BasicEndpoint 用的是定长类型，resize 是个空操作；
        // 但其他协议的实现可能依赖这个调用，保留它以确保通用性。
        if (peer_ && result >= 0)
            peer_->resize(addrlen_);

        if (handle_) {
            auto handle = std::exchange(handle_, nullptr);
            handle.resume();
        }
    }

    // ...
private:
    Context& context_;
    int fd_;
    endpoint_type* peer_;
    socklen_t addrlen_; 

    // ...
};
```

借由 `Endpoint` 内部基于 `sockaddr_storage` 构建的内存缓冲区，无论对端是 IPv4 还是 IPv6，我们都能安全地承载内核写入的地址信息。

至此，`Acceptor` 可以在协程流中优雅地捕获对端信息：

```cpp
auto async_accept(endpoint_type& endpoint) noexcept -> AcceptAwaiter<Protocol, Context>
{
    return AcceptAwaiter<Protocol, Context>{ context(), native_handle(), &endpoint };
}
```

[完整代码](https://github.com/Doomjustin/blog/blob/main/src/acceptor.h)

### 5. 全异步 Echo Server 实战

基础设施拼图现已全部齐备（`IOContext`、`Endpoint`、`StreamSocket`、`Acceptor`、协程调度机制）。是时候用几行极其简练的 C++20 代码，检验这套系统的真实战力了。

我们将编写一个全异步的 TCP Echo Server。它包含两个核心的协程流：**会话流（Session）**与**分发流（Echo）**。

#### 5.1 业务逻辑：Session 协程
`session` 协程负责处理单一的客户端连接。在协程的作用下，原本复杂的异步状态机被拉平为直观的 `while(true)` 同步循环流。我们使用 `std::expected` 的返回值配合 `co_await`，在局部范围内完成了非阻塞的 I/O 与异常处理。

```cpp
#include <cstdlib>
#include <iostream>
#include <spdlog/spdlog.h>
#include "co_spawn.h"
#include "io_context.h"
#include "ip/tcp.h"
#include "task.h"
#include "timeout.h"
#include "buffer.h"

auto session(ip::tcp::socket<IOContext> client) -> Task<>
{
    auto data = std::string(1024, '\0');

    while (true) {
        // 1. 异步读取，协程挂起，零线程阻塞
        // 如果需要的话，你也可以套一个timeout。不过不要忘了除了timedout错误
        auto read_result = co_await client.async_read_some(buffer(data));
        if (!read_result) {
            spdlog::warn("Failed to read from client {}: {}", client.native_handle(), read_result.error().message());
            co_return;
        }

        auto bytes_read = *read_result;
        if (bytes_read == 0) {
            spdlog::info("Client {} disconnected", client.native_handle());
            co_return;
        }

        spdlog::info("Data from client {}: {}", client.native_handle(), data);

        auto write_buffer = buffer(data.substr(0, bytes_read));

        // 2. 利用 std::span 提取有效数据视图，异步写回
        using namespace std::literals::chrono_literals;
        auto write_result = co_await timeout(client.async_write_some(write_buffer), 5s);
        if (!write_result) {
            if (write_result.error() == std::errc::timed_out)
                spdlog::warn("Write to client {} timed out", client.native_handle());
            else
                spdlog::warn("Failed to write to client {}: {}", client.native_handle(), write_result.error().message());

            co_return;
        }
    }
}
```

#### 5.2 监听派发：Echo 协程与主循环
`echo` 协程使用 `Acceptor` 监听本地端口。当内核将新连接投递到 `io_uring` 的完成队列（CQE）时，`echo` 协程被唤醒，并通过 `co_spawn` 将接收到的强类型 Socket 所有权移交给新的 `session` 协程。

```cpp
auto echo(IOContext& context) -> Task<>
{
    // auto endpoint = ip::tcp::endpoint::from_string("127.0.0.1", 12345);
    auto endpoint = ip::tcp::endpoint{ ip::AddressV6::loopback(), 12345 };
    std::cout << "Server listening on " << endpoint << "\n";

    auto acceptor = ip::tcp::acceptor{ context, endpoint };

    auto client_endpoint = ip::tcp::endpoint{};
    while (true) {
        auto client = co_await acceptor.async_accept(client_endpoint);
        if (!client) {
            spdlog::warn("Failed to accept client connection: {}", client.error().message());
            continue;
        }

        spdlog::info("Accepted connection from {}:{}", client_endpoint.address().to_string(), client_endpoint.port());
        co_spawn(context, session(std::move(*client)));
    }
}

int main(int argc, char* argv[])
{
    IOContext context;

    // 启动监听协程
    co_spawn(context, echo(context));

    // 启动 io_uring 事件循环
    context.run();

    return EXIT_SUCCESS;
}
```

[完整代码](https://github.com/Doomjustin/blog/blob/main/demo/tcp_ip.cpp)

运行结果
~~~bash
[2026-04-23 15:49:48.030] [info] Accepted connection from ::1:37696
[2026-04-23 15:49:49.471] [warning] Data from client 7: dsaf

[2026-04-23 15:49:51.885] [warning] Data from client 7: dasaasdd

[2026-04-23 15:49:54.124] [info] Accepted connection from ::1:48684
[2026-04-23 15:49:55.462] [warning] Data from client 8: fasasdasdfas

[2026-04-23 15:49:55.987] [info] Client 8 disconnected
[2026-04-23 15:49:57.515] [info] Client 7 disconnected
^C[2026-04-23 15:49:59.360] [info] Received shutdown signal, stopping IOContext...
~~~

### 结语
我们利用 C++20 的协程机制彻底抹平了异步 I/O 的认知鸿沟；利用 `io_uring` 将系统调用的开销降至极限；更重要的是，利用现代 C++ 的类型萃取与所有权语义（RAII & Move Semantics），我们将资源泄漏、类型混用等致命的系统级并发问题，统统拦截在了编译期。

别的类型的socket我们不做介绍，和stream socket大体上都是相同的。后续会回归主线，介绍writev以及uring buffer环形队列的协程接口封装。