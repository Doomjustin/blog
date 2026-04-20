#ifndef BLOG_IP_STREAM_SOCKET_H
#define BLOG_IP_STREAM_SOCKET_H

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "operations.h"
#include "protocol.h"
#include "readsome_awaiter.h"
#include "socket.h"

namespace ip {

template<typename Context, typename Protocol>
    requires is_same_protocol<Protocol, typename Protocol::endpoint>
class StreamSocket: public BaseSocket<Context, Protocol> {
public:
    using endpoint_type = typename Protocol::endpoint;

    using how = operations::ShutdownHow;

    using keep_alive = BooleanOption<SOL_SOCKET, SO_KEEPALIVE>;

    using keep_alive_idle = ValueOption<IPPROTO_TCP, TCP_KEEPIDLE>;

    using keep_alive_interval = ValueOption<IPPROTO_TCP, TCP_KEEPINTVL>;

    using keep_alive_count = ValueOption<IPPROTO_TCP, TCP_KEEPCNT>;
    
    using no_delay = BooleanOption<IPPROTO_TCP, TCP_NODELAY>;

    explicit StreamSocket(Context& context)
      : BaseSocket<Context, Protocol>{ context }
    {}

    StreamSocket(Context& context, int fd)
      : BaseSocket<Context, Protocol>{ context, fd }
    {}

    ~StreamSocket() = default;

    void connect(const endpoint_type& peer)
    {
        operations::connect(this->native_handle(), peer.data(), peer.size());
    }

    auto shutdown(how how) noexcept -> std::expected<void, std::error_code>
    {
        return operations::shutdown(this->native_handle(), how);
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

    auto async_read_some(std::span<std::byte> buffer) noexcept 
        -> ReadSomeAwaiter<Context>
    {
        return ReadSomeAwaiter<Context>{ this->context(), this->native_handle(), buffer };
    }

    // auto async_write_some(std::span<const std::byte> buffer) noexcept -> WriteSomeAwaiter
    // {
    //     return WriteSomeAwaiter{ this->context(), this->native_handle(), buffer };
    // }
};

} // namespace ip

#endif // BLOG_IP_STREAM_SOCKET_H