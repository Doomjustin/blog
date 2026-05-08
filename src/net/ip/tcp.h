#ifndef BLOG_NET_IP_TCP_H
#define BLOG_NET_IP_TCP_H

#include <netinet/in.h>

#include <net/acceptor.h>
#include <net/ip/endpoint.h>
#include <net/ip/stream_socket.h>

namespace net::ip {

/**
 * @brief Protocol tag that identifies TCP stream sockets.
 *
 * Acts as a policy type: it carries address family (`AF_INET` or
 * `AF_INET6`), socket type (`SOCK_STREAM`), and protocol (`IPPROTO_TCP`)
 * metadata, and defines the associated `endpoint`, `socket`, and
 * `acceptor` type aliases used throughout the library.
 */
class tcp {
public:
    using address = Address;
    using endpoint = BasicEndpoint<tcp>;
    using socket = StreamSocket<tcp>;
    using acceptor = BasicAcceptor<tcp>;


    tcp() = default;

    [[nodiscard]]
    constexpr auto domain() const noexcept -> int
    {
        return domain_;
    }

    [[nodiscard]]
    consteval auto type() const noexcept -> int
    {
        return SOCK_STREAM;
    }

    [[nodiscard]]
    consteval auto protocol() const noexcept -> int
    {
        return IPPROTO_TCP;
    }

    /** @brief Return a TCP/IPv4 protocol instance. */
    static auto v4() noexcept -> tcp
    {
        return tcp{ AF_INET };
    }

    /** @brief Return a TCP/IPv6 protocol instance. */
    static auto v6() noexcept -> tcp
    {
        return tcp{ AF_INET6 };
    }

private:
    int domain_ = AF_INET;

    explicit tcp(int domain)
      : domain_{ domain }
    {}
};


} // namespace net::ip

#endif // BLOG_NET_IP_TCP_H