#ifndef BLOG_NET_IP_STREAM_SOCKET_H
#define BLOG_NET_IP_STREAM_SOCKET_H

#include <type_traits>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "write_sequence_awaiter.h"

#include <async.h>
#include <base_socket.h>
#include <common.h>
#include <operations.h>
#include <query_endpoint.h>
#include <receive_awaiter.h>
#include <receive_stream.h>
#include <send_awaiter.h>
#include <send_zc_awaiter.h>
#include <zero_copy.h>

namespace net::ip {

/**
 * @brief TCP-style stream socket with sync and async I/O.
 *
 * Extends `BaseSocket` with stream-specific operations: connection
 * establishment, half-close shutdown, TCP keepalive options, and
 * both blocking and io_uring-backed read/write.
 *
 * @tparam Protocol Protocol type satisfying `socket_protocol`.
 */
template<typename Protocol>
class StreamSocket: public BaseSocket<Protocol>,
                    public QueryRemoteEndpoint<StreamSocket<Protocol>> {
public:
    using base_socket_type = BaseSocket<Protocol>;
    using endpoint_type = typename Protocol::endpoint;
    using context_type = typename base_socket_type::context_type;
    using is_stream_t = std::true_type;

    /** @brief Alias for `ShutdownHow`; controls which direction to close. */
    using how = net::operations::ShutdownHow;

    /** @brief Enable TCP keepalive probes to detect dead peers. */
    using keep_alive = BooleanOption<SOL_SOCKET, SO_KEEPALIVE>;

    /** @brief Idle time (seconds) before the first keepalive probe is sent. */
    using keep_alive_idle = ValueOption<IPPROTO_TCP, TCP_KEEPIDLE>;

    /** @brief Interval (seconds) between consecutive keepalive probes. */
    using keep_alive_interval = ValueOption<IPPROTO_TCP, TCP_KEEPINTVL>;

    /** @brief Maximum number of keepalive probes before declaring connection dead. */
    using keep_alive_count = ValueOption<IPPROTO_TCP, TCP_KEEPCNT>;
    
    /**
     * @brief Disable Nagle's algorithm to reduce per-packet latency.
     *
     * Enable for low-latency request-response protocols where small
     * writes must be sent immediately rather than coalesced.
     */
    using no_delay = BooleanOption<IPPROTO_TCP, TCP_NODELAY>;

    /**
     * @brief Deferred-open constructor; socket is not yet created.
     *
     * Use when the protocol or remote address is determined at runtime
     * and the socket needs to be explicitly opened later.
     */
    explicit StreamSocket(context_type& context = async::this_coroutine::context())
      : base_socket_type{ context }
    {}

    /**
     * @brief Eagerly open a socket from a protocol descriptor.
     *
     * Use when the protocol is known at construction time and the socket
     * should be ready for option-setting or `connect()` immediately.
     */
    StreamSocket(const Protocol& protocol, context_type& context = async::this_coroutine::context())
      : base_socket_type{ protocol, context }
    {}

    /**
     * @brief Adopt an existing file descriptor.
     *
     * Used by acceptors to wrap a kernel-assigned fd after `accept(2)`
     * without calling `socket(2)` again.
     *
     * @param fd An already-opened, connected socket fd.
     * @pre `fd` must be a valid socket; ownership is transferred to this object.
     */
    StreamSocket(int fd, context_type& context = async::this_coroutine::context())
      : base_socket_type{ fd, context }
    {}

    /**
     * @brief Connect to a remote peer.
     *
     * @param peer Remote endpoint to connect to.
     * @throws std::system_error If `connect(2)` fails.
     */
    void connect(const endpoint_type& peer)
    {
        net::operations::connect(this->native_handle(), peer.data(), peer.size());
    }

    /**
     * @brief Shut down one or both directions of the stream.
     *
     * @param how Direction to shut down (`read`, `write`, or `read_write`).
     * @return Empty success or an error code from `shutdown(2)`.
     */
    auto shutdown(how how) noexcept -> std::expected<void, std::error_code>
    {
        return net::operations::shutdown(this->native_handle(), how);
    }

    /**
     * @brief Receive up to `buffer.size_bytes()` bytes (blocking).
     *
     * @param buffer Destination byte span.
     * @return Bytes read (0 means peer closed), or an error code.
     */
    auto receive_some(std::span<std::byte> buffer) noexcept 
        -> std::expected<std::size_t, std::error_code>
    {
        return net::operations::receive(this->native_handle(), buffer);
    }

    /**
     * @brief Send up to `buffer.size_bytes()` bytes (blocking).
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

    /**
     * @brief Create a `ReceiveStream` using the context's default buffer ring.
     *
     * @throws std::runtime_error If no default buffer ring has been registered
     *         via `async::setup_buffer_ring()` for this thread.
     * @return `ReceiveStream` backed by the default buffer group.
     */
    auto receive_stream() -> ReceiveStream
    {
        auto default_bgid = this->context().default_buffer();
        if (!default_bgid)
            throw std::runtime_error{ "No default buffer ring available for receive stream" }; 

        return { this->context(), this->native_handle(), *default_bgid };
    }

    /**
     * @brief Create a `ReceiveStream` bound to a specific buffer group.
     *
     * @param bgid Buffer group ID returned by `async::setup_buffer_ring()`.
     * @return `ReceiveStream` backed by the specified buffer group.
     */
    auto receive_stream(unsigned bgid) -> ReceiveStream
    {
        return { this->context(), this->native_handle(), bgid };
    }
};

} // namespace net::ip

#endif // BLOG_NET_IP_STREAM_SOCKET_H