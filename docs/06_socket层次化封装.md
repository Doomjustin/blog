在构建了底层的异步轮询引擎（`IOContext`）、协程机制以及端点（`Endpoint`）的内存布局后，我们进入网络编程的核心实体：套接字（Socket）。

在早期的概念验证阶段（第四篇博客中），为了快速验证系统可行性，我们曾实现过一个简陋的 `Socket` 类，将描述符的创建、`bind`、`listen`、`accept`、`read` 和 `write` 糅合在一个结构中。

作为原型，它是合格的。但作为工业级的基础设施，这种设计存在致命的类型安全隐患：如果一个用于 UDP 的数据报套接字暴露了 `listen()` 和 `accept()` 接口，编译器并不会报错，逻辑谬误只会在运行时以 `-EOPNOTSUPP`（Operation not supported）的形式暴露。

本篇的目标是从宏观架构出发，构建一个职责单一、零运行时开销，且受 C++20 强类型系统严格保护的套接字层级体系。

-----

### 1\. 宏观架构

POSIX 系统为网络通信提供了极度灵活但也极其松散的 C API。在现代 C++ 中，核心接口设计原则是：**让接口易于正确使用，难以被误用**。为此，我们必须根据网络协议的物理行为特征，将 Socket 拆解为层次分明的类簇：

1.  **`BaseSocket`**：
    所有套接字的基类。其唯一职责是：**基于 RAII 原则管理文件描述符（fd）的生命周期**，并提供底层统一的套接字选项（Socket Options）配置接口。

2.  **`Acceptor`（被动接收器）**：
    继承自 `BaseSocket`。专门用于服务端监听。仅开放 `bind`、`listen` 和 `accept` 接口。它剥离了数据读写能力，因为监听套接字本身不应参与数据载荷的收发。

3.  **`StreamSocket`（流式套接字）**：
    继承自 `BaseSocket`。代表**面向连接、可靠的字节流通信**（如 `ip::tcp` 或本机流式 IPC `local::stream_protocol`）。开放 `connect`、`read_some` 和 `write_some` 接口。

4.  **`DatagramSocket`（数据报套接字）**：
    继承自 `BaseSocket`。代表**无连接、不可靠的数据报通信**（如 `ip::udp`）。无 `connect` 语义，仅开放 `send_to` 和 `receive_from` 接口。

通过这一分层，若业务代码试图在 UDP Socket 上调用 `accept`，编译器将在编译阶段直接抛出“找不到该成员函数”的错误。我们将潜在的运行时崩溃彻底转化为编译期约束。同时，该架构也为未来扩充不同的协议栈保留了正交性。

### 2\. 契约先行：Protocol 的 Concept 约束

在泛型编程中，必须确保传入的模板参数是合法的协议类型。根据上文对 TCP 的设计，协议需要提供调用 `::socket()` 系统调用所需的三要素：`domain`、`type` 和 `protocol`。

我们使用 C++20 的 Concept 来定义这一显式契约：

```cpp
template <typename T>
concept socket_protocol = requires (const T& t)
{
    T::endpoint_type;

    { t.domain() } -> std::convertible_to<int>;
    { t.type() } -> std::convertible_to<int>;
    { t.protocol() } -> std::convertible_to<int>;
};
```

引入 `socket_protocol` 约束后，任何不满足规范的自定义协议类型在实例化 Socket 时，编译器都会提供精确的诊断信息。

### 3\. 第一步：RAII 资源基类 BaseSocket

接下来构建套接字的基类 `BaseSocket`。

其核心是实现严格的所有权（Ownership）和移动语义。在系统级编程中，文件描述符是独占资源。尽管可以通过 `dup` 复制文件描述符，但在基础套接字封装中，拷贝往往意味着所有权语义的混乱。因此，我们明确拒绝拷贝语义。

