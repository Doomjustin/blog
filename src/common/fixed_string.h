#ifndef BLOG_COMMON_FIXED_STRING_H
#define BLOG_COMMON_FIXED_STRING_H

#include <algorithm>
#include <array>
#include <string_view>

template<std::size_t N>
struct FixedString {
    std::array<char, N> data;

    static constexpr std::size_t capacity{ N };

    // 为了调用的方便性，我们允许字符串的隐式转换
    constexpr FixedString(const char (&s)[N])
    {
        std::copy_n(s, N, data.data());
    }

    auto operator<=>(const FixedString& other) const = default;

    [[nodiscard]]
    constexpr auto view() const noexcept -> std::string_view
    {
        return { data.data(), N - 1 };
    }
};

#endif // BLOG_COMMON_FIXED_STRING_H