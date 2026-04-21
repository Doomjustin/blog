在构建了底层的异步 I/O 引擎（`IOContext`）与核心的 Awaiter 机制后，我们的网络库已经具备了处理并发事件的能力。但要让它真正与网络世界通信，我们必须跨越网络编程的第一道门槛：**地址与端点（Endpoint）的封装**。

在原生的 POSIX C API 中，网络地址的表示极其繁琐。开发者需要手动处理 `sockaddr_in`（IPv4）、`sockaddr_in6`（IPv6）甚至 `sockaddr_un`（Unix Domain Socket），并充斥着各种宏与危险的指针强制转换。

本章的目标是：从零开始，一步步利用 C++20 的语言特性，构建出一套强类型、内存安全且**零运行时开销**的 Endpoint 体系。（也可以看做对asio的cosplay）

我们最终期望的业务层 API 形态，应该是极致简洁的：

~~~cpp
// 期望的现代 C++ 用法：自动推导，透明解析
auto ep1 = ip::tcp::endpoint::from_string("127.0.0.1", 12345); 
auto ep2 = ip::tcp::endpoint::from_string("::1", 12345);       
~~~

要实现这一目标，我们需要拆解三个核心概念：协议（Protocol）、IP 地址（Address）与端点（Endpoint）。

---

### 1. 协议抽象：静态特征与运行期状态的权衡

任何网络通信都需要指定协议。在 C++ 中，为了追求性能，我们通常倾向于将一切可能的信息在编译期固化。对于 `Protocol`，第一时间，我们可能会设计出一个如下的纯模板类：

~~~cpp
// 纯静态协议封装（存在局限性）
template<int Domain, int Type, int Protocol>
struct BasicProtocol {
    static constexpr int domain = Domain;
    static constexpr int type = Type;
    static constexpr int protocol = Protocol;
};
~~~

对于 `Type`（如 `SOCK_STREAM` 代表 TCP）和 `Protocol`（如 `IPPROTO_TCP`），它们确实是静态不变的。然而，`Domain`（地址族，即 `AF_INET` 或 `AF_INET6`）在现代网络编程中，**并非总能在编译期绝对固化**。

考虑一个支持 **双栈(Dual-Stack)** 的服务器：当你创建一个监听 `::` 的 Acceptor 时，它在运行期需要同时处理 IPv4 和 IPv6 的接入。如果 `Domain` 被彻底写死在模板参数中，我们将无法用单一的泛型类型来描述这个 Acceptor。

因此，正确的设计哲学是：**固化协议的本原特征，保留地址族的运行期决议能力**。

以下是我们对 `ip::tcp` 的完整实现：

~~~cpp
namespace ip {
class tcp {
public:
    // 1. 本原特征：使用 consteval 强制在编译期求值，拒绝任何运行时状态的介入
    [[nodiscard]] consteval auto type() const noexcept -> int { return SOCK_STREAM; }
    [[nodiscard]] consteval auto protocol() const noexcept -> int { return IPPROTO_TCP; }

    // 2. 运行期特征：使用 constexpr，允许在运行时根据双栈需求进行动态切换
    [[nodiscard]] constexpr auto domain() const noexcept -> int { return domain_; }

    // 具名构造器，语义清晰
    static auto v4() noexcept -> tcp { return tcp{ AF_INET }; }
    static auto v6() noexcept -> tcp { return tcp{ AF_INET6 }; }

private:
    int domain_ = AF_INET;
    explicit tcp(int domain) : domain_{ domain } {}
};
} // namespace ip
~~~

通过将 `type()` 和 `protocol()` 声明为 `consteval`，我们在编译器层面建立了一条严格的契约，这在后续作为 Type Traits 提取协议参数时，提供了与静态常量完全一致的零开销保证。

### 2. IP 地址封装

端点是由 IP 地址和端口组成的。在封装端点之前，我们需要先解决繁琐的 IP 地址解析。

在 POSIX 中，IPv4 被存储为 4 字节的 `in_addr`，IPv6 被存储为 16 字节的 `in6_addr`。

我们首先利用 RAII 将它们分别封装，并隐藏丑陋的 `inet_pton`（字符串转网络字节序）系统调用。

以 `AddressV4` 为例：

~~~cpp
struct AddressV4 {
    using address_type = in_addr;
    address_type address{};

    // 字符串解析工厂
    static auto from_string(std::string_view address) -> AddressV4 {
        AddressV4 result;
        if (::inet_pton(AF_INET, address.data(), &result.address) != 1)
            throw_system_error("Failed to convert string to IPv4 address");
        return result;
    }

