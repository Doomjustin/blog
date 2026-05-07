# io_uring + C++ Coroutine 网络库

基于 Linux io_uring 与 C++23 协程构建的异步网络库。用协程语法写出直线型的异步代码，零手动回调，零线程阻塞。

## 快速上手

### 依赖

- Linux kernel ≥ 6.1（io_uring multishot 支持）
- Clang ≥ 17 或 GCC ≥ 13
- CMake ≥ 3.30
- pkg-config
- liburing
- jemalloc
- [vcpkg](https://github.com/microsoft/vcpkg)（依赖管理）

### 构建

```bash
export VCPKG_ROOT=/path/to/vcpkg

cmake -S . -B build-release -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake --build build-release -j
```

### 最小示例

```cpp
#include <blog.h>

auto hello() -> async::Task<>
{
    log::info("hello from coroutine");
    co_return;
}

int main()
{
    async::run(hello);
}
```

### TCP echo server

```cpp
#include <blog.h>

auto session(net::ip::tcp::socket client, net::ip::tcp::endpoint peer) -> async::Task<>
{
    auto stream = client.receive_stream();
    while (true) {
        auto read_result = co_await stream.next();
        if (!read_result || read_result->data().empty())
            co_return;

        co_await net::send(client, read_result->data());
    }
}

auto echo_server(std::uint16_t port) -> async::Task<>
{
    async::setup_buffer_ring(128, 4096);

    auto endpoint = net::ip::tcp::endpoint{ net::ip::AddressV4::any(), port };
    auto acceptor = net::ip::tcp::acceptor{ endpoint, true };

    while (true) {
        net::ip::tcp::endpoint peer;
        auto client = co_await acceptor.async_accept(peer);
        if (!client) {
            if (client.error() == std::errc::operation_canceled)
                co_return;

            continue;
        }

        async::co_spawn(session(std::move(*client), peer));
    }
}

auto shutdown_monitor() -> async::Task<>
{
    async::SignalSet signals{ async::signals::interrupt, async::signals::terminate };
    co_await signals.async_wait();
    async::stop();
}

int main()
{
    async::co_spawn(shutdown_monitor());
    async::run(1, echo_server, static_cast<std::uint16_t>(12345));
}
```

## 教程

逐步学习 io_uring + C++23 协程网络编程：[docs/tutorial/README.md](docs/tutorial/README.md)

## 博客

记录了这个库从零搭建的全过程：

| 篇 | 主题 |
|----|------|
| [01](docs/blogs/01_基础骨架_Awaiter机制.md) | 基础骨架：Awaiter 机制 |
| [02](docs/blogs/02_模块解耦_完备退出机制.md) | 模块解耦与完备退出机制 |
| [03](docs/blogs/03_链式请求_零开销超时.md) | 链式请求与零开销超时 |
| [04](docs/blogs/04_核心IO_协程化实现.md) | 核心 I/O 的协程化实现 |
| [05](docs/blogs/05_Protocol_Endpoint封装.md) | Protocol / Endpoint 封装 |
| [06](docs/blogs/06_Socket层次化封装.md) | Socket 层次化封装 |
| [07](docs/blogs/07_Acceptor实现.md) | Acceptor 实现 |
| [08](docs/blogs/08_写路径优化_Scatter-Gather与writev.md) | 写路径优化：Scatter-Gather 与 writev |
| [09](docs/blogs/09_读路径优化_recv_multishot与ReceiveStream.md) | 读路径优化：recv_multishot 与 ReceiveStream |
| [10](docs/blogs/10_API重构_让用户不再关心IOContext.md) | API 重构：让用户不再关心 IOContext |
| [11](docs/blogs/11_停止机制补完.md) | 停止机制补完 |
| [12](docs/blogs/12_零拷贝发送_SEND_ZC两阶段完成.md) | 零拷贝发送：SEND_ZC 两阶段完成 |