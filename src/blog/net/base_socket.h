#ifndef BLOG_NET_BASE_SOCKET_H
#define BLOG_NET_BASE_SOCKET_H

#include <concepts>
#include <utility>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "async/io_context.h"
#include "common/exceptions.h"
#include "linger.h"
#include "option.h"

namespace net {

/**
 * @brief Constrain Protocol to a minimal socket contract.
 *
 * This concept prevents template misuse early by requiring `endpoint`
 * and OS-level socket descriptors (`domain/type/protocol`) in one place.
 */
template <typename T>
concept socket_protocol = requires (const T& t)
{
    typename T::endpoint;

    { t.domain() } -> std::convertible_to<int>;
    { t.type() } -> std::convertible_to<int>;
    { t.protocol() } -> std::convertible_to<int>;
};

/**
 * @brief Provide shared socket lifecycle and option management primitives.
 *
 * This base type centralizes fd ownership, move-only semantics, and option
 * access so higher-level socket types can focus on I/O behavior.
 *
 * @tparam Protocol Socket protocol type satisfying `socket_protocol`.
 * @tparam IOContext Execution context type associated with the socket.
 */
template<socket_protocol Protocol>
class BaseSocket {
public:
    using context_type = async::IOContext;
    using protocol_type = Protocol;
    using endpoint_type = typename Protocol::endpoint;

    /**
     * @brief Read and clear pending socket error state after NET/poll events.
     *
     * Useful when operation failures are reported indirectly and you need the
     * concrete kernel error from `SO_ERROR` for diagnostics or recovery policy.
     */
    using error = BooleanOption<SOL_SOCKET, SO_ERROR>;

    /**
     * @brief Tune receive throughput/latency tradeoff via kernel RX buffer size.
     *
     * Larger values can reduce packet drops under burst load; smaller values
     * can reduce memory footprint and tail latency in constrained workloads.
     */
    using receive_buffer_size = ValueOption<SOL_SOCKET, SO_RCVBUF>;

    /**
     * @brief Tune send buffering behavior for bandwidth vs memory usage.
     *
     * Increase for high-throughput streaming, decrease when you need tighter
     * backpressure and more predictable per-connection memory cost.
     */
    using send_buffer_size = ValueOption<SOL_SOCKET, SO_SNDBUF>;

    /**
     * @brief Control close behavior when unsent data is still pending.
     *
     * `linger` helps choose between fast close and graceful drain, depending
     * on reliability requirements during shutdown.
     */
    using linger = LingerOption;

    /**
     * @brief Enable non-blocking mode for event-loop/coroutine driven I/O.
     *
     * This is typically required before integrating fd operations with poll/
     * epoll/io_uring style schedulers.
     */
    using non_blocking = FlagOption<F_GETFL, F_SETFL, O_NONBLOCK>;

    /**
     * @brief Prevent descriptor leakage across `exec` boundaries.
     *
     * Enable this in process-spawning environments to avoid unintentionally
     * inheriting socket fds in child executables.
     */
    using close_on_exec = FlagOption<F_GETFD, F_SETFD, FD_CLOEXEC>;

    /**
     * @brief Delay socket creation until runtime policy is finalized.
     *
     * Use this when open/bind/option order is configured later.
     *
     * @param context Execution context associated with this socket.
     */
    explicit BaseSocket(context_type& context)
      : context_{ &context }
    {}

    /**
     * @brief Open socket immediately from protocol metadata.
     *
     * This constructor is useful for eager initialization paths where the
     * protocol is known at construction time.
     *
     * @param context Execution context associated with this socket.
     * @param protocol Protocol used to create the native socket.
     * @throws std::system_error If `socket(2)` fails.
     */
    BaseSocket(const Protocol& protocol, context_type& context)
      : context_{ &context },
        fd_{ create(protocol) }
    {}

    BaseSocket(const BaseSocket&) = delete;
    auto operator=(const BaseSocket&) -> BaseSocket& = delete;

    /**
     * @brief Transfer fd ownership without duplicating kernel resources.
     *
     * This keeps socket wrappers move-only, which prevents accidental double
     * close and makes ownership handoff explicit in NET pipelines.
     *
     * @param other Source socket wrapper.
     * @post This object owns the previous fd/context from other.
     * @post other becomes empty (invalid fd, null context pointer).
     */
    BaseSocket(BaseSocket&& other) noexcept
      : context_{ std::exchange(other.context_, nullptr) },
        fd_{ std::exchange(other.fd_, INVALID_SOCKET) }
    {}

    /**
     * @brief Rebind ownership to another socket's resources safely.
     *
     * Current fd is closed first, then ownership is moved from source.
     * Self-assignment is handled as a no-op.
     *
     * @param other Source socket wrapper.
     * @return Reference to this object.
     * @post This object owns the previous fd/context from other.
     * @post other becomes empty (invalid fd, null context pointer).
     */
    auto operator=(BaseSocket&& other) noexcept -> BaseSocket&
    {
        if (this == &other) return *this;

        close();

        context_ = std::exchange(other.context_, nullptr);
        fd_ = std::exchange(other.fd_, INVALID_SOCKET);
        return *this;
    }

    /**
     * @brief Ensure fd cleanup on scope exit.
     *
     * Close errors are intentionally not thrown from destructor.
     */
    virtual ~BaseSocket()
    {
        close();
    }

    /**
     * @brief Guard operations that require an opened fd.
     *
     * @return `true` when native fd is valid.
     */
    [[nodiscard]]
    constexpr auto is_valid() const noexcept -> bool
    {
        return fd_ != INVALID_SOCKET;
    }

