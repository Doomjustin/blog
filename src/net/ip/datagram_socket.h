#ifndef BLOG_NET_IP_DATAGRAM_SOCKET_H
#define BLOG_NET_IP_DATAGRAM_SOCKET_H

#include <netinet/in.h>
#include <sys/socket.h>

#include <async/write_sequence_awaiter.h>

#include <async/async.h>
#include <net/base_socket.h>
#include <common/common.h>
#include <net/operations.h>
#include <net/query_endpoint.h>
#include <net/receive_awaiter.h>
#include <net/send_awaiter.h>
#include <net/send_zc_awaiter.h>
#include <net/zero_copy.h>

namespace net::ip {

/**
 * @brief UDP-style datagram socket with sync and async I/O.
 *
 * Extends `BaseSocket` with datagram operations: addressed send/receive,
 * optional connection to a default peer, and io_uring-backed
 * async send/receive.
 *
 * @tparam Protocol Protocol type satisfying `socket_protocol`.
 * @tparam Context  Execution context type (must provide `sqe()`).
 */
template<socket_protocol Protocol>
class DatagramSocket: public BaseSocket<Protocol>,
                      public QueryRemoteEndpoint<DatagramSocket<Protocol>> {
public:
    using base_socket_type = BaseSocket<Protocol>;
    using endpoint_type = typename Protocol::endpoint;
    using context_type = async::IOContext;

    /** @brief Deferred-open constructor; socket is not yet created. */
    explicit DatagramSocket(context_type& context = async::this_coroutine::context())
      : base_socket_type{ context }
    {}

    /** @brief Eagerly open a socket from a protocol descriptor. */
    DatagramSocket(const Protocol& protocol, context_type& context = async::this_coroutine::context())
      : base_socket_type{ protocol, context }
    {}

    /**
     * @brief Adopt an existing file descriptor.
     *
     * @param fd An already-opened datagram socket fd; ownership is transferred.
     */
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
        return net::operations::send_to(this->native_handle(), buffer, destination, 0);
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
        return net::operations::receive_from(this->native_handle(), buffer, sender, 0);
    }

    /**
     * @brief Associate socket with a default peer for subsequent send/receive.
     *
        * After calling this, `receive_some`/`send_some` can be used instead of
        * `receive_from`/`send_to`.
     *
     * @param peer Remote endpoint to connect to.
     * @throws std::system_error If `connect(2)` fails.
     */
    void connect(const endpoint_type& peer)
    {
        net::operations::connect(this->native_handle(), peer.data(), peer.size());
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
        return net::operations::receive(this->native_handle(), buffer);
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
        return net::operations::send(this->native_handle(), buffer);
    }

    /**
     * @brief Scatter-gather send via `writev(2)` (blocking).
     *
     * Writes multiple buffer spans in a single syscall without copying
     * them into a contiguous staging buffer first.
     *
     * @tparam Sequence A range type whose elements each model `const_buffer`.
     * @param sequence Range of buffer views to write in order.
     * @return Total bytes written or an error code.
     */
    template<async::sequence_buffer Sequence>
    auto send_some(const Sequence& sequence) noexcept 
        -> std::expected<std::size_t, std::error_code>
    {
        return net::operations::writev(this->native_handle(), sequence);
    }

    /**
     * @brief Suspend until a receive completes via io_uring.
     *
     * @param buffer Destination byte span.
     * @pre `buffer` must outlive the `co_await` expression.
     * @return Bytes read or an error code.
     */
    auto async_receive_some(std::span<std::byte> buffer) noexcept -> ReceiveAwaiter
    {
        return { this->context(), this->native_handle(), buffer };
    }

    /**
     * @brief Suspend until a send completes via io_uring.
     *
     * @param buffer Source byte span.
     * @pre `buffer` must outlive the `co_await` expression.
     * @return Bytes written or an error code.
     */
    auto async_send_some(std::span<const std::byte> buffer) noexcept -> SendAwaiter
    {
        return { this->context(), this->native_handle(), buffer };
    }

    /**
     * @brief Suspend until a zero-copy send completes via io_uring.
     *
     * Uses `IORING_OP_SEND_ZC`; the `ZeroCopyT` tag ensures the caller
     * acknowledges that the buffer must stay valid until the notif CQE.
     *
     * @param buffer Zero-copy-tagged source buffer.
     * @pre Buffer memory must outlive the second (notif) CQE.
     * @return Bytes sent or an error code.
     */
    auto async_send_some(const ZeroCopyT& buffer) noexcept -> SendZCAwaiter
    {
        return { this->context(), this->native_handle(), buffer.span };
    }

    /**
     * @brief Suspend until a scatter-gather write completes via io_uring.
     *
     * Issues a single `writev`-style operation over a sequence of buffers,
     * avoiding multiple round-trips for multi-part messages.
     *
     * @tparam Sequence A range type whose elements each model `const_buffer`.
     * @param sequence Range of buffer views to write in order.
     * @pre Each element span must outlive the `co_await` expression.
     * @return `WriteSequenceAwaiter` ready to be `co_await`-ed.
     */
    template<async::sequence_buffer Sequence>
    auto async_send_some(const Sequence& sequence) noexcept -> async::WriteSequenceAwaiter<Sequence>
    {
        return { this->context(), this->native_handle(), sequence };
    }
};

} // namespace net::ip

#endif // BLOG_NET_IP_DATAGRAM_SOCKET_H