#ifndef BLOG_ACCEPTOR_H
#define BLOG_ACCEPTOR_H

#include <sys/socket.h>

#include "accept_awaiter.h"
#include "protocol.h"
#include "socket.h"

template<typename Protocol, typename Endpoint>
concept acceptable_protocol = is_same_protocol<Protocol, Endpoint>;


template<typename Protocol, typename Context>
class BasicAcceptor: public BaseSocket<Protocol, Context> {
public:
    using socket_type = typename Protocol::template socket<Context>;
    using endpoint_type = typename Protocol::endpoint;
    using base_type = BaseSocket<Protocol, Context>;

    using reuse_address = BooleanOption<SOL_SOCKET, SO_REUSEADDR>;
    
#ifdef SO_REUSEPORT
    using reuse_port = BooleanOption<SOL_SOCKET, SO_REUSEPORT>;
#endif

    explicit BasicAcceptor(Context& context, const Protocol& protocol = Protocol{})
      : base_type{ context, protocol }
    {}

    BasicAcceptor(Context& context, const endpoint_type& endpoint)
      : base_type{ context, endpoint.protocol() }
    {
        this->option(reuse_address{ true });

        bind(endpoint);
        listen(DEFAULT_LISTEN_BACKLOG);
    }

    BasicAcceptor(Context& context, const endpoint_type& endpoint, bool enable_reuse_port)
      : base_type{ context, endpoint.protocol() }
    {
        this->option(reuse_address{ true });

        if (enable_reuse_port)
            this->option(reuse_port{ true });

        bind(endpoint);
        listen(DEFAULT_LISTEN_BACKLOG);
    }


    void bind(const endpoint_type& endpoint)
        requires acceptable_protocol<Protocol, typename Protocol::endpoint>
    {
        auto res = ::bind(this->native_handle(), endpoint.data(), endpoint.size());
        if (res == -1)
            throw_system_error("Failed to bind socket");
    }

    void listen(int backlog = DEFAULT_LISTEN_BACKLOG)
    {
        auto res = ::listen(this->native_handle(), backlog);
        if (res == -1)
            throw_system_error("Failed to listen on socket");
    }

    auto accept() noexcept -> std::expected<socket_type, std::error_code>
    {
        auto client = ::accept(this->native_handle(), nullptr, nullptr);
        if (client == -1)
            return unexpected_system_error();

        return socket_type{ this->context(), client };
    }

    auto accept(endpoint_type& endpoint) noexcept 
        -> std::expected<socket_type, std::error_code>
        requires acceptable_protocol<Protocol, typename Protocol::endpoint>
    {
        auto len = endpoint.capacity();
        auto client = ::accept(this->native_handle(), endpoint.data(), &len);
        if (client == -1)
            return unexpected_system_error();

        endpoint.resize(len);
        
        return socket_type{ this->context(), client };
    }

    auto async_accept() noexcept -> AcceptAwaiter<Protocol, Context>
    {
        return AcceptAwaiter<Protocol, Context>{ context(), native_handle() };
    }

    auto async_accept(endpoint_type& endpoint) noexcept -> AcceptAwaiter<Protocol, Context>
        requires acceptable_protocol<Protocol, typename Protocol::endpoint>
    {
        return AcceptAwaiter<Protocol, Context>{ context(), native_handle(), &endpoint };
    }

    auto context() noexcept -> Context& 
    { 
        return base_type::context(); 
    }

    [[nodiscard]]
    constexpr auto native_handle() const noexcept -> int
    {
        return base_type::native_handle();
    }

private:
    static constexpr auto DEFAULT_LISTEN_BACKLOG = SOMAXCONN;
};

#endif // BLOG_ACCEPTOR_H