```cpp
#include <system_error>
#include <utility>
#include <unistd.h>
#include "exceptions.h"

template<socket_protocol Protocol, typename Context>
class BaseSocket {
public:
    using context_type = Context;
    using protocol_type = Protocol;

    // 1. 基于协议类型创建底层 fd
    BaseSocket(Context& context, const Protocol& protocol)
      : context_{ &context }, 
        fd_{ create(protocol) }
    {}

    // 2. 彻底禁用拷贝语义
    BaseSocket(const BaseSocket&) = delete;
    auto operator=(const BaseSocket&) -> BaseSocket& = delete;

    // 3. 完美的移动语义：交接控制权，并将源对象的 fd 置为无效
    BaseSocket(BaseSocket&& other) noexcept
      : context_{ std::exchange(other.context_, nullptr) }, 
        fd_{ std::exchange(other.fd_, INVALID_SOCKET) }
    {}

    auto operator=(BaseSocket&& other) noexcept -> BaseSocket&
    {
        if (this == &other) return *this;
        close(); // 覆盖前必须先关闭自己现有的 fd
        context_ = std::exchange(other.context_, nullptr);
        fd_ = std::exchange(other.fd_, INVALID_SOCKET);
        return *this;
    }

    // 4. RAII 析构
    virtual ~BaseSocket() 
    {
        close();
    }

    [[nodiscard]] constexpr auto is_valid() const noexcept -> bool 
    {
        return fd_ != INVALID_SOCKET;
    }

    auto close() noexcept -> std::expected<void, std::error_code> 
    {
        if (is_valid()) {
            auto res = ::close(fd_);
            fd_ = INVALID_SOCKET;
            if (res == -1) return unexpected_system_error();
        }
        return {};
    }

    [[nodiscard]] constexpr auto native_handle() const noexcept -> int { return fd_; }
    [[nodiscard]] auto context() noexcept -> Context& { return *context_; }

protected:
    // 供 Acceptor 接收新连接后直接接管已存在的 fd
    BaseSocket(Context& context, int fd)
      : context_{ &context }, fd_{ fd }
    {}

private:
    static constexpr int INVALID_SOCKET = -1;
    Context* context_;
    int fd_ = INVALID_SOCKET;

    static auto create(const Protocol& protocol) -> int 
    {
        auto res = ::socket(protocol.domain(), protocol.type(), protocol.protocol());
        if (res == -1) throw_system_error("Failed to create socket");
        return res;
    }
};
```

这里模板参数的声明顺序（`Protocol`, `Context`）具有其实际意义。通过让 `Context` 作为第二个模板参数，我们可以在提供 `Protocol` 的前提下，利用 C++17 的类模板参数推导（CTAD）简化客户端代码：

```cpp
// 固定协议别名
template<typename Context>
using socket = stream_socket<tcp, Context>;

IOContext context{};

// 编译器自动推导 Context 为 IOContext，无需显式指定模板参数
auto s = socket{ context };
```

### 4\. 第二步：构建面向连接的 StreamSocket

确立了资源管理基线后，我们可以派生出 `StreamSocket`。

流式套接字的核心特征在于点对点连接（提供 `connect`）及无边界的字节流传输（提供 `read_some` 和 `write_some`）。这里我们摒弃了传统的 `void* buffer` 配合 `size_t length`，全面使用 C++20 的 `std::span<std::byte>`。它携带连续内存的边界信息，能在编译期和运行期最大限度地避免内存越界。

```cpp
#ifndef BLOG_IP_STREAM_SOCKET_H
#define BLOG_IP_STREAM_SOCKET_H

#include <span>
#include <expected>
#include "socket.h"
#include "operations.h"
#include "readsome_awaiter.h"   // 协程 Awaiter
#include "writesome_awaiter.h"

namespace ip {

template<typename Protocol, typename Context>
class StreamSocket: public BaseSocket<Protocol, Context> {
public:
    using base_socket_type = BaseSocket<Protocol, Context>;
    using endpoint_type = typename Protocol::endpoint;

    explicit StreamSocket(Context& context)
      : base_socket_type{ context, Protocol{} }
    {}

    // 接收内核已完成的连接 (用于 Acceptor 生成)
    StreamSocket(Context& context, int fd)
      : base_socket_type{ context, fd }
    {}

    StreamSocket(StreamSocket&&) = default;
    StreamSocket& operator=(StreamSocket&&) = default;

    ~StreamSocket() = default;

    // --- 同步阻塞接口 ---
    void connect(const endpoint_type& peer) 
    {
        // 配合 Endpoint 的 data() 和 size() 语义
        operations::connect(this->native_handle(), peer.data(), peer.size());
    }

    auto read_some(std::span<std::byte> buffer) noexcept 
        -> std::expected<std::size_t, std::error_code> 
    {
        return operations::read_some(this->native_handle(), buffer);
    }

    auto write_some(std::span<const std::byte> buffer) noexcept 
        -> std::expected<std::size_t, std::error_code> 
    {
        return operations::write_some(this->native_handle(), buffer);
    }

    // --- 异步协程接口 (对接 io_uring) ---
    auto async_read_some(std::span<std::byte> buffer) noexcept 
        -> ReadSomeAwaiter<Context> 
    {
        return ReadSomeAwaiter<Context>{ this->context(), this->native_handle(), buffer };
    }

    auto async_write_some(std::span<const std::byte> buffer) noexcept 
        -> WriteSomeAwaiter<Context> 
    {
        return WriteSomeAwaiter<Context>{ this->context(), this->native_handle(), buffer };
    }
};

} // namespace ip
#endif // BLOG_IP_STREAM_SOCKET_H
```

通过继承，`StreamSocket` 天然获取了生命周期管理能力，仅需专注具体的 I/O 逻辑。

值得探讨的是，**为何 `StreamSocket` 位于 `namespace ip` 中？**

在纯抽象层面，流式套接字似乎应是一个全局泛型概念：只要能读写字节流，即为 Stream Socket。然而，底层协议之间天然存在物理特征的不兼容。例如，IP 协议栈的流式套接字拥有专属于 IP 层的控制选项（如控制 Nagle 算法的 `TCP_NODELAY`）。

