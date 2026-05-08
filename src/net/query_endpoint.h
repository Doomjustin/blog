#ifndef BLOG_NET_QUERY_ENDPOINT_H
#define BLOG_NET_QUERY_ENDPOINT_H

#include <net/operations.h>

namespace net {

template<typename T>
concept QuerableSocket = requires (const T& t)
{
    typename T::endpoint_type;
    { t.native_handle() } -> std::convertible_to<int>;
};

template<typename Derived>
struct QueryLocalEndpoint {
    friend auto local_endpoint(const Derived& socket) noexcept
        requires QuerableSocket<Derived>
    {
        return operations::query_local_endpoint<typename Derived::endpoint_type>(socket.native_handle());
    }
};

template<typename Derived>
struct QueryRemoteEndpoint {
    friend auto remote_endpoint(const Derived& socket) noexcept
        requires QuerableSocket<Derived>
    {
        return operations::query_remote_endpoint<typename Derived::endpoint_type>(socket.native_handle());
    }
};

} // namespace net

#endif // BLOG_NET_QUERY_ENDPOINT_H