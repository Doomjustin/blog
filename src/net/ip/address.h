#ifndef BLOG_NET_IP_ADDRESS_H
#define BLOG_NET_IP_ADDRESS_H

#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace net::ip {

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
    auto to_string() const -> std::string;

    auto operator==(const AddressV4& other) const noexcept -> bool;

    auto operator<=>(const AddressV4& other) const noexcept -> std::strong_ordering;

    static constexpr auto any() noexcept -> AddressV4
    {
        return { { 0, 0, 0, 0 } };
    }

    static constexpr auto loopback() noexcept -> AddressV4
    {
        return { { 127, 0, 0, 1 } };
    }

    static constexpr auto broadcast() noexcept -> AddressV4
    {
        return { { 255, 255, 255, 255 } };
    }

    static auto from_string(std::string_view address) -> AddressV4;

    static auto from_addr(const in_addr& addr) -> AddressV4;
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
    auto to_string() const -> std::string;

    auto operator==(const AddressV6& other) const noexcept -> bool;

    auto operator<=>(const AddressV6& other) const noexcept -> std::strong_ordering;

    static constexpr auto any() noexcept -> AddressV6
    {
        return { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } };
    }

    static constexpr auto loopback() noexcept -> AddressV6
    {
        return { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 } };
    }

    static auto from_string(std::string_view address) -> AddressV6;

    static auto from_addr(const in6_addr& addr) -> AddressV6;
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

    auto operator==(const Address& other) const noexcept -> bool;

    auto operator<=>(const Address& other) const noexcept -> std::strong_ordering;

    static auto from_string(std::string_view address) -> Address;

private:
    address_type address_;
};

auto operator<<(std::ostream& os, const Address& address) -> std::ostream&;

} // namespace net::ip

#endif // BLOG_NET_IP_ADDRESS_H