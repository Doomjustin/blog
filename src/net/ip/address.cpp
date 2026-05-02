#include "address.h"

#include <utility>

#include <common.h>

namespace net::ip {

auto AddressV4::to_string() const -> std::string
{
    std::string buffer(INET_ADDRSTRLEN, '\0');
    if (::inet_ntop(AF_INET, &address, buffer.data(), INET_ADDRSTRLEN) == nullptr)
        throw_system_error("Failed to convert IPv4 address to string");

    return buffer;
}

auto AddressV4::operator==(const AddressV4& other) const noexcept -> bool
{
    return address.s_addr == other.address.s_addr;
}

auto AddressV4::operator<=>(const AddressV4& other) const noexcept -> std::strong_ordering
{
    return ::ntohl(address.s_addr) <=> ::ntohl(other.address.s_addr);
}

auto AddressV4::from_string(std::string_view address) -> AddressV4
{
    AddressV4 result;
    auto res = ::inet_pton(AF_INET, address.data(), &result.address);
    if (res != 1)
        throw_system_error("Failed to convert string to IPv4 address");

    return result;
}

auto AddressV4::from_addr(const in_addr& addr) -> AddressV4
{
    AddressV4 result;
    result.address.s_addr = addr.s_addr;
    return result;
}

auto AddressV6::to_string() const -> std::string
{
    std::string buffer(INET6_ADDRSTRLEN, '\0');
    if (::inet_ntop(AF_INET6, &address, buffer.data(), INET6_ADDRSTRLEN) == nullptr)
        throw_system_error("Failed to convert IPv6 address to string");

    return buffer;
}

auto AddressV6::operator==(const AddressV6& other) const noexcept -> bool
{
    auto res = std::memcmp(address.s6_addr, other.address.s6_addr, 16);
    return res == 0;
}

auto AddressV6::operator<=>(const AddressV6& other) const noexcept -> std::strong_ordering
{
    auto res = std::memcmp(address.s6_addr, other.address.s6_addr, 16);
    if (res < 0) return std::strong_ordering::less;
    if (res > 0) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

auto AddressV6::from_string(std::string_view address) -> AddressV6
{
    AddressV6 result;
    auto res = ::inet_pton(AF_INET6, address.data(), &result.address);
    if (res != 1)
        throw_system_error("Failed to convert string to IPv6 address");

    return result;
}

auto AddressV6::from_addr(const in6_addr& addr) -> AddressV6
{
    AddressV6 result;
    std::memcpy(result.address.s6_addr, addr.s6_addr, 16);
    return result;
}

auto Address::operator==(const Address& other) const noexcept -> bool
{
    return address_ == other.address_;
}

auto Address::operator<=>(const Address& other) const noexcept -> std::strong_ordering
{
    if (address_.index() != other.address_.index())
        return address_.index() <=> other.address_.index();

    if (std::holds_alternative<AddressV4>(address_))
        return std::get<AddressV4>(address_) <=> std::get<AddressV4>(other.address_);

    return std::get<AddressV6>(address_) <=> std::get<AddressV6>(other.address_);
}

auto Address::from_string(std::string_view address) -> Address
{
    AddressV4 ipv4{};
    if (::inet_pton(AF_INET, address.data(), &ipv4.address) == 1)
        return { ipv4 };

    AddressV6 ipv6{};
    if (::inet_pton(AF_INET6, address.data(), &ipv6.address) == 1)
        return { ipv6 };

    throw_system_error("Invalid IP address format");
    std::unreachable();
}


auto operator<<(std::ostream& os, const Address& address) -> std::ostream&
{
    os << address.to_string();
    return os;
}

} // namespace net::ip