    // 格式化输出
    [[nodiscard]] auto to_string() const -> std::string {
        std::string buffer(INET_ADDRSTRLEN, '\0');
        if (::inet_ntop(AF_INET, &address, buffer.data(), INET_ADDRSTRLEN) == nullptr)
            throw_system_error("Failed to convert IPv4 address to string");
        return buffer;
    }
};
// AddressV6 的实现高度对称，此处省略
~~~

**统一抽象：构建 Address 类**

在业务代码中，我们不希望用户去手动 `if/else` 判断当前字符串是 V4 还是 V6。我们需要一个统一的 `Address` 类来容纳它们。

此时，**`std::variant`** 成为了最完美的工具。我们可以给出如下实现：

~~~cpp
class Address {
public:
    using address_type = std::variant<AddressV4, AddressV6>;

    Address() = default;
    Address(const AddressV4& ipv4) : address_{ ipv4 } {}
    Address(const AddressV6& ipv6) : address_{ ipv6 } {}

    [[nodiscard]] constexpr auto is_v4() const noexcept -> bool {
        return std::holds_alternative<AddressV4>(address_);
    }

    [[nodiscard]] auto to_string() const -> std::string {
        // 优雅的多态调用
        return std::visit([](const auto& addr) { return addr.to_string(); }, address_);
    }

    static auto from_string(std::string_view address) -> Address {
        AddressV4 ipv4{};
        if (::inet_pton(AF_INET, address.data(), &ipv4.address) == 1)
            return { ipv4 };

        AddressV6 ipv6{};
        if (::inet_pton(AF_INET6, address.data(), &ipv6.address) == 1)
            return { ipv6 };

        throw_system_error("Invalid IP address format");
        std::unreachable();
    }

private:
    address_type address_;
};
~~~

通过 `from_string`，我们实现了一个健壮的解析器：它会依次尝试按 IPv4 和 IPv6 解析字符串，并将成功的结果打包进安全的 `variant` 容器中。

### 3. Endpoint 封装：跨越 ABI 边界的内存博弈

现在，我们来到了最核心的部件 `BasicEndpoint`。它需要将前面实现的 `Protocol`、`Address` 与端口（Port）结合起来，并最终生成底层的 `sockaddr` 结构供内核使用。

#### 3.1 为什么引入 Protocol 模板参数？
你可能会疑惑：既然 IP 层的底层表示都是 `sockaddr`，为什么我们要设计成模板类 `BasicEndpoint<Protocol>`，而不是一个通用的 `Endpoint` 类？

这是出于 **强类型安全** 的考量。
TCP 的 `127.0.0.1:80` 和 UDP 的 `127.0.0.1:80` 在底层字节上完全一致，但在物理逻辑上是截然不同的通道。如果它们是同一个类型，开发者极易将 UDP 的端点传给 TCP 的 Socket 进行 `connect`，这种谬误只能在运行时由内核抛出异常。
通过 `Protocol` 模板，`endpoint<tcp>` 和 `endpoint<udp>` 在 C++ 编译器眼中变成了绝对正交的两种类型，任何混用都会在编译期被拦截，这是零开销抽象的典范。

#### 3.2 致命陷阱：为何 Endpoint 内部必须摒弃 std::variant？
在封装 `Address` 时，我们使用了 `std::variant`。但在 `Endpoint` 内部存储底层结构时，却不能继续使用 `std::variant<sockaddr_in, sockaddr_in6>` 了，

`Endpoint` 的内存不仅用于读取，更要以裸指针（`sockaddr*`）的形式交给内核 API（如 `accept` 或 `recvfrom`）。这些 API 具有 **Overwrite** 语义：内核会直接根据实际接收到的连接，向这块内存灌入 IPv4 或 IPv6 的字节流。

如果底层是 `std::variant`：
1. **内存布局破坏**：`variant` 内部存在一个用于记录当前类型的 `index` 标记（以及可能的对齐 Padding）。内核如果从首地址开始写 `sa_family`，会直接破坏这个标记。
2. **状态脱节（UB）**：假设 `variant` 当前为 IPv4（16字节），内核写入了 IPv6（28字节）的数据。内核无从知晓 C++ 的机制，绝不会去更新 `variant` 的 `index`。当 C++ 代码再次读取时，将发生严重的未定义行为（Undefined Behavior）。

