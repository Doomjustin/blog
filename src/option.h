#ifndef BLOG_OPTION_H
#define BLOG_OPTION_H

#include <concepts>

/**
 * @brief Constrain socket option types used with `setsockopt`/`getsockopt`.
 *
 * An option must advertise its `level` and `name` constants and provide a
 * `data()`/`size()` pair compatible with the POSIX socket option API.
 * It must also be default-constructible so the get-option path can create
 * an empty value to be filled in by the kernel.
 */
template<typename T>
concept socket_option = requires(const T& opt) {
    { T::level } -> std::convertible_to<int>;
    { T::name }  -> std::convertible_to<int>;
    { opt.data() } -> std::convertible_to<const void*>;
    { opt.size() } -> std::convertible_to<std::size_t>;
} && std::is_default_constructible_v<T>;


/**
 * @brief Constrain flag options toggled via `fcntl(F_GETFL/F_SETFL)` or similar.
 *
 * Flag options (e.g. `O_NONBLOCK`, `FD_CLOEXEC`) differ from socket options:
 * they are set by ORing a single bit into a flags word rather than via
 * `setsockopt`, so they need different command constants.
 */
template<typename T>
concept flag_option = requires(const T& opt) {
    { T::get_cmd } -> std::convertible_to<int>;
    { T::set_cmd } -> std::convertible_to<int>;
    { T::bit }     -> std::convertible_to<int>;
} && std::constructible_from<T, bool>
  && std::convertible_to<T, bool>;


/**
 * @brief Model a boolean-valued socket option backed by an `int` flag.
 *
 * Encodes the `SO_KEEPALIVE`, `SO_REUSEADDR` family of options where the
 * kernel expects a non-zero `int` to mean *enabled*.
 *
 * @tparam Level `setsockopt` level (e.g. `SOL_SOCKET`, `IPPROTO_TCP`).
 * @tparam Name  `setsockopt` option name.
 */
template<int Level, int Name>
class BooleanOption {
public:
    static constexpr int level = Level;
    static constexpr int name = Name;
    using value_type = bool;

    explicit BooleanOption(bool value = false) 
      : value_{ value ? 1 : 0 } 
    {}

    [[nodiscard]]
    constexpr auto value() const noexcept -> bool
    {
        return value_ != 0;
    }

    [[nodiscard]]
    auto data() const noexcept -> const void*
    {   
        return &value_;
    }

    auto data() noexcept -> void*
    {
        return &value_;
    }

    [[nodiscard]]
    constexpr auto size() const noexcept -> std::size_t
    {
        return sizeof(value_);
    }

    auto operator<=>(const BooleanOption& other) const noexcept = default;

    operator bool() const noexcept 
    { 
        return value_ != 0; 
    }

private:
    int value_{};
};


/**
 * @brief Model an integer-valued socket option.
 *
 * Covers options such as `SO_RCVBUF`, `TCP_KEEPIDLE`, `TCP_KEEPINTVL`
 * where the kernel reads or writes a numeric value of type `T`.
 *
 * @tparam Level `setsockopt` level.
 * @tparam Name  `setsockopt` option name.
 * @tparam T     Integer value type (default `int`).
 */
template<int Level, int Name, std::integral T = int>
class ValueOption {
public:
    static constexpr int level = Level;
    static constexpr int name = Name;
    using value_type = T;

    explicit ValueOption(T value = {}) 
      : value_{ value } 
    {}

    [[nodiscard]] constexpr auto value() const noexcept -> T 
    { 
        return value_;
    }

    [[nodiscard]]
    auto data() const noexcept -> const void*
    {
        return &value_;
    }

    auto data() noexcept -> void*
    {
        return &value_;
    }

    [[nodiscard]]
    constexpr auto size() const noexcept -> std::size_t
    {
        return sizeof(value_);
    }

    auto operator<=>(const ValueOption& other) const noexcept = default;

    [[nodiscard]]
    constexpr operator bool() const noexcept
    {
        return value_ != 0;
    }
      
private:
    T value_{};
};


/**
 * @brief Model a single-bit flag toggled via `fcntl`.
 *
 * Used for file-descriptor flags such as `O_NONBLOCK` and `FD_CLOEXEC`
 * where the setting involves reading the current flags word, masking or
 * setting the target bit, and writing back.
 *
 * @tparam GetCmd `fcntl` command to read current flags (e.g. `F_GETFL`).
 * @tparam SetCMD `fcntl` command to write new flags (e.g. `F_SETFL`).
 * @tparam Bit    Target flag bit (e.g. `O_NONBLOCK`).
 */
template<int GetCmd, int SetCMD, int Bit>
class FlagOption {
public:
    static constexpr int get_cmd = GetCmd;
    static constexpr int set_cmd = SetCMD;
    static constexpr int bit = Bit;

    explicit FlagOption(bool enabled = false) 
      : value_{ enabled ? bit : 0 } 
    {}

    [[nodiscard]]
    constexpr auto value() const noexcept -> bool
    {
        return (value_ & bit) != 0;
    }

    [[nodiscard]]
    auto data() const noexcept -> const void*
    {   
        return &value_;
    }

    auto data() noexcept -> void*
    {
        return &value_;
    }

    [[nodiscard]]
    constexpr auto size() const noexcept -> std::size_t
    {
        return sizeof(value_);
    }

    auto operator<=>(const FlagOption& other) const noexcept = default;

    constexpr operator bool() const noexcept
    {
        return (value_ & bit) != 0;
    }

private:
    int value_{};
};

#endif // BLOG_OPTION_H