#ifndef BLOG_IP_ENDPOINT_H
#define BLOG_IP_ENDPOINT_H

#include <cstring>
#include <ostream>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "address.h"

namespace ip {

template<typename Protocol>
class BasicEndpoint {
public:
    using protocol_type = Protocol;

    using address_type = Address;

    BasicEndpoint()
    {
        std::memset(&data_, 0, sizeof(data_));
        data_.storage.ss_family = AF_INET;
    }

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

    [[nodiscard]]
    auto address() const noexcept -> Address
    {
        if (data_.storage.ss_family == AF_INET)
            return Address{ AddressV4::from_addr(data_.v4.sin_addr) };

        return Address{ AddressV6::from_addr(data_.v6.sin6_addr) };
    }

    [[nodiscard]]
    auto port() const noexcept -> in_port_t
    {
        if (data_.storage.ss_family == AF_INET)
            return ::ntohs(data_.v4.sin_port);

        return ::ntohs(data_.v6.sin6_port);
    }

    auto data() noexcept -> sockaddr*
    {
        return reinterpret_cast<sockaddr*>(&data_);
    }

    [[nodiscard]]
    auto data() const noexcept -> const sockaddr*
    {
        return reinterpret_cast<const sockaddr*>(&data_);
    }

    [[nodiscard]]
    constexpr auto capacity() const noexcept -> socklen_t
    {
        return sizeof(sockaddr_storage);
    }

    [[nodiscard]]
    constexpr auto size() const noexcept -> socklen_t
    {
        if (data_.storage.ss_family == AF_INET)
            return sizeof(sockaddr_in);

        return sizeof(sockaddr_in6);
    }

    void resize(socklen_t new_size) noexcept
    {
        // Endpoint size is fixed by protocol, ignore resize requests.
    }

    auto protocol() const noexcept -> protocol_type
    {
        if (data_.storage.ss_family == AF_INET)
            return protocol_type::v4();

        return protocol_type::v6();
    }

    static auto from_string(std::string_view address, in_port_t port) -> BasicEndpoint
    {
        return BasicEndpoint{ address_type::from_string(address), port };
    }

private:
    union AddressType {
        sockaddr_storage storage; // 确保足够大以容纳任何地址类型
        sockaddr_in v4;
        sockaddr_in6 v6;
    } data_;
};

template<typename Protocol>
auto operator<<(std::ostream& os, const BasicEndpoint<Protocol>& endpoint) -> std::ostream&
{
    return os << endpoint.address() << ":" << endpoint.port();
}

} // namespace ip

#endif // BLOG_IP_ENDPOINT_H