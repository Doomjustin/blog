#ifndef BLOG_NET_IP_DATAGRAM_SOCKET_H
#define BLOG_NET_IP_DATAGRAM_SOCKET_H

#include <netinet/in.h>
#include <sys/socket.h>

#include "async/io_context.h"
#include "async/this_coroutine.h"
#include "common/operations.h"
#include "net/base_socket.h"

namespace net::ip {

/**
 * @brief UDP-style datagram socket with sync and async I/O.
 *
 * Extends `BaseSocket` with datagram operations: addressed send/receive,
 * optional connection to a default peer, and io_uring-backed
 * async read/write.
 *
 * @tparam Protocol Protocol type satisfying `socket_protocol`.
 * @tparam Context  Execution context type (must provide `sqe()`).
 */
template<socket_protocol Protocol>
class DatagramSocket: public BaseSocket<Protocol> {
public:
    using base_socket_type = BaseSocket<Protocol>;
    using endpoint_type = typename Protocol::endpoint;
    using context_type = async::IOContext;

    explicit DatagramSocket(context_type& context = async::this_coroutine::context())
      : base_socket_type{ context }
    {}

    DatagramSocket(const Protocol& protocol, context_type& context = async::this_coroutine::context())
      : base_socket_type{ protocol, context }
    {}

    DatagramSocket(int fd, context_type& context = async::this_coroutine::context())
      : base_socket_type{ fd, context }
    {}

    /**
     * @brief Send a datagram to a specific endpoint.
     *
     * @param buffer      Payload bytes to send.
     * @param destination Target endpoint.
     * @return Bytes sent or an error code.
     */
    auto send_to(std::span<const std::byte> buffer, const endpoint_type& destination) noexcept
        -> std::expected<std::size_t, std::error_code>
    {
        return operations::send_to(this->native_handle(), buffer, destination, 0);
    }

    /**
     * @brief Receive a datagram and capture the sender endpoint.
     *
     * @param buffer Destination byte span.
     * @param sender Output: populated with the sender's endpoint on success.
     * @return Bytes received or an error code.
     */
    auto receive_from(std::span<std::byte> buffer, endpoint_type& sender) noexcept
        -> std::expected<std::size_t, std::error_code>
    {
        return operations::receive_from(this->native_handle(), buffer, sender, 0);
    }

    /**
     * @brief Associate socket with a default peer for subsequent send/receive.
     *
     * After calling this, `read_some`/`write_some` can be used instead of
     * `receive_from`/`send_to`.
     *
     * @param peer Remote endpoint to connect to.
     * @throws std::system_error If `connect(2)` fails.
     */
    void connect(const endpoint_type& peer)
    {
        operations::connect(this->native_handle(), peer.data(), peer.size());
    }

    /**
     * @brief Receive from the connected peer (blocking).
     *
     * @param buffer Destination byte span.
     * @return Bytes read or an error code.
     */
    auto receive_some(std::span<std::byte> buffer) noexcept 
        -> std::expected<std::size_t, std::error_code>
    {
        // return operations::receive_some(this->native_handle(), buffer);
        // TODO:
    }

    /**
     * @brief Send to the connected peer (blocking).
     *
     * @param buffer Source byte span.
     * @return Bytes written or an error code.
     */
    auto send_some(std::span<const std::byte> buffer) noexcept 
        -> std::expected<std::size_t, std::error_code>
    {
        // return operations::send_some(this->native_handle(), buffer);
        // TODO:
    }
};

} // namespace net::ip

#endif // BLOG_NET_IP_DATAGRAM_SOCKET_H