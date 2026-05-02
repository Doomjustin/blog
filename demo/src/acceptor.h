#ifndef BLOG_ACCEPTOR_H
#define BLOG_ACCEPTOR_H

#include <sys/socket.h>

#include "accept_awaiter.h"
#include "socket.h"

/**
 * @brief Keep server-side accept behavior consistent across call sites.
 *
 * This type centralizes option ordering, bind/listen sequencing, and
 * sync/async accept error semantics so restart and scaling behavior is
 * predictable in production.
 *
 * @tparam Protocol Protocol type that defines endpoint and socket types.
 * @tparam Context I/O execution context type.
 */
template<typename Protocol, typename Context>
class BasicAcceptor: public BaseSocket<Protocol, Context> {
public:
    using socket_type = typename Protocol::template socket<Context>;

    using endpoint_type = typename Protocol::endpoint;

    using base_type = BaseSocket<Protocol, Context>;

    /**
     * @brief Improve restart resilience by mitigating `EADDRINUSE` after restarts.
     *
     * After crash/redeploy, old connections may remain in `TIME_WAIT`. Enabling
     * this option helps server processes rebind quickly instead of waiting for
     * kernel timeout windows to elapse.
     *
     * @pre Must be set before `bind()`.
     *
     * @code{.cpp}
     * acceptor.option(ip::tcp::acceptor<IOContext>::reuse_address{ true });
     * acceptor.bind(endpoint);
     * @endcode
     */
    using reuse_address = BooleanOption<SOL_SOCKET, SO_REUSEADDR>;

    /**
     * @brief Enable multi-worker load sharing on the same address/port.
     *
     * This option is useful when several workers need to bind one listening
     * port and rely on kernel-side connection distribution for horizontal
     * scaling on a single host.
     *
     * @pre Must be set before `bind()`.
     */
    using reuse_port = BooleanOption<SOL_SOCKET, SO_REUSEPORT>;

    /**
     * @brief Allow delayed policy configuration before opening the endpoint.
     *
     * Use this overload when bind target or option policy is decided later
     * (for example, runtime config reload or test fixture setup).
     *
     * @param context I/O context used by this acceptor.
     *
     * @note This constructor intentionally does not call `bind()`/`listen()`.
     */
    explicit BasicAcceptor(Context& context)
      : base_type{ context }
    {}

    /**
     * @brief Keep restart resilience while optionally enabling multi-worker scaling.
     *
     * This constructor keeps `reuse_address` default behavior and optionally
     * enables `reuse_port` for deployments with multiple listeners on one port.
     *
     * @param context I/O context used by this acceptor.
     * @param endpoint Local endpoint to bind.
     * @param enable_reuse_port Whether to enable `SO_REUSEPORT`.
     * @throws std::system_error If setting options, `bind`, or `listen` fails.
     *
    * @note This project targets Linux/io_uring, where `SO_REUSEPORT` is
    * expected to be available.
     */
    BasicAcceptor(Context& context, const endpoint_type& endpoint, bool enable_reuse_port = false)
      : base_type{ context, endpoint.protocol() }
    {
        this->option(reuse_address{ true });

        if (enable_reuse_port)
            this->option(reuse_port{ true });

        this->bind(endpoint);
        listen(MAX_LISTEN_CONNECTIONS);
    }

    BasicAcceptor(const BasicAcceptor&) = delete;
    auto operator=(const BasicAcceptor&) -> BasicAcceptor& = delete;

    BasicAcceptor(BasicAcceptor&& other) noexcept = default;
    auto operator=(BasicAcceptor&& other) noexcept -> BasicAcceptor& = default;

    ~BasicAcceptor() = default;

    /**
     * @brief Make backlog policy explicit at startup boundaries.
     *
     * Direct calls are useful when startup is split into stages and backlog
     * tuning is controlled by deployment configuration.
     *
     * @param backlog Maximum pending connection queue length.
     * @throws std::system_error If the underlying `listen(2)` call fails.
     */
    void listen(int backlog = MAX_LISTEN_CONNECTIONS)
    {
        auto res = ::listen(this->native_handle(), backlog);
        if (res == -1)
            throw_system_error("Failed to listen on socket");
    }