#### 3.3 解决方案：Union 与 sockaddr_storage
为了在确保 C 兼容性的同时提供 C++ 视图，最标准的解决方案是：**使用 `union` 配合 `sockaddr_storage`。** 也借此机会，强调一下C++的底层哲学：**程序的世界没有银弹**。对于不同场景选择适合的方式，所以C++提供了大量特性。

~~~cpp
template<typename Protocol>
class BasicEndpoint {
public:
    using protocol_type = Protocol;
    using address_type = Address;

    // ... 构造函数见下文 ...

    // 提供给内核 API 的多态强转接口
    auto data() noexcept -> sockaddr* 
    {
        return reinterpret_cast<sockaddr*>(&data_.storage);
    }

private:
    // 经典的多态内存视图 (Aliased Views)
    union AddressType {
        sockaddr_storage storage; // 128字节，最严格对齐，提供绝对安全的物理容量兜底
        sockaddr_in v4;           // 提供给 C++ 侧的 IPv4 具象化读写视图
        sockaddr_in6 v6;          // 提供给 C++ 侧的 IPv6 具象化读写视图
    } data_;
};
~~~

这里巧妙利用了 C/C++ 语言规范中的**公共初始序列**机制：所有 `sockaddr` 家族结构体的头两个字节都是 `sa_family_t`。

因此，即便内核粗暴地覆盖了 `storage`，我们依然可以通过 `data_.storage.ss_family` 安全且合法地获知当前内存中实际装载的协议。

### 4. 严守边界：Size 与 Capacity 的严格隔离

底层封装中最容易触发“缓冲区截断”Bug 的，是如何向内核报告这块 `union` 的长度。我们必须显式隔离 `size()` 和 `capacity()` 的语义：

~~~cpp
    // 专供 输出型 API (如 accept, recvfrom) 使用
    [[nodiscard]] constexpr auto capacity() const noexcept -> socklen_t {
        return sizeof(sockaddr_storage); // 永远返回最大容量 128 字节
    }

    // 专供 输入型 API (如 bind, connect) 使用
    [[nodiscard]] constexpr auto size() const noexcept -> socklen_t {
        if (data_.storage.ss_family == AF_INET) return sizeof(sockaddr_in); // 16 字节
        return sizeof(sockaddr_in6); // 28 字节
    }
~~~

* **Input 操作（`bind` / `connect`）**：内核要求**精确匹配**。如果你调用 `bind` 时传入了 128 字节（`capacity`），内核会因为长度不符合 IPv4（16）或 IPv6（28）的规约而直接返回 `-EINVAL`。必须严格使用动态计算的 `size()`。
* **Output 操作（`accept`）**：内核要求提供**最大安全缓冲**。在双栈监听模式下，随时可能接入 28 字节的 IPv6 客户端。如果此时你传入的是 `size()`（若端点默认初始化为 v4，则为 16），内核会直接截断写入，导致提取到的客户端地址完全损坏。必须严格使用 `capacity()`。

### 5. 拼图闭环：优雅的构造过程

有了上述坚实的底层基础，我们可以将 `Address` 对象转换为底层的 `union` 表示，最终兑现文章开头“一键构造”的承诺：

~~~cpp
    BasicEndpoint(const address_type& address, in_port_t port) {
        std::memset(&data_, 0, sizeof(data_)); // 彻底清空，防止残留垃圾数据
        
        if (address.is_v4()) {
            data_.storage.ss_family = AF_INET;
            data_.v4.sin_family = AF_INET;
            data_.v4.sin_port = ::htons(port);
            data_.v4.sin_addr = address.to_v4().address;
        } else {
            data_.storage.ss_family = AF_INET6;
            data_.v6.sin6_family = AF_INET6;
            data_.v6.sin6_port = ::htons(port);
            data_.v6.sin6_addr = address.to_v6().address;
        }
    }

    static auto from_string(std::string_view address, in_port_t port) -> BasicEndpoint {
        // 委托给 Address 的 variant 解析引擎，解析完成后交由本类的构造函数填充物理内存
        return BasicEndpoint{ address_type::from_string(address), port };
    }
~~~

至此，我们的 `Endpoint` 彻底打通了从上层字符串抽象到底层 C 语言裸内存的通路。它对外提供了严格的类型契约，对内完美化解了 ABI 边界的内存博弈。

对了，不要忘了，在tcp中，导出我们的endpoint
~~~c++
class tcp {
public:
    using address = Address;

    using endpoint = BasicEndpoint<tcp>;
};
~~~

[完整代码](../src/ip/endpoint.h)
