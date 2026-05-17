#ifndef BLOG_NET_IP_UDP_H
#define BLOG_NET_IP_UDP_H

#include <sys/socket.h>

#include <net/acceptor.h>
#include <net/ip/address.h>
#include <net/ip/datagram_socket.h>
#include <net/ip/endpoint.h>

namespace net::ip {

class udp {
private:
    int domain_ = AF_INET;

    explicit udp(int domain)
      : domain_{ domain }
    {}

public:
    using address = Address;
    using endpoint = BasicEndpoint<udp>;
    using socket = DatagramSocket<udp>;
    using acceptor = BasicAcceptor<udp>;

    udp() = default;

    [[nodiscard]]
    constexpr auto domain() const noexcept -> int
    {
        return domain_;
    }

    [[nodiscard]]
    consteval auto type() const noexcept -> int
    {
        return SOCK_DGRAM;
    }

    [[nodiscard]]
    consteval auto protocol() const noexcept -> int
    {
        return IPPROTO_UDP;
    }

    /** @brief Return a UDP/IPv4 protocol instance. */
    static auto v4() noexcept -> udp
    {
        return udp{ AF_INET };
    }

    /** @brief Return a UDP/IPv6 protocol instance. */
    static auto v6() noexcept -> udp
    {
        return udp{ AF_INET6 };
    }
};

} // namespace net::ip

#endif // BLOG_NET_IP_UDP_H