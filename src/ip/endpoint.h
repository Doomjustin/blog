#ifndef BLOG_IP_ENDPOINT_H
#define BLOG_IP_ENDPOINT_H

#include <arpa/inet.h>
#include <netinet/in.h>

#include "address.h"
#include "overloads.h"

namespace ip {

template<typename Protocol>
class BasicEndpoint {
public:
    using protocol_type = Protocol;
    
    using address_type = Address;

    using port_type = in_port_t;
    
    BasicEndpoint() = default;

    BasicEndpoint(const protocol_type& protocol, port_type port)
    {
        if (protocol.family() == AF_INET) {
            sockaddr_in endpoint{};
            endpoint.sin_family = AF_INET;
            endpoint.sin_port = ::htons(port);
            endpoint.sin_addr.s_addr = ::htonl(INADDR_ANY);
            endpoint_ = endpoint;
        }
        else {
            sockaddr_in6 endpoint{};
            endpoint.sin6_family = AF_INET6;
            endpoint.sin6_port = ::htons(port);
            endpoint.sin6_addr = in6addr_any;
            endpoint_ = endpoint;
        }
    }

    BasicEndpoint(const address_type& address, port_type port)
    {
        if (address.is_v4()) {
            sockaddr_in endpoint{};
            endpoint.sin_family = AF_INET;
            endpoint.sin_port = ::htons(port);
            endpoint.sin_addr = address.to_v4().address;
            endpoint_ = endpoint;
        }
        else {
            sockaddr_in6 endpoint{};
            endpoint.sin6_family = AF_INET6;
            endpoint.sin6_port = ::htons(port);
            endpoint.sin6_addr = address.to_v6().address;
            endpoint_ = endpoint;
        }
    }

    [[nodiscard]]
    auto address() const noexcept -> Address
    {
        return std::visit(Overload{
            [](const sockaddr_in& endpoint) -> Address 
            {
                AddressV4 address;
                address.address = endpoint.sin_addr;
                return Address{ address };
            },
            [](const sockaddr_in6& endpoint) -> Address 
            {
                AddressV6 address;
                address.address = endpoint.sin6_addr;
                return Address{ address };
            }
        }, endpoint_);
    }

    [[nodiscard]]
    auto port() const noexcept -> port_type
    {
        return std::visit(Overload{
            [](const sockaddr_in& endpoint) -> port_type 
            {
                return ::ntohs(endpoint.sin_port);
            },
            [](const sockaddr_in6& endpoint) -> port_type 
            {
                return ::ntohs(endpoint.sin6_port);
            }
        }, endpoint_);
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

    auto protocol() const noexcept -> protocol_type
    {
        if (std::holds_alternative<sockaddr_in>(endpoint_))
            return protocol_type::v4();

        return protocol_type::v6();
    }

    static auto from_string(std::string_view address, port_type port) -> BasicEndpoint
    {
        return BasicEndpoint{ address_type::from_string(address), port };
    }

private:
    std::variant<sockaddr_in, sockaddr_in6> endpoint_;
};

} // namespace ip

#endif // BLOG_IP_ENDPOINT_H