如果将其强制抽象为全局通用的 `StreamSocket`，会导致这些特有配置选项失去编译期类型保护，进而增加运行时决议的复杂度和错误风险。因此，利用 `namespace ip` 进行物理与语义双重隔离，是维持零开销抽象的必要设计：

```cpp
namespace ip {
template<typename Protocol, typename Context>
class StreamSocket: public BaseSocket<Protocol, Context> {
public:
    // ...
    // IP 协议簇专属的类型安全选项
    using keep_alive_idle = ValueOption<IPPROTO_TCP, TCP_KEEPIDLE>;
    using keep_alive_interval = ValueOption<IPPROTO_TCP, TCP_KEEPINTVL>;
    using keep_alive_count = ValueOption<IPPROTO_TCP, TCP_KEEPCNT>;
    using no_delay = BooleanOption<IPPROTO_TCP, TCP_NODELAY>;
    // ...
};
}
```

#### 缓冲区适配：优雅的调用体验 (Ergonomics)

与此同时，我们必须正视一个工程体验问题：虽然将底层的 I/O 接口固化为 `std::span<std::byte>` 确保了绝对的内存边界安全，但这会给上层业务代码带来不适。我们显然不能强制用户在每次读写时，都笨拙地手动强转指针，或者仅仅为了网络传输而将所有业务层容器都声明为 `std::array<std::byte, N>`。

为了优化调用侧的体验，同时不向底层 I/O 方法中引入任何多余的模板复杂度，我们利用 C++20 的 Concept，提供一个轻量级的视图转换工厂函数 `buffer()`。

对于**只读**的连续内存容器，我们将其零开销转换为 `std::span<const std::byte>`，用于 `write_some`：

```cpp
template<std::ranges::contiguous_range T>
auto buffer(const T& range) noexcept -> std::span<const std::byte>
{
    return std::as_bytes(std::span{ range });
}
```

这样一来，用户可以顺畅地写出如下代码，而不必亲自干预指针转换：

```cpp
std::string msg = "Hello, io_uring!";
// 自动推导并转换，安全且零拷贝
client_sock.write_some(buffer(msg));
```

对于**可写**的连续内存容器，我们将其转换为 `std::span<std::byte>`，用于 `read_some` 这样的输出调用：

```cpp
template<std::ranges::contiguous_range T>
    requires (!std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<T>>>)
auto buffer(T& range) noexcept -> std::span<std::byte>
{
    return std::as_writable_bytes(std::span{ range });
}
```

同样地，读取操作也变得极其自然：

```cpp
std::vector<char> recv_buf(1024);
// 安全地提取底层连续内存块供内核填充
client_sock.read_some(buffer(recv_buf));
```

这一层薄薄的抽象，大大优化了调用体验，且没有在核心的 I/O 方法中引入多余的模板膨胀。

### 5\. 协议的装配：Type Traits 工厂

如何将泛型的 `StreamSocket` 与具体的协议（如 TCP）优雅绑定？

在我们的设计中，`Protocol` 类（例如 `ip::tcp`）不仅提供静态常量，还充当类型特征（Type Traits）的装配枢纽。

将上述组件装配进去：

```cpp
namespace ip {

class tcp {
public:
    // 强制约束 tcp 的 socket 实现类型
    template<typename Context>
    using socket = StreamSocket<tcp, Context>;
    
    // ... 其他静态特征 ...
};

} // namespace ip
```

### 结语：零开销与强类型的交响曲

至此，让我们审视业务代码的最终形态：

```cpp
ip::tcp::socket<IOContext> client{ context };
ip::tcp::endpoint target = ip::tcp::endpoint::from_string("127.0.0.1", 8080);

client.connect(target);

std::vector<char> recv_buf(1024);
client.read_some(buffer(recv_buf))
```

在这寥寥数行代码中，类型系统在幕后默默完成了以下推导与约束：

1.  `ip::tcp::socket` 自动推导为 `StreamSocket<ip::tcp, IOContext>`。
2.  编译器确保 `connect` 仅接受 `ip::tcp::endpoint`，如果误传 `udp::endpoint` 会导致编译立刻失败。
3.  `StreamSocket` 的基类构造函数静态提取 `ip::tcp` 的 `SOCK_STREAM` 和 `IPPROTO_TCP` 以发起安全的系统调用。
4.  对象离开作用域时，`BaseSocket` 的 RAII 机制确保文件描述符被安全释放。

我们彻底隐藏了底层的状态机转移与裸露的 C API，以层次分明、类型严格的现代 C++ 接口取而代之。由于高度模板化和内联优化，这一系列抽象的运行期开销，严格等价于手写裸 C 语言代码。

在下一篇文章中，我们将补齐拼图的最后一块：`Acceptor`（被动接收器）的封装。届时，便可利用这套基础设施，跑通完整的基于 `io_uring` 的全异步协程服务器。

[完整代码](../src/ip/stream_socket.h)