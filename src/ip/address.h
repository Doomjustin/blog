#ifndef BLOG_IP_ADDRESS_H
#define BLOG_IP_ADDRESS_H

#include <array>
#include <compare>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "exceptions.h"

namespace ip {

struct AddressV4 {
    using byte_type = std::array<std::uint8_t, 4>;
    using address_type = in_addr;

    address_type address{};

    AddressV4() = default;

    constexpr AddressV4(const byte_type& bytes)
    {
        address.s_addr = std::bit_cast<std::uint32_t>(bytes);
    }

    [[nodiscard]] 
    auto to_string() const -> std::string
    {
        std::string buffer(INET_ADDRSTRLEN, '\0');
        if (::inet_ntop(AF_INET, &address, buffer.data(), INET_ADDRSTRLEN) == nullptr)
            throw_system_error("Failed to convert IPv4 address to string");

        return buffer;
    }

    auto operator==(const AddressV4& other) const noexcept -> bool
    {
        return address.s_addr == other.address.s_addr;
    }

    auto operator<=>(const AddressV4& other) const noexcept -> std::strong_ordering
    {
        return ::ntohl(address.s_addr) <=> ::ntohl(other.address.s_addr);
    }

    static constexpr auto any() noexcept -> AddressV4
    {
        return {{ 0, 0, 0, 0 }};
    }

    static constexpr auto loopback() noexcept -> AddressV4
    {
        return {{ 127, 0, 0, 1 }};
    }

    static constexpr auto broadcast() noexcept -> AddressV4
    {
        return {{ 255, 255, 255, 255 }};
    }

    static auto from_string(std::string_view address) -> AddressV4
    {
        AddressV4 result;
        auto res = ::inet_pton(AF_INET, address.data(), &result.address);
        if (res != 1)
            throw_system_error("Failed to convert string to IPv4 address");

        return result;
    }
};


struct AddressV6 {
    using byte_type = std::array<std::uint8_t, 16>;
    using address_type = in6_addr;

    address_type address{};

    AddressV6() = default;

    constexpr AddressV6(const byte_type& bytes)
    {
        std::ranges::copy(bytes, address.s6_addr);
    }

    [[nodiscard]] 
    auto to_string() const -> std::string
    {
        std::string buffer(INET6_ADDRSTRLEN, '\0');
        if (::inet_ntop(AF_INET6, &address, buffer.data(), INET6_ADDRSTRLEN) == nullptr)
            throw_system_error("Failed to convert IPv6 address to string");

        return buffer;
    }

    auto operator==(const AddressV6& other) const noexcept -> bool
    {
        auto res = std::memcmp(address.s6_addr, other.address.s6_addr, 16);
        return res == 0;
    }

    auto operator<=>(const AddressV6& other) const noexcept -> std::strong_ordering
    {
        auto res = std::memcmp(address.s6_addr, other.address.s6_addr, 16);
        if (res < 0) return std::strong_ordering::less;
        if (res > 0) return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }

    static constexpr auto any() noexcept -> AddressV6
    {
        return {{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }};
    }

    static constexpr auto loopback() noexcept -> AddressV6
    {
        return {{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }};
    }

    static auto from_string(std::string_view address) -> AddressV6
    {
        AddressV6 result;
        auto res = ::inet_pton(AF_INET6, address.data(), &result.address);
        if (res != 1)
            throw_system_error("Failed to convert string to IPv6 address");

        return result;
    }
};


class Address {
public:
    using address_type = std::variant<AddressV4, AddressV6>;

    Address() = default;

    Address(const AddressV4& ipv4)
      : address_{ ipv4 }
    {}

    Address(const AddressV6& ipv6)
      : address_{ ipv6 }
    {}

    [[nodiscard]]
    auto to_string() const -> std::string
    {
        return std::visit([](const auto& addr) { return addr.to_string(); }, address_);
    }

    [[nodiscard]] 
    constexpr auto is_v4() const noexcept -> bool
    {
        return std::holds_alternative<AddressV4>(address_);
    }

    [[nodiscard]]
    constexpr auto is_v6() const noexcept -> bool
    {
        return std::holds_alternative<AddressV6>(address_);
    }

    [[nodiscard]]
    constexpr auto to_v4() const -> AddressV4
    {
        return std::get<AddressV4>(address_);
    }

    [[nodiscard]]
    constexpr auto to_v6() const -> AddressV6
    {
        return std::get<AddressV6>(address_);
    }

    auto operator==(const Address& other) const noexcept -> bool
    {
        return address_ == other.address_;
    }

    auto operator<=>(const Address& other) const noexcept -> std::strong_ordering
    {
        if (address_.index() != other.address_.index())
            return address_.index() <=> other.address_.index();

        if (std::holds_alternative<AddressV4>(address_))
            return std::get<AddressV4>(address_) <=> std::get<AddressV4>(other.address_);

        return std::get<AddressV6>(address_) <=> std::get<AddressV6>(other.address_);
    }

    static auto from_string(std::string_view address) -> Address
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

private:
    address_type address_;
};

} // namespace ip

#endif // BLOG_IP_ADDRESS_H