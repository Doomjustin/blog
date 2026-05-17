#ifndef BLOG_NET_IP_STREAM_SOCKET_H
#define BLOG_NET_IP_STREAM_SOCKET_H

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <net/connect_awaiter.h>
#include <net/receive_awaiter.h>
#include <net/send_awaiter.h>
#include <net/socket.h>

namespace net::ip {

/// @brief 面向流协议（TCP）的 socket 封装，继承自 BasicSocket。
///
/// 在 BasicSocket 基础上增加 connect/shutdown/send/receive 及
/// TCP 专属 socket option 支持。
///
/// @tparam Protocol 协议类型，需满足 socket_protocol（如 Tcp）。
template<typename Protocol>
class StreamSocket
  : public BasicSocket<Protocol>
  , public QueryRemoteEndpoint<StreamSocket<Protocol>> {
public:
    using base_type = BasicSocket<Protocol>;
    using endpoint_type = typename Protocol::endpoint;

    /// @brief 控制 shutdown 方向。
    enum class how : std::uint8_t { receive = SHUT_RD, send = SHUT_WR, both = SHUT_RDWR };

    /// @brief 启用/禁用 TCP keep-alive（SO_KEEPALIVE）。
    using keep_alive = BooleanOption<SOL_SOCKET, SO_KEEPALIVE>;
    /// @brief keep-alive 探测前的空闲时间（TCP_KEEPIDLE，秒）。
    using keep_alive_idle = ValueOption<IPPROTO_TCP, TCP_KEEPIDLE>;
    /// @brief keep-alive 探测间隔（TCP_KEEPINTVL，秒）。
    using keep_alive_interval = ValueOption<IPPROTO_TCP, TCP_KEEPINTVL>;
    /// @brief keep-alive 探测最大失败次数（TCP_KEEPCNT）。
    using keep_alive_count = ValueOption<IPPROTO_TCP, TCP_KEEPCNT>;
    /// @brief 禁用 Nagle 算法（TCP_NODELAY）。
    using no_delay = BooleanOption<IPPROTO_TCP, TCP_NODELAY>;

    StreamSocket() = default;

    /// @brief 按协议构造并创建 socket。
    /// @param[in] protocol 协议对象（如 Tcp::v4()）。
    explicit StreamSocket(const Protocol& protocol)
      : base_type{ protocol }
    {}

    /// @brief 接管已有 fd 构造 StreamSocket。
    /// @param[in] fd 已创建的 socket fd。
    explicit StreamSocket(int fd)
      : base_type{ fd }
    {}

    /// @brief 同步连接到远端端点。
    /// @param[in] peer 目标端点。
    /// @throws std::system_error 连接失败时抛出。
    /// @note 非阻塞 socket 下会立即返回，不等待握手完成；建议优先使用 async_connect。
    void connect(const endpoint_type& peer)
    {
        if (::connect(this->native_handle(), peer.data(), peer.size()) != 0)
            throw_system_error("Failed to connect socket");
    }

    /// @brief 异步连接到远端端点（io_uring）。
    /// @param[in] peer 目标端点。
    /// @return ConnectAwaiter，co_await 后返回 expected<void, error_code>。
    /// @note 协程挂起直到 TCP 三次握手完成或出错。
    auto async_connect(const endpoint_type& peer) noexcept -> ConnectAwaiter<Protocol>
    {
        return { this->native_handle(), peer };
    }

    /// @brief 关闭 socket 的发送/接收方向。
    /// @param[in] how 关闭方向（receive/send/both）。
    /// @return 成功返回空 expected；失败返回对应 error_code。
    auto shutdown(how how) noexcept -> std::expected<void, std::error_code>
    {
        if (::shutdown(this->native_handle(), std::to_underlying(how)) == -1)
            return unexpected_system_error();

        return {};
    }

    /// @brief 异步接收数据（io_uring）。
    /// @param[in] buffer 接收缓冲区。
    /// @return ReceiveAwaiter，co_await 后返回 expected<size_t, error_code>。
    auto async_read(std::span<std::byte> buffer) noexcept -> ReceiveAwaiter
    {
        return { this->native_handle(), buffer };
    }

    /// @brief 异步发送数据（io_uring）。
    /// @param[in] buffer 发送缓冲区。
    /// @return SendAwaiter，co_await 后返回 expected<size_t, error_code>。
    auto async_write(std::span<const std::byte> buffer) noexcept -> SendAwaiter
    {
        return { this->native_handle(), buffer };
    }
};

} // namespace net::ip

#endif // BLOG_NET_IP_STREAM_SOCKET_H