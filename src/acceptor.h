#ifndef BLOG_ACCEPTOR_H
#define BLOG_ACCEPTOR_H

#include <sys/socket.h>

#include "accept_awaiter.h"
#include "protocol.h"
#include "socket.h"

template<typename Protocol, typename Endpoint>
concept acceptable_protocol = is_same_protocol<Protocol, Endpoint> 
                           && connection_oriented_protocol<Protocol>;


template<typename Context, typename Protocol>
class BasicAcceptor: public BaseSocket<Context, Protocol> {
public:
    using socket_type = typename Protocol::socket;
    using endpoint_type = typename Protocol::endpoint;

    explicit BasicAcceptor(Context& context)
      : BaseSocket<Context, Protocol>{ context }
    {}

    BasicAcceptor(Context& context, const endpoint_type& endpoint)
      : BaseSocket<Context, Protocol>{ context }
    {
        this->option(typename Protocol::acceptor::reuse_address{ true });

        this->bind(endpoint);
        this->listen(DEFAULT_LISTEN_BACKLOG);
    }

    BasicAcceptor(Context& context, const endpoint_type& endpoint, bool reuse_port)
      : BaseSocket<Context, Protocol>{ context }
    {
        this->option(typename Protocol::acceptor::reuse_address{ true });

        if (reuse_port)
            this->option(typename Protocol::acceptor::reuse_port{ true });

        this->bind(endpoint);
        this->listen(DEFAULT_LISTEN_BACKLOG);
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
        auto len = endpoint.size();
        auto client = ::accept(this->native_handle(), endpoint.data(), &len);
        if (client == -1)
            return unexpected_system_error();

        endpoint.resize(len);
        
        return socket_type{ this->context(), client };
    }

    auto async_accept() noexcept -> AcceptAwaiter<Context, Protocol>
    {
        return AcceptAwaiter<Context, Protocol>{ context(), native_handle() };
    }

    auto async_accept(endpoint_type& endpoint) noexcept -> AcceptAwaiter<Context, Protocol>
    {
        return AcceptAwaiter<Context, Protocol>{ context(), native_handle(), &endpoint };
    }

    auto context() noexcept -> Context& 
    { 
        return this->context(); 
    }

    [[nodiscard]]
    constexpr auto native_handle() const noexcept -> int
    {
        return this->native_handle();
    }

private:
    static constexpr auto DEFAULT_LISTEN_BACKLOG = SOMAXCONN;
};

#endif // BLOG_ACCEPTOR_H