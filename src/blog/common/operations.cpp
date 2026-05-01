#include "operations.h"

#include <utility>

namespace operations {

auto shutdown(int socket, ShutdownHow how) noexcept -> std::expected<void, std::error_code>
{
    if (::shutdown(socket, std::to_underlying(how)) == INVALID_RESULT)
        return unexpected_system_error();

    return {};
}

void connect(int socket, const sockaddr* addr, socklen_t addrlen)
{
    if (::connect(socket, addr, addrlen) != 0)
        throw_system_error("Failed to connect socket");
}

auto send(int socket, std::span<const std::byte> buffer)
    -> std::expected<std::size_t, std::error_code>
{
    auto bytes_written = ::send(socket, buffer.data(), buffer.size_bytes(), 0);
    if (bytes_written == INVALID_RESULT)
        return unexpected_system_error();

    return static_cast<std::size_t>(bytes_written);
}

auto receive(int socket, std::span<std::byte> buffer)
    -> std::expected<std::size_t, std::error_code>
{
    auto bytes_read = ::recv(socket, buffer.data(), buffer.size_bytes(), 0);
    if (bytes_read == INVALID_RESULT)
        return unexpected_system_error();

    return static_cast<std::size_t>(bytes_read);
}

} // namespace operations