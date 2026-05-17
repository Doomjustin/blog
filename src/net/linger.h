#ifndef BLOG_NET_LINGER_H
#define BLOG_NET_LINGER_H

#include <compare>
#include <cstddef>

#include <sys/socket.h>

namespace net {

/// @brief socket `SO_LINGER` 选项的轻量封装。
class LingerOption {
private:
    struct ::linger value_;

public:
    /// @brief `setsockopt/getsockopt` 使用的 socket level。
    static constexpr int level = SOL_SOCKET;
    /// @brief `setsockopt/getsockopt` 使用的选项名。
    static constexpr int name = SO_LINGER;
    /// @brief 底层 native 值类型。
    using value_type = struct ::linger;

    /// @brief 默认构造，保持系统默认的 `linger` 配置值。
    LingerOption() = default;

    /// @brief 按开关与超时时间构造 `SO_LINGER` 配置。
    /// @param[in] on 是否启用 linger 行为。
    /// @param[in] timeout linger 超时时间（秒）。
    explicit LingerOption(bool on, int timeout)
      : value_{ .l_onoff = on ? 1 : 0, .l_linger = timeout }
    {}

    /// @brief 获取底层 `linger` 结构的只读引用。
    /// @return 底层选项值的只读引用。
    [[nodiscard]]
    constexpr auto value() const noexcept -> const value_type&
    {
        return value_;
    }

    /// @brief 获取可写原始数据指针，用于传给 `setsockopt`。
    /// @return 指向底层 `linger` 结构的可写指针。
    auto data() noexcept -> void*
    {
        return &value_;
    }

    /// @brief 获取只读原始数据指针，用于传给 `getsockopt` 或只读场景。
    /// @return 指向底层 `linger` 结构的只读指针。
    [[nodiscard]]
    auto data() const noexcept -> const void*
    {
        return &value_;
    }

    /// @brief 获取底层选项值的字节大小。
    /// @return `linger` 结构体大小（字节）。
    [[nodiscard]]
    constexpr auto size() const noexcept -> std::size_t
    {
        return sizeof(value_);
    }

    /// @brief 判断两个 `LingerOption` 是否等价。
    /// @param[in] other 待比较对象。
    /// @return 相同返回 `true`，否则返回 `false`。
    auto operator==(const LingerOption& other) const noexcept -> bool
    {
        return value_.l_onoff == other.value_.l_onoff && value_.l_linger == other.value_.l_linger;
    }

    /// @brief 提供三路比较以支持排序与关系运算。
    /// @param[in] other 待比较对象。
    /// @return 按 `l_onoff` 与 `l_linger` 组成的字典序比较结果。
    auto operator<=>(const LingerOption& other) const noexcept -> std::strong_ordering
    {
        return value_.l_onoff == other.value_.l_onoff ? value_.l_linger <=> other.value_.l_linger
                                                      : value_.l_onoff <=> other.value_.l_onoff;
    }
};

} // namespace net

#endif // BLOG_NET_LINGER_H