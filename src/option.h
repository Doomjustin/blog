#ifndef BLOG_OPTION_H
#define BLOG_OPTION_H

#include <concepts>

template<typename T>
concept socket_option = requires(const T& opt) {
    { T::level } -> std::convertible_to<int>;
    { T::name }  -> std::convertible_to<int>;
    { opt.data() } -> std::convertible_to<const void*>;
    { opt.size() } -> std::convertible_to<std::size_t>;
} && std::is_default_constructible_v<T>;


template<typename T>
concept flag_option = requires(const T& opt) {
    { T::get_cmd } -> std::convertible_to<int>;
    { T::set_cmd } -> std::convertible_to<int>;
    { T::bit }     -> std::convertible_to<int>;
} && std::constructible_from<T, bool>
  && std::convertible_to<T, bool>;


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