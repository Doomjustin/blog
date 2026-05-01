#ifndef BLOG_COMMON_OPERATIONS_H
#define BLOG_COMMON_OPERATIONS_H

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "exceptions.h"

/**
 * @brief Constrain endpoint types that can receive address data from kernel.
 *
 * Required by APIs such as `recvfrom`, `getsockname`, and `getpeername`
 * where the endpoint storage is written by the OS and then resized.
 */
template<typename T>
concept writable_endpoint = requires (T& endpoint, socklen_t len)
{
    { endpoint.data() } -> std::convertible_to<sockaddr*>;
    { endpoint.size() } -> std::convertible_to<socklen_t>;
    { endpoint.capacity() } -> std::convertible_to<socklen_t>;
    endpoint.resize(len);
};

/**
 * @brief Constrain endpoint types that expose a stable read-only sockaddr view.
 *
 * Suitable for destination endpoints passed to send-style operations.
 */
template<typename T>
concept mutable_endpoint = requires (const T& endpoint)
{
    { endpoint.data() } -> std::convertible_to<const sockaddr*>;
    { endpoint.size() } -> std::convertible_to<socklen_t>;
};

/**
 * @brief Constrain types that can be adapted to writable byte spans.
 */
template <typename T>
concept mutable_buffer = requires(T& t)
{
    { buffer(t) } -> std::same_as<std::span<std::byte>>;
};

/**
 * @brief Constrain types that can be adapted to read-only byte spans.
 */
template<typename T>
concept const_buffer = requires(const T& t)
{
    { buffer(t) } -> std::same_as<std::span<const std::byte>>;
};

/**
 * @brief Constrain ranges whose elements each model `const_buffer`.
 */
template <typename T>
concept sequence_buffer = std::ranges::range<T> && const_buffer<std::ranges::range_reference_t<T>>;


