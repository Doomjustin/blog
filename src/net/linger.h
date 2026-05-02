#ifndef BLOG_NET_LINGER_H
#define BLOG_NET_LINGER_H

#include <compare> // for operator<=>
#include <cstddef>

#include <sys/socket.h>

namespace net {

/**
 * @brief Model the `SO_LINGER` socket option controlling close behavior.
 *
 * When linger is enabled with a non-zero timeout, `close(2)` blocks until
 * all pending data is sent or the timeout expires. With a zero timeout,
 * the connection is reset immediately. Disabling linger restores the
 * default behavior where `close(2)` returns immediately and the kernel
 * drains data in the background.
 */
class LingerOption {
public:
    static constexpr int level = SOL_SOCKET;
    static constexpr int name = SO_LINGER;
    using value_type = struct ::linger;

    LingerOption() = default;

    /**
     * @brief Construct with explicit linger policy.
     *
     * @param on      Enable linger behavior.
     * @param timeout Linger duration in seconds when `on` is true.
     */
    explicit LingerOption(bool on, int timeout) 
      : value_{ .l_onoff=on ? 1 : 0, .l_linger=timeout } 
    {}

    [[nodiscard]]
    constexpr auto value() const noexcept -> const value_type&
    {
        return value_;
    }

    auto data() noexcept -> void*
    {
        return &value_;
    }

    [[nodiscard]]
    auto data() const noexcept -> const void*
    {
        return &value_;
    }

    [[nodiscard]]
    constexpr auto size() const noexcept -> std::size_t
    {
        return sizeof(value_);
    }

    auto operator==(const LingerOption& other) const noexcept -> bool
    {
        return value_.l_onoff == other.value_.l_onoff
            && value_.l_linger == other.value_.l_linger;
    }

    auto operator<=>(const LingerOption& other) const noexcept
    {
        return value_.l_onoff == other.value_.l_onoff
            ? value_.l_linger <=> other.value_.l_linger
            : value_.l_onoff <=> other.value_.l_onoff;
    }

private:
    struct ::linger value_;
};

} // namespace net

#endif // BLOG_NET_LINGER_H