#ifndef BLOG_IP_ENDPOINT_H
#define BLOG_IP_ENDPOINT_H

#include <arpa/inet.h>
#include <netinet/in.h>

#include "address.h"
#include "protocol.h"

namespace ip {

template<int Domain>
struct AddressTraits;

template<>
struct AddressTraits<domain::ipv4> {
    using address_type = AddressV4;
    using port_type = in_port_t;
    using endpoint_type = sockaddr_in;

    static auto cast(const address_type& address, port_type port) -> endpoint_type
    {
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_port = ::htons(port);
        endpoint.sin_addr = address.address;    
        return endpoint;
    }

    static auto address(const endpoint_type& endpoint) -> address_type
    {
        address_type address{};
        address.address = endpoint.sin_addr;
        return address;
    }

    static auto port(const endpoint_type& endpoint) -> port_type
    {
        return ::ntohs(endpoint.sin_port);
    }
};

template<>
struct AddressTraits<domain::ipv6> {
    using address_type = AddressV6;
    using port_type = in_port_t;
    using endpoint_type = sockaddr_in6;

    static auto cast(const address_type& address, port_type port) -> endpoint_type
    {
        sockaddr_in6 endpoint{};
        endpoint.sin6_family = AF_INET6;
        endpoint.sin6_port = ::htons(port);
        endpoint.sin6_addr = address.address;
        return endpoint;
    }

    static auto address(const endpoint_type& endpoint) -> address_type
    {
        address_type address{};
        address.address = endpoint.sin6_addr;
        return address;
    }

    static auto port(const endpoint_type& endpoint) -> port_type
    {
        return ::ntohs(endpoint.sin6_port);
    }
};


template<typename Protocol>
class BasicEndpoint {
public:
    using protocol_type = Protocol;

    static constexpr auto domain = Protocol::domain;

    using address_type = typename AddressTraits<domain>::address_type;

    using port_type = typename AddressTraits<domain>::port_type;

    using endpoint_type = typename AddressTraits<domain>::endpoint_type;

    using traits = AddressTraits<domain>;

    BasicEndpoint() = default;

    BasicEndpoint(const address_type& address, port_type port)
      : endpoint_{ traits::cast(address, port) }
    {}

    auto address() const noexcept -> address_type
    {
        return traits::address(endpoint_);
    }

    auto port() const noexcept -> port_type
    {
        return traits::port(endpoint_);
    }

    auto data() noexcept -> sockaddr*
    {
        return reinterpret_cast<sockaddr*>(&endpoint_);
    }

    [[nodiscard]]
    auto data() const noexcept -> const sockaddr*
    {
        return reinterpret_cast<const sockaddr*>(&endpoint_);
    }

    [[nodiscard]]
    constexpr auto capacity() const noexcept -> socklen_t
    {
        return sizeof(endpoint_);
    }

    [[nodiscard]]
    constexpr auto size() const noexcept -> socklen_t
    {
        return sizeof(endpoint_);
    }

    void resize(socklen_t new_size) noexcept
    {
        // Endpoint size is fixed by protocol, ignore resize requests.
    }

    static auto from_string(std::string_view address, port_type port) -> BasicEndpoint
    {
        return BasicEndpoint{ address_type::from_string(address), port };
    }

    static auto loopback(port_type port) -> BasicEndpoint
    {
        return BasicEndpoint{ address_type::loopback(), port };
    }

    static auto any(port_type port) -> BasicEndpoint
    {
        return BasicEndpoint{ address_type::any(), port };
    }

private:
    endpoint_type endpoint_;
};

} // namespace ip

#endif // BLOG_IP_ENDPOINT_H