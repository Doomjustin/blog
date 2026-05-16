#ifndef BLOG_COMMON_META_STRING_H
#define BLOG_COMMON_META_STRING_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace meta {

/// @brief 编译期固定容量字符串，适合用作非类型模板参数或轻量字面量包装。
///
/// `N` 包含结尾的空字符，因此 `view()` 返回的长度为 `N - 1`。
///
/// ```cpp
/// constexpr xin::meta::String<4> text{ "xin" };
/// static_assert(text.capacity == 4);
/// static_assert(text.view() == "xin");
/// ```
///
/// @tparam N 字符数组容量，包含结尾空字符。
template<std::size_t N>
struct String {
    std::array<char, N> data;

    static constexpr std::size_t capacity{ N };

    constexpr String(const char (&s)[N])
    {
        std::copy_n(s, N, data.data());
    }

    auto operator<=>(const String& other) const = default;

    /// @brief 返回去掉结尾空字符后的字符串视图。
    /// @return 指向内部字符数组的非拥有 std::string_view。
    [[nodiscard]] constexpr auto view() const noexcept -> std::string_view
    {
        return { data.data(), N - 1 };
    }
};

} // namespace meta

#endif // BLOG_COMMON_META_STRING_H