    /**
     * @brief Keep accept-loop failure policy explicit and exception-free.
     *
     * Returning `std::expected` allows callers to decide retry/backoff/stop
     * strategy locally without exception-based control flow.
     *
     * @code{.cpp}
     * IOContext context;
     * auto endpoint = ip::tcp::endpoint{ ip::AddressV6::loopback(), 12345 };
     * auto acceptor = ip::tcp::acceptor{ context, endpoint };
     *
     * auto client = acceptor.accept();
     * if (!client) {
     *     // Handle client.error()
     *     return;
     * }
     *
     * // Use *client as a connected socket
     * @endcode
     *
     * @return A connected socket on success, or an error code on failure.
     * @post On success, returned socket is ready for I/O.
     */
    auto accept() noexcept -> std::expected<socket_type, std::error_code>
    {
        auto client = ::accept(this->native_handle(), nullptr, nullptr);
        if (client == -1)
            return unexpected_system_error();

        return socket_type{ this->context(), client };
    }

    /**
     * @brief Capture peer metadata when admission logic depends on source address.
     *
     * Use this overload for ACL, audit logging, and routing decisions that need
     * endpoint data alongside the accepted socket.
     *
     * @param endpoint In/out peer endpoint buffer.
    * The input `capacity()` value is used as `accept(2)` address length. After
    * success, `resize(len)` is invoked; for current IP endpoints this is a
    * no-op because size is protocol-fixed, but other protocols may use it.
     * @return A connected socket on success, or an error code on failure.
     */
    auto accept(endpoint_type& endpoint) noexcept
        -> std::expected<socket_type, std::error_code>
    {
        auto len = endpoint.capacity();
        auto client = ::accept(this->native_handle(), endpoint.data(), &len);
        if (client == -1)
            return unexpected_system_error();

        endpoint.resize(len);

        return socket_type{ this->context(), client };
    }

    /**
     * @brief Avoid blocking while preserving explicit error policy in coroutines.
     *
     * Resume value remains `std::expected`, so async flows keep the same retry
     * strategy style as sync `accept()`.
     *
     * @code{.cpp}
     * auto serve_once(ip::tcp::acceptor<IOContext>& acceptor) -> Task<>
     * {
     *     auto client = co_await acceptor.async_accept();
     *     if (!client) {
     *         // Handle client.error()
     *         co_return;
     *     }
     *
     *     // Use *client as a connected socket
     * }
     * @endcode
     *
     * @return Awaiter representing one accept operation.
     * @post Coroutine resumes with a socket or error code.
     */
    auto async_accept() noexcept -> AcceptAwaiter<Protocol, Context>
    {
        return AcceptAwaiter<Protocol, Context>{ context(), native_handle() };
    }

    /**
     * @brief Combine async throughput with peer-aware admission decisions.
     *
     * This overload is useful when both non-blocking accept and peer endpoint
     * metadata are required in the same coroutine pipeline.
     *
     * @param endpoint In/out peer endpoint buffer.
     * The endpoint object must remain valid until awaiter completion. On
    * success, it is updated with the remote address; endpoint size handling
    * remains protocol-defined (IP endpoint keeps fixed size).
     * @return Awaiter representing one accept operation.
     */
    auto async_accept(endpoint_type& endpoint) noexcept -> AcceptAwaiter<Protocol, Context>
    {
        return AcceptAwaiter<Protocol, Context>{ context(), native_handle(), &endpoint };
    }

    /**
     * @brief Expose context access for orchestrated lifecycle coordination.
     *
     * Intended for advanced integration paths (shutdown control, scheduling,
     * shared context-level instrumentation).
     *
     * @return Reference to the associated context.
     */
    auto context() noexcept -> Context&
    {
        return base_type::context();
    }

    /**
     * @brief Expose native fd for low-level interop and diagnostics.
     *
     * Use this when integrating with OS APIs or tools that operate on raw
     * descriptors.
     *
     * @return Native socket handle.
     */
    [[nodiscard]]
    constexpr auto native_handle() const noexcept -> int
    {
        return base_type::native_handle();
    }

private:
    static constexpr auto MAX_LISTEN_CONNECTIONS = SOMAXCONN;
};

#endif // BLOG_ACCEPTOR_H
