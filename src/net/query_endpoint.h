#ifndef BLOG_NET_QUERY_ENDPOINT_H
#define BLOG_NET_QUERY_ENDPOINT_H

#include <concepts>

#include <sys/socket.h>

#include <common/common.h>

namespace net {

namespace detail {

template<typename Endpoint>
auto query_local_endpoint(int fd) -> std::expected<Endpoint, std::error_code>
{
    Endpoint endpoint{};
    socklen_t addrlen = endpoint.capacity();

    if (::getsockname(fd, endpoint.data(), &addrlen) != 0)
        return unexpected_system_error();

    endpoint.resize(addrlen);
    return endpoint;
}

template<typename Endpoint>
auto query_remote_endpoint(int fd) -> std::expected<Endpoint, std::error_code>
{
    Endpoint endpoint{};
    socklen_t addrlen = endpoint.capacity();

    if (::getpeername(fd, endpoint.data(), &addrlen) != 0)
        return unexpected_system_error();

    endpoint.resize(addrlen);
    return endpoint;
}

} // namespace detail

template<typename T>
concept QuerableSocket = requires(const T& t) {
    typename T::endpoint_type;
    { t.native_handle() } -> std::convertible_to<int>;
};

template<typename Derived>
auto local_endpoint(const Derived& socket) noexcept
    requires QuerableSocket<Derived>
{
    using Endpoint = typename Derived::endpoint_type;
    return detail::query_local_endpoint<Endpoint>(socket.native_handle());
}

template<typename Derived>
auto remote_endpoint(const Derived& socket) noexcept
    requires QuerableSocket<Derived>
{
    using Endpoint = typename Derived::endpoint_type;
    return detail::query_remote_endpoint<Endpoint>(socket.native_handle());
}

template<typename Derived>
struct QueryLocalEndpoint {
    template<typename U>
    friend auto local_endpoint(const U& socket) noexcept
        requires QuerableSocket<U>;
};

template<typename Derived>
struct QueryRemoteEndpoint {
    template<typename U>
    friend auto remote_endpoint(const U& socket) noexcept
        requires QuerableSocket<U>;
};

} // namespace net

#endif // BLOG_NET_QUERY_ENDPOINT_H