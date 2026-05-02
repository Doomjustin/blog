#ifndef BLOG_COMMON_FIXED_STRING_H
#define BLOG_COMMON_FIXED_STRING_H

#include <algorithm>
#include <array>
#include <string_view>

/**
 * @brief Compile-time string literal that can be used as a non-type template parameter.
 *
 * Stores a null-terminated string in a `std::array<char, N>` where N includes
 * the null terminator. The primary use case is tagging `NamedType` instantiations
 * to make each type unique at compile time.
 *
 * @tparam N  Length of the string including the null terminator.
 *
 * @pre The constructor accepts only string literals via `const char (&)[N]`.
 *
 * Example:
 * @code
 * constexpr FixedString tag{ "Meter" };  // N == 6
 * static_assert(tag.view() == "Meter");
 *
 * template<FixedString Name> struct Tag {};
 * using MeterTag = Tag<"Meter">;  // non-type template parameter
 * @endcode
 */
template<std::size_t N>
struct FixedString {
    std::array<char, N> data;

    /** Total storage size, including the null terminator. */
    static constexpr std::size_t capacity{ N };

    /** Implicitly constructs from a string literal; copies all N bytes including '\0'. */
    constexpr FixedString(const char (&s)[N])
    {
        std::copy_n(s, N, data.data());
    }

    auto operator<=>(const FixedString& other) const = default;

    /**
     * @brief Returns a string_view over the stored characters, excluding the null terminator.
     * @return View of length N-1 aliasing internal storage.
     */
    [[nodiscard]]
    constexpr auto view() const noexcept -> std::string_view
    {
        return { data.data(), N - 1 };
    }
};

#endif // BLOG_COMMON_FIXED_STRING_H