    /**
     * @brief Open the socket explicitly when using delayed initialization.
     *
     * This keeps creation timing under caller control.
     *
     * @param protocol Protocol used to create the native socket.
     * @throws std::runtime_error If already opened.
     * @throws std::system_error If `socket(2)` fails.
     */
    void open(const Protocol& protocol)
    {
        if (is_valid())
            throw std::runtime_error{ "Socket is already open" };

        fd_ = create(protocol);
    }

    /**
     * @brief Attach the socket to a local endpoint before listening/connecting.
     *
     * @param endpoint Local endpoint to bind.
     * @throws std::system_error If `bind(2)` fails.
     * @pre Socket must be opened.
     */
    void bind(const endpoint_type& endpoint)
    {
        assert(is_valid());

        auto res = ::bind(this->native_handle(), endpoint.data(), endpoint.size());
        if (res == -1)
            throw_system_error("Failed to bind socket");
    }

    /**
     * @brief Provide non-throwing shutdown/cleanup path.
     *
     * Returning `std::expected` lets callers decide whether close failures
     * should be logged, retried, or ignored.
     *
     * @return Empty success or error code from `close(2)`.
     * @post Native fd becomes invalid even when `close(2)` reports an error.
     */
    auto close() noexcept -> std::expected<void, std::error_code>
    {
        if (is_valid()) {
            auto res = ::close(fd_);
            fd_ = INVALID_SOCKET;

            if (res == -1)
                return unexpected_system_error();
        }

        return {};
    }

    /**
     * @brief Expose raw fd for low-level interop.
     *
     * @return Native socket file descriptor.
     */
    [[nodiscard]]
    constexpr auto native_handle() const noexcept -> int
    {
        return fd_;
    }

    /**
     * @brief Apply socket options where ordering affects behavior.
     *
     * Typical use includes options that must be set before `bind()`.
     *
     * @tparam Option Option type modeled by `socket_option`.
     * @param value Option value to set.
     * @throws std::system_error If `setsockopt(2)` fails.
     */
    template<socket_option Option>
    void option(const Option& value)
    {
        auto res = ::setsockopt(fd_, Option::level, Option::name, value.data(), value.size());
        if (res == -1)
            throw_system_error("Failed to set socket option[{}]", Option::name);
    }

    /**
     * @brief Read effective socket option values for diagnostics/policy checks.
     *
     * @tparam Option Option type modeled by `socket_option`.
     * @return Current option value from kernel.
     * @throws std::system_error If `getsockopt(2)` fails.
     * @throws std::runtime_error If returned option size mismatches expectation.
     */
    template<socket_option Option>
    auto option() const -> Option
    {
        Option option{};
        auto size = static_cast<socklen_t>(option.size());
        auto res = ::getsockopt(fd_, Option::level, Option::name, option.data(), &size);
        if (res == -1)
            throw_system_error("Failed to get socket option[{}]", Option::name);

        if (size != option.size())
            throw std::runtime_error{ "Unexpected socket option size" };

        return option;
    }

    /**
     * @brief Toggle fd flags in a read-modify-write safe pattern.
     *
     * @tparam Option Flag option type modeled by `flag_option`.
     * @param value Whether target bit should be enabled.
     * @throws std::system_error If `fcntl(2)` get/set fails.
     */
    template<flag_option Option>
    void option(const Option& value)
    {
        auto current_flags = ::fcntl(fd_, Option::get_cmd);
        if (current_flags == -1)
            throw_system_error("Failed to get socket flags");

        auto new_flags = value ? (current_flags | Option::bit) : (current_flags & ~Option::bit);
        if (::fcntl(fd_, Option::set_cmd, new_flags) == -1)
            throw_system_error("Failed to set socket flags");
    }

    /**
     * @brief Query current state of fd flag options.
     *
     * @tparam Option Flag option type modeled by `flag_option`.
     * @return Flag wrapper reflecting current kernel bit state.
     * @throws std::system_error If `fcntl(2)` get fails.
     */
    template<flag_option Option>
    auto option() const -> Option
    {
        auto current_flags = ::fcntl(fd_, Option::get_cmd);
        if (current_flags == -1)
            throw_system_error("Failed to get socket flags");

        if (current_flags & Option::bit)
            return Option{ true };

        return Option{ false };
    }

    /**
     * @brief Access associated execution context for orchestration.
     *
     * @return Mutable context reference.
     */
    [[nodiscard]]
    auto context() noexcept -> context_type&
    {
        return *context_;
    }

    /**
     * @brief Read-only context access for const call paths.
     *
     * @return Const context reference.
     */
    [[nodiscard]]
    auto context() const noexcept -> const context_type&
    {
        return *context_;
    }

protected:
    /**
     * @brief Adopt an existing native fd under managed ownership.
     *
     * Used by derived types that receive fd from accept/connect-like paths.
     *
     * @param context Execution context associated with this socket.
     * @param fd Native socket file descriptor to own.
     */
    BaseSocket(int fd, context_type& context)
      : context_{ &context },
        fd_{ fd }
    {}

private:
    static constexpr int INVALID_SOCKET = -1;

    context_type* context_{ nullptr };
    int fd_ = INVALID_SOCKET;

    static auto create(const Protocol& protocol) -> int
    {
        auto res = ::socket(protocol.domain(), protocol.type(), protocol.protocol());
        if (res == -1)
            throw_system_error("Failed to create socket");

        return res;
    }
};

} // namespace NET

#endif // BLOG_NET_BASE_SOCKET_H