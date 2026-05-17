#ifndef BLOG_NET_OPTION_H
#define BLOG_NET_OPTION_H

#include <concepts>

namespace net {

/// @brief 约束可用于 setsockopt/getsockopt 的 socket option 类型。
///
/// 要求类型提供 level/name 常量，以及 data()/size() 访问底层存储。
/// @tparam T 待检查的 option 类型。
template<typename T>
concept socket_option = requires(const T& opt) {
    { T::level } -> std::convertible_to<int>;
    { T::name } -> std::convertible_to<int>;
    { opt.data() } -> std::convertible_to<const void*>;
    { opt.size() } -> std::convertible_to<std::size_t>;
} && std::is_default_constructible_v<T>;

/// @brief 约束可用于 fcntl flag 读写的 option 类型。
///
/// 要求类型提供 get_cmd/set_cmd/bit 常量，并支持 bool 语义转换。
/// @tparam T 待检查的 flag option 类型。
template<typename T>
concept flag_option = requires(const T& opt) {
    { T::get_cmd } -> std::convertible_to<int>;
    { T::set_cmd } -> std::convertible_to<int>;
    { T::bit } -> std::convertible_to<int>;
} && std::constructible_from<T, bool> && std::convertible_to<T, bool>;

/// @brief bool 语义的 socket option 封装。
///
/// 将逻辑值映射到内核期望的整数存储（0/1），可直接用于
/// setsockopt/getsockopt 的 data()/size() 接口。
///
/// @tparam Level socket option level（如 SOL_SOCKET）。
/// @tparam Name socket option name（如 SO_REUSEADDR）。
template<int Level, int Name>
class BooleanOption {
public:
    static constexpr int level = Level;
    static constexpr int name = Name;
    using value_type = bool;

    /// @brief 构造 BooleanOption。
    /// @param[in] value 初始布尔值。
    explicit BooleanOption(bool value = false)
      : value_{ value ? 1 : 0 }
    {}

    /// @brief 读取当前布尔值。
    /// @return true 表示启用，false 表示禁用。
    [[nodiscard]]
    constexpr auto value() const noexcept -> bool
    {
        return value_ != 0;
    }

    /// @brief 获取底层只读数据指针。
    /// @return 指向内部存储的 const void*。
    [[nodiscard]]
    auto data() const noexcept -> const void*
    {
        return &value_;
    }

    /// @brief 获取底层可写数据指针。
    /// @return 指向内部存储的 void*。
    auto data() noexcept -> void*
    {
        return &value_;
    }

    /// @brief 获取底层存储字节大小。
    /// @return 用于 socket API 的 option 长度。
    [[nodiscard]]
    constexpr auto size() const noexcept -> std::size_t
    {
        return sizeof(value_);
    }

    auto operator<=>(const BooleanOption& other) const noexcept = default;

    /// @brief 提供 bool 语义转换。
    /// @return 当前 option 的启用状态。
    [[nodiscard]]
    constexpr operator bool() const noexcept
    {
        return value_ != 0;
    }

private:
    int value_{};
};

/// @brief 值语义的 socket option 封装。
///
/// 适用于以数值形式配置的 socket option。
///
/// @tparam Level socket option level。
/// @tparam Name socket option name。
/// @tparam T option 存储类型，需满足 integral。
template<int Level, int Name, std::integral T = int>
class ValueOption {
public:
    static constexpr int level = Level;
    static constexpr int name = Name;
    using value_type = T;

    /// @brief 构造 ValueOption。
    /// @param[in] value 初始数值。
    explicit ValueOption(T value = {})
      : value_{ value }
    {}

    /// @brief 读取当前 option 数值。
    /// @return 当前存储值。
    [[nodiscard]] constexpr auto value() const noexcept -> T
    {
        return value_;
    }

    /// @brief 获取底层只读数据指针。
    /// @return 指向内部存储的 const void*。
    [[nodiscard]]
    auto data() const noexcept -> const void*
    {
        return &value_;
    }

    /// @brief 获取底层可写数据指针。
    /// @return 指向内部存储的 void*。
    auto data() noexcept -> void*
    {
        return &value_;
    }

    /// @brief 获取底层存储字节大小。
    /// @return 用于 socket API 的 option 长度。
    [[nodiscard]]
    constexpr auto size() const noexcept -> std::size_t
    {
        return sizeof(value_);
    }

    auto operator<=>(const ValueOption& other) const noexcept = default;

    /// @brief 提供 bool 语义转换。
    /// @return 数值非 0 返回 true。
    [[nodiscard]]
    constexpr operator bool() const noexcept
    {
        return value_ != 0;
    }

private:
    T value_{};
};

/// @brief 基于位标志的 fcntl option 封装。
///
/// 常用于通过 fcntl 读写文件描述符标志位。
///
/// @tparam GetCmd fcntl 获取命令（如 F_GETFL）。
/// @tparam SetCmd fcntl 设置命令（如 F_SETFL）。
/// @tparam Bit 目标标志位掩码（如 O_NONBLOCK）。
template<int GetCmd, int SetCmd, int Bit>
class FlagOption {
public:
    static constexpr int get_cmd = GetCmd;
    static constexpr int set_cmd = SetCmd;
    static constexpr int bit = Bit;

    /// @brief 构造 FlagOption。
    /// @param[in] enabled 是否启用该 bit。
    explicit FlagOption(bool enabled = false)
      : value_{ enabled ? bit : 0 }
    {}

    /// @brief 查询当前 bit 是否启用。
    /// @return 启用返回 true，否则返回 false。
    [[nodiscard]]
    constexpr auto value() const noexcept -> bool
    {
        return (value_ & bit) != 0;
    }

    /// @brief 获取底层只读数据指针。
    /// @return 指向内部存储的 const void*。
    [[nodiscard]]
    auto data() const noexcept -> const void*
    {
        return &value_;
    }

    /// @brief 获取底层可写数据指针。
    /// @return 指向内部存储的 void*。
    auto data() noexcept -> void*
    {
        return &value_;
    }

    /// @brief 获取底层存储字节大小。
    /// @return 用于 fcntl 读写的缓冲区长度。
    [[nodiscard]]
    constexpr auto size() const noexcept -> std::size_t
    {
        return sizeof(value_);
    }

    auto operator<=>(const FlagOption& other) const noexcept = default;

    /// @brief 提供 bool 语义转换。
    /// @return 当前 bit 启用状态。
    [[nodiscard]]
    constexpr operator bool() const noexcept
    {
        return (value_ & bit) != 0;
    }

private:
    int value_{};
};

} // namespace net

#endif // BLOG_NET_OPTION_H