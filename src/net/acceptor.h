#ifndef BLOG_NET_ACCEPTOR_H
#define BLOG_NET_ACCEPTOR_H

#include <sys/socket.h>

#include <common/common.h>

#include "acceptor_awaiter.h"
#include "option.h"
#include "socket.h"

namespace net {

template<typename Protocol>
class BasicAcceptor : public BasicSocket<Protocol> {
private:
    static constexpr auto MAX_LISTEN_CONNECTIONS = SOMAXCONN;

public:
    using socket_type = typename Protocol::socket;
    using endpoint_type = typename Protocol::endpoint;
    using base_type = BasicSocket<Protocol>;

    using reuse_address = BooleanOption<SOL_SOCKET, SO_REUSEADDR>;

    using reuse_port = BooleanOption<SOL_SOCKET, SO_REUSEPORT>;

    BasicAcceptor() = default;

    BasicAcceptor(const endpoint_type& endpoint, bool enable_reuse_port = false)
      : base_type{ endpoint.protocol() }
    {
        this->option(reuse_address{ true });

        if (enable_reuse_port)
            this->option(reuse_port{ true });

        this->bind(endpoint);
        listen(MAX_LISTEN_CONNECTIONS);
    }

    void listen(int backlog = MAX_LISTEN_CONNECTIONS)
    {
        auto res = ::listen(this->native_handle(), backlog);
        if (res == -1)
            throw_system_error("Failed to listen on socket");
    }

    auto async_accept() noexcept -> AcceptAwaiter<Protocol>
    {
        return AcceptAwaiter<Protocol>{ native_handle() };
    }

    auto async_accept(endpoint_type& endpoint) noexcept -> AcceptAwaiter<Protocol>
    {
        return AcceptAwaiter<Protocol>{ native_handle(), &endpoint };
    }

    [[nodiscard]]
    constexpr auto native_handle() const noexcept -> int
    {
        return base_type::native_handle();
    }
};

} // namespace net

#endif // BLOG_NET_ACCEPTOR_H