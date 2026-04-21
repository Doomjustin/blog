#ifndef BLOG_IP_TCP_H
#define BLOG_IP_TCP_H

#include <netinet/in.h>

#include "acceptor.h"
#include "endpoint.h"
#include "stream_socket.h"

namespace ip {

class tcp {
public:
    using address = Address;

    using endpoint = BasicEndpoint<tcp>;

    template<typename Context>
    using socket = StreamSocket<tcp, Context>;

    template<typename Context>
    using acceptor = BasicAcceptor<tcp, Context>;

    tcp() = default;

    [[nodiscard]]
    constexpr auto domain() const noexcept -> int
    {
        return domain_;
    }

    [[nodiscard]]
    constexpr auto type() const noexcept -> int
    {
        return SOCK_STREAM;
    }

    [[nodiscard]]
    constexpr auto protocol() const noexcept -> int
    {
        return IPPROTO_TCP;
    }

    static auto v4() noexcept -> tcp
    {
        return tcp{ AF_INET };
    }

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

} // namespace ip

#endif // BLOG_IP_TCP_H