namespace operations {

/**
 * @brief Sentinel value returned by POSIX APIs on failure.
 */
inline constexpr int INVALID_RESULT = -1;

/**
 * @brief Shutdown directions forwarded to `shutdown(2)`.
 */
enum class ShutdownHow: std::uint8_t {
    read = SHUT_RD,
    write = SHUT_WR,
    read_write = SHUT_RDWR
};

/**
 * @brief Disable one or both directions of a full-duplex socket.
 *
 * @param socket Native socket fd.
 * @param how Shutdown direction.
 * @return Empty success or error code from `shutdown(2)`.
 */
auto shutdown(int socket, ShutdownHow how) noexcept -> std::expected<void, std::error_code>;

/**
 * @brief Establish a connection for stream-oriented sockets.
 *
 * @param socket Native socket fd.
 * @param addr Destination address.
 * @param addrlen Size of destination address.
 * @throws std::system_error If `connect(2)` fails.
 */
void connect(int socket, const sockaddr* addr, socklen_t addrlen);

/**
 * @brief Send up to `buffer.size_bytes()` bytes on a connected socket.
 *
 * @param socket Native socket fd.
 * @param buffer Source bytes.
 * @return Number of bytes written or an error code.
 */
auto send(int socket, std::span<const std::byte> buffer) -> std::expected<std::size_t, std::error_code>;

/**
 * @brief Receive up to `buffer.size_bytes()` bytes from a connected socket.
 *
 * @param socket Native socket fd.
 * @param buffer Destination bytes.
 * @return Number of bytes read (0 means peer shutdown) or an error code.
 */
auto receive(int socket, std::span<std::byte> buffer) -> std::expected<std::size_t, std::error_code>;

/**
 * @brief Gather-write multiple buffers with `writev(2)`.
 *
 * Handles EINTR retry and validates iovec count against platform limits.
 *
 * @tparam SequenceBuffer Range of buffer-like elements.
 * @param socket Native socket fd.
 * @param buffers Source buffer sequence.
 * @return Number of bytes written or an error code.
 */
template<sequence_buffer SequenceBuffer>
auto writev(int socket, const SequenceBuffer& buffers) noexcept
    -> std::expected<std::size_t, std::error_code>
{
    const auto n = std::ranges::size(buffers);
    if (n == 0) return 0;

    const long iov_max = ::sysconf(_SC_IOV_MAX);

    if (iov_max > 0 && static_cast<long>(n) > iov_max)
        return unexpected_system_error(std::errc::invalid_argument);

    if (n > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return unexpected_system_error(std::errc::invalid_argument);

    std::vector<::iovec> iov{};
    try {
        iov.reserve(n);
    }
    catch (...) {
        return unexpected_system_error(std::errc::not_enough_memory);
    }

    for (const auto& buffer : buffers) {
        iov.push_back(::iovec{
            .iov_base = const_cast<std::byte*>(buffer.data()),
            .iov_len = buffer.size_bytes()
        });
    }

    while (true) {
        const auto bytes_written = ::writev(socket, iov.data(), static_cast<int>(iov.size()));
        if (bytes_written == INVALID_RESULT) {
            if (errno == EINTR)
                continue;

            return unexpected_system_error();
        }

        return static_cast<std::size_t>(bytes_written);
    }
}

/**
 * @brief Send a datagram to a specific endpoint.
 *
 * @tparam Endpoint Endpoint type exposing const sockaddr view.
 * @param socket Native socket fd.
 * @param buffer Payload bytes.
 * @param destination Target endpoint.
 * @param flags Flags forwarded to `sendto(2)`.
 * @return Number of bytes sent or an error code.
 */
template<mutable_endpoint Endpoint>
auto send_to(int socket, std::span<const std::byte> buffer, const Endpoint& destination, int flags) noexcept
    -> std::expected<std::size_t, std::error_code>
{
    auto bytes_sent = ::sendto(socket,
                               buffer.data(),
                               buffer.size_bytes(),
                               flags,
                               destination.data(),
                               destination.size()
                        );

    if (bytes_sent == INVALID_RESULT)
        return unexpected_system_error();

    return static_cast<std::size_t>(bytes_sent);
}

/**
 * @brief Receive a datagram and capture sender endpoint.
 *
 * On success, `source` is resized to the actual address length returned by
 * the kernel.
 *
 * @tparam Endpoint Endpoint type with writable storage.
 * @param socket Native socket fd.
 * @param buffer Destination payload buffer.
 * @param source Output sender endpoint.
 * @param flags Flags forwarded to `recvfrom(2)`.
 * @return Number of bytes received or an error code.
 */
template<writable_endpoint Endpoint>
auto receive_from(int socket, std::span<std::byte> buffer, Endpoint& source, int flags) noexcept
    -> std::expected<std::size_t, std::error_code>
{
    socklen_t addrlen = source.capacity();
    auto bytes_received = ::recvfrom(socket,
                                     buffer.data(),
                                     buffer.size_bytes(),
                                     flags,
                                     source.data(),
                                     &addrlen
                                );

    if (bytes_received == INVALID_RESULT)
        return unexpected_system_error();

    source.resize(addrlen);
    return static_cast<std::size_t>(bytes_received);
}

/**
 * @brief Query peer endpoint of a connected socket.
 *
 * @tparam Endpoint Endpoint storage type.
 * @param socket Native socket fd.
 * @return Peer endpoint value or an error code.
 */
template<writable_endpoint Endpoint>
auto query_remote_endpoint(int socket) -> std::expected<Endpoint, std::error_code>
{
    Endpoint endpoint{};
    socklen_t addrlen = endpoint.capacity();

    if (::getpeername(socket, endpoint.data(), &addrlen) != 0)
        return unexpected_system_error();

    endpoint.resize(addrlen);
    return endpoint;
}

/**
 * @brief Query local endpoint bound to a socket.
 *
 * @tparam Endpoint Endpoint storage type.
 * @param socket Native socket fd.
 * @return Local endpoint value or an error code.
 */
template<writable_endpoint Endpoint>
auto query_local_endpoint(int socket) -> std::expected<Endpoint, std::error_code>
{
    Endpoint endpoint{};
    socklen_t addrlen = endpoint.capacity();

    if (::getsockname(socket, endpoint.data(), &addrlen) != 0)
        return unexpected_system_error();

    endpoint.resize(addrlen);
    return endpoint;
}

} // namespace operations

#endif // BLOG_COMMON_OPERATIONS_H 