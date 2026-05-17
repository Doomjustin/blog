#ifndef BLOG_NET_IP_DATAGRAM_SOCKET_H
#define BLOG_NET_IP_DATAGRAM_SOCKET_H

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <net/receive_message_awaiter.h>
#include <net/send_message_awaiter.h>
#include <net/socket.h>

namespace net::ip {

template<typename Protocol>
class DatagramSocket : public BasicSocket<Protocol> {
public:
    using base_type = BasicSocket<Protocol>;
    using endpoint_type = typename Protocol::endpoint;

    DatagramSocket() = default;

    explicit DatagramSocket(const Protocol& protocol)
      : base_type{ protocol }
    {}

    explicit DatagramSocket(int fd)
      : base_type{ fd }
    {}

    auto async_send_some_to(const endpoint_type& peer, std::span<const std::byte> buffer)
        -> SendMessageAwaiter<Protocol>
    {
        return SendMessageAwaiter<Protocol>{ this->fd(), buffer };
    }

    template<std::ranges::contiguous_range T>
    auto async_send_some_to(const endpoint_type& peer, const T& range)
        -> SendMessageAwaiter<Protocol>
    {
        return async_send_some_to(peer, async::buffer(range));
    }

    auto async_receive_some_from(std::span<std::byte> buffer) -> ReceiveMessageAwaiter<Protocol>
    {
        return ReceiveMessageAwaiter<Protocol>{ this->fd(), buffer };
    }

    template<std::ranges::contiguous_range T>
    auto async_receive_some_from(T& range) -> ReceiveMessageAwaiter<Protocol>
    {
        return async_receive_some_from(async::buffer(range));
    }

    auto async_receive_some_from(std::span<std::byte> buffer, endpoint_type& sender)
        -> ReceiveMessageAwaiter<Protocol>
    {
        return ReceiveMessageAwaiter<Protocol>{ this->fd(), buffer, &sender };
    }

    template<std::ranges::contiguous_range T>
    auto async_receive_from(T& range, endpoint_type& sender) -> ReceiveMessageAwaiter<Protocol>
    {
        return async_receive_some_from(async::buffer(range), sender);
    }
};

} // namespace net::ip

#endif // BLOG_NET_IP_DATAGRAM_SOCKET_H