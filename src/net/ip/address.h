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

/**
 * @brief IPv4 address value type backed by `in_addr`.
 *
 * Provides factory methods, string conversion, and total ordering so
 * addresses can be used as map keys and printed directly.
 */
struct AddressV4 {
    using byte_type = std::array<std::uint8_t, 4>;
    using address_type = in_addr;

    address_type address{};

    AddressV4() = default;

    /**
     * @brief Construct from a four-byte array in network byte order.
     *
     * @param bytes Raw IPv4 bytes, most significant first (e.g. `{192,168,1,1}`).
     */
    constexpr AddressV4(const byte_type& bytes)
    {
        address.s_addr = std::bit_cast<std::uint32_t>(bytes);
    }

    [[nodiscard]] 
    auto to_string() const -> std::string;

    auto operator==(const AddressV4& other) const noexcept -> bool;

    auto operator<=>(const AddressV4& other) const noexcept -> std::strong_ordering;

    /** @brief Return the wildcard address `0.0.0.0` (binds to all interfaces). */
    static constexpr auto any() noexcept -> AddressV4
    {
        return {{ 0, 0, 0, 0 }};
    }

    /** @brief Return the loopback address `127.0.0.1`. */
    static constexpr auto loopback() noexcept -> AddressV4
    {
        return {{ 127, 0, 0, 1 }};
    }

    /** @brief Return the limited broadcast address `255.255.255.255`. */
    static constexpr auto broadcast() noexcept -> AddressV4
    {
        return {{ 255, 255, 255, 255 }};
    }

    /**
     * @brief Parse a dotted-decimal string into an `AddressV4`.
     *
     * @param address Dotted-decimal string (e.g. `"192.168.1.1"`).
     * @throws std::system_error If `inet_pton` fails.
     */
    static auto from_string(std::string_view address) -> AddressV4;

    /**
     * @brief Construct from an existing `in_addr` (e.g. from `accept(2)`).
     */
    static auto from_addr(const in_addr& addr) -> AddressV4;
};


/**
 * @brief IPv6 address value type backed by `in6_addr`.
 *
 * Provides factory methods, string conversion, and total ordering
 * for IPv6 addresses.
 */
struct AddressV6 {
    using byte_type = std::array<std::uint8_t, 16>;
    using address_type = in6_addr;

    address_type address{};

    AddressV6() = default;

    /**
     * @brief Construct from a sixteen-byte array in network byte order.
     *
     * @param bytes Raw IPv6 bytes.
     */
    constexpr AddressV6(const byte_type& bytes)
    {
        std::ranges::copy(bytes, address.s6_addr);
    }

    [[nodiscard]] 
    auto to_string() const -> std::string;

    auto operator==(const AddressV6& other) const noexcept -> bool;

    auto operator<=>(const AddressV6& other) const noexcept -> std::strong_ordering;

    /** @brief Return the unspecified address `::` (binds to all interfaces). */
    static constexpr auto any() noexcept -> AddressV6
    {
        return {{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }};
    }

    /** @brief Return the loopback address `::1`. */
    static constexpr auto loopback() noexcept -> AddressV6
    {
        return {{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }};
    }

    /**
     * @brief Parse a colon-hex string into an `AddressV6`.
     *
     * @param address Colon-hex string (e.g. `"::1"`).
     * @throws std::system_error If `inet_pton` fails.
     */
    static auto from_string(std::string_view address) -> AddressV6;

    /**
     * @brief Construct from an existing `in6_addr` (e.g. from `accept(2)`).
     */
    static auto from_addr(const in6_addr& addr) -> AddressV6;
};


/**
 * @brief Version-agnostic IP address holding either `AddressV4` or `AddressV6`.
 *
 * Use in generic APIs that must handle both address families. Concrete
 * family can be queried with `is_v4()`/`is_v6()` and extracted with
 * `to_v4()`/`to_v6()`.
 */
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

    /**
     * @brief Parse either a dotted-decimal or colon-hex address string.
     *
     * Tries IPv4 first, then IPv6. Throws if neither format matches.
     *
     * @param address String representation of the IP address.
     * @throws std::system_error If neither format parses successfully.
     */
    static auto from_string(std::string_view address) -> Address;

private:
    address_type address_;
};


/** @brief Stream output operator; delegates to `Address::to_string()`. */
auto operator<<(std::ostream& os, const Address& address) -> std::ostream&;

} // namespace net::ip

#endif // BLOG_NET_IP_ADDRESS_H