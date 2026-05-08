#ifndef BLOG_NET_IP_ENDPOINT_H
#define BLOG_NET_IP_ENDPOINT_H

#include <cstring>
#include <ostream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <net/ip/address.h>

namespace net::ip {

/**
 * @brief Protocol-aware endpoint that stores either an IPv4 or IPv6 socket address.
 *
 * Holds a `sockaddr_storage` union so the same type works for both
 * `AF_INET` and `AF_INET6` sockets. The active family is tracked via
 * `ss_family` and methods switch on it transparently.
 *
 * @tparam Protocol Protocol type that provides `v4()` and `v6()` factories.
 */
template<typename Protocol>
class BasicEndpoint {
public:
    using protocol_type = Protocol;
    using address_type = Address;

    /**
     * @brief Construct a default endpoint for IPv4 with unspecified address and port 0.
     */
    BasicEndpoint()
    {
        std::memset(&data_, 0, sizeof(data_));
        data_.storage.ss_family = AF_INET;
    }

    /**
     * @brief Construct an any-address endpoint for the given protocol and port.
     *
     * Binds to `INADDR_ANY` (IPv4) or `in6addr_any` (IPv6) which accepts
     * connections on all network interfaces.
     *
     * @param protocol Protocol instance indicating the address family.
     * @param port     Port number in host byte order.
     */
    BasicEndpoint(const protocol_type& protocol, in_port_t port)
    {
        std::memset(&data_, 0, sizeof(data_));

        if (protocol.domain() == AF_INET) {
            data_.storage.ss_family = AF_INET;
            data_.v4.sin_family = AF_INET;
            data_.v4.sin_port = ::htons(port);
            data_.v4.sin_addr.s_addr = ::htonl(INADDR_ANY);
        }
        else {
            data_.storage.ss_family = AF_INET6;
            data_.v6.sin6_family = AF_INET6;
            data_.v6.sin6_port = ::htons(port);
            data_.v6.sin6_addr = in6addr_any;
        }
    }

    /**
     * @brief Construct an endpoint from an explicit address and port.
     *
     * @param address IP address (v4 or v6).
     * @param port    Port number in host byte order.
     */
    BasicEndpoint(const address_type& address, in_port_t port)
    {
        std::memset(&data_, 0, sizeof(data_));
        
        if (address.is_v4()) {
            data_.storage.ss_family = AF_INET;
            data_.v4.sin_family = AF_INET;
            data_.v4.sin_port = ::htons(port);
            data_.v4.sin_addr = address.to_v4().address;
        }
        else {
            data_.storage.ss_family = AF_INET6;
            data_.v6.sin6_family = AF_INET6;
            data_.v6.sin6_port = ::htons(port);
            data_.v6.sin6_addr = address.to_v6().address;
        }
    }

    /** @brief Extract the IP address from the stored sockaddr. */
    [[nodiscard]]
    auto address() const noexcept -> Address
    {
        if (data_.storage.ss_family == AF_INET)
            return Address{ AddressV4::from_addr(data_.v4.sin_addr) };

        return Address{ AddressV6::from_addr(data_.v6.sin6_addr) };
    }

    /** @brief Extract the port number in host byte order. */
    [[nodiscard]]
    auto port() const noexcept -> in_port_t
    {
        if (data_.storage.ss_family == AF_INET)
            return ::ntohs(data_.v4.sin_port);

        return ::ntohs(data_.v6.sin6_port);
    }

    /** @brief Raw `sockaddr*` pointer for POSIX socket APIs that write address data. */
    auto data() noexcept -> sockaddr*
    {
        return reinterpret_cast<sockaddr*>(&data_);
    }

    /** @brief Raw `const sockaddr*` pointer for POSIX socket APIs that read address data. */
    [[nodiscard]]
    auto data() const noexcept -> const sockaddr*
    {
        return reinterpret_cast<const sockaddr*>(&data_);
    }

    /**
     * @brief Maximum storable address size, always `sizeof(sockaddr_storage)`.
     *
     * Passed to accept/recvfrom as the input `addrlen` so the kernel knows
     * how much space is available to write the peer address.
     */
    [[nodiscard]]
    constexpr auto capacity() const noexcept -> socklen_t
    {
        return sizeof(sockaddr_storage);
    }

    /**
     * @brief Actual address structure size for the current address family.
     *
     * Returns `sizeof(sockaddr_in)` for IPv4 or `sizeof(sockaddr_in6)` for IPv6.
     * Used as the outgoing `addrlen` in connect/bind/sendto.
     */
    [[nodiscard]]
    constexpr auto size() const noexcept -> socklen_t
    {
        if (data_.storage.ss_family == AF_INET)
            return sizeof(sockaddr_in);

        return sizeof(sockaddr_in6);
    }

    /**
     * @brief No-op; endpoint size is determined by address family, not by kernel output.
     *
     * Present to satisfy the `writable_endpoint` concept used by `recvfrom`-style
     * APIs that resize the endpoint after the kernel writes the actual address length.
     */
    void resize(socklen_t new_size) noexcept
    {
        // Endpoint size is fixed by protocol, ignore resize requests.
    }

    /** @brief Infer the protocol instance from the stored address family. */
    auto protocol() const noexcept -> protocol_type
    {
        if (data_.storage.ss_family == AF_INET)
            return protocol_type::v4();

        return protocol_type::v6();
    }

    /**
     * @brief Parse an address string and construct with the given port.
     *
     * @param address IP address string (dotted-decimal or colon-hex).
     * @param port    Port number in host byte order.
     * @throws std::system_error If address parsing fails.
     */
    static auto from_string(std::string_view address, in_port_t port) -> BasicEndpoint
    {
        return BasicEndpoint{ address_type::from_string(address), port };
    }

private:
    union AddressType {
        sockaddr_storage storage; // Large enough to hold any address family.
        sockaddr_in v4;
        sockaddr_in6 v6;
    } data_;
};

/** @brief Format endpoint as `address:port` for logging and diagnostics. */
template<typename Protocol>
auto operator<<(std::ostream& os, const BasicEndpoint<Protocol>& endpoint) -> std::ostream&
{
    return os << endpoint.address() << ":" << endpoint.port();
}

} // namespace net::ip

#endif // BLOG_NET_IP_ENDPOINT_H