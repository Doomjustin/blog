#ifndef BLOG_NET_IP_STREAM_SOCKET_H
#define BLOG_NET_IP_STREAM_SOCKET_H

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "async/read_some_awaiter.h"
#include "async/receive_stream.h"
#include "async/this_coroutine.h"
#include "async/write_sequence_awaiter.h"
#include "async/write_some_awaiter.h"
#include "common/operations.h"
#include "net/base_socket.h"

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
class StreamSocket: public BaseSocket<Protocol> {
public:
    using base_socket_type = BaseSocket<Protocol>;
    using endpoint_type = typename Protocol::endpoint;
    using context_type = typename base_socket_type::context_type;

    /** @brief Alias for `ShutdownHow`; controls which direction to close. */
    using how = operations::ShutdownHow;

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

    StreamSocket(StreamSocket&&) = default;
    auto operator=(StreamSocket&&) -> StreamSocket& = default;

    ~StreamSocket() = default;

    /**
     * @brief Connect to a remote peer.
     *
     * @param peer Remote endpoint to connect to.
     * @throws std::system_error If `connect(2)` fails.
     */
    void connect(const endpoint_type& peer)
    {
        operations::connect(this->native_handle(), peer.data(), peer.size());
    }

    /**
     * @brief Shut down one or both directions of the stream.
     *
     * @param how Direction to shut down (`read`, `write`, or `read_write`).
     * @return Empty success or an error code from `shutdown(2)`.
     */
    auto shutdown(how how) noexcept -> std::expected<void, std::error_code>
    {
        return operations::shutdown(this->native_handle(), how);
    }

    /**
     * @brief Receive up to `buffer.size_bytes()` bytes (blocking).
     *
     * @param buffer Destination byte span.
     * @return Bytes read (0 means peer closed), or an error code.
     */
    auto read_some(std::span<std::byte> buffer) noexcept 
        -> std::expected<std::size_t, std::error_code>
    {
        return operations::read_some(this->native_handle(), buffer);
    }

    /**
     * @brief Send up to `buffer.size_bytes()` bytes (blocking).
     *
     * @param buffer Source byte span.
     * @return Bytes written or an error code.
     */
    auto write_some(std::span<const std::byte> buffer) noexcept 
        -> std::expected<std::size_t, std::error_code>
    {
        return operations::write_some(this->native_handle(), buffer);
    }

    /**
     * @brief Suspend until a receive completes via io_uring.
     *
     * @param buffer Destination byte span.
     * @pre `buffer` must outlive the `co_await` expression.
     * @return Bytes read or an error code.
     */
    auto async_read_some(std::span<std::byte> buffer) noexcept -> async::ReadSomeAwaiter
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
    auto async_write_some(std::span<const std::byte> buffer) noexcept -> async::WriteSomeAwaiter
    {
        return { this->context(), this->native_handle(), buffer };
    }

    /**
     * @brief Suspend until a gather-write completes via io_uring.
     *
     * Submits a vectored send operation backed by a sequence of immutable
     * buffers and resumes when the kernel reports completion.
     *
     * @tparam Buffer Buffer sequence type satisfying `sequence_buffer`.
     * @param buffer Sequence of source buffers written in order.
     * @pre All underlying buffer storage must outlive the `co_await` expression.
     * @return Bytes written or an error code.
     */
    template<sequence_buffer Buffer>
    auto async_write_some(const Buffer& buffer) noexcept
        -> async::WriteSequenceAwaiter<Buffer>
    {
        return async::WriteSequenceAwaiter<Buffer>{ this->context(), this->native_handle(), buffer };
    }

    auto receive_stream() -> async::ReceiveStream
    {
        auto default_bgid = this->context().default_buffer();
        if (!default_bgid)
            throw std::runtime_error{ "No default buffer ring available for receive stream" }; 

        return async::ReceiveStream{ this->context(), this->native_handle(), *default_bgid };
    }

    auto receive_stream(unsigned bgid) -> async::ReceiveStream
    {
        return async::ReceiveStream{ this->context(), this->native_handle(), bgid };
    }
};

} // namespace net::ip

#endif // BLOG_NET_IP_STREAM_SOCKET_H