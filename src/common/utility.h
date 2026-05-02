#ifndef BLOG_COMMON_UTILITY_H
#define BLOG_COMMON_UTILITY_H

#include <charconv>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

/**
 * @brief Returns a copy of `input` with every ASCII letter converted to upper case.
 *
 * Non-ASCII bytes and non-letter characters are passed through unchanged.
 * The original string_view is not modified.
 */
auto to_uppercase(std::string_view input) -> std::string;

/**
 * @brief Returns a copy of `input` with every ASCII letter converted to lower case.
 *
 * Non-ASCII bytes and non-letter characters are passed through unchanged.
 * The original string_view is not modified.
 */
auto to_lowercase(std::string_view input) -> std::string;

/**
 * @brief Parses the entire string `str` as an arithmetic value of type `T`.
 *
 * Wraps `std::from_chars` and rejects any trailing non-numeric characters
 * so that "42abc" is treated as an error rather than the value 42.
 *
 * @tparam T  Target arithmetic type (int, unsigned, long long, …).
 * @param str  The string to parse; must contain only the number representation.
 * @return The parsed value, or an `std::error_code` on failure (invalid format,
 *         overflow, trailing garbage).
 */
template<typename T>
auto numeric_cast(std::string_view str) -> std::expected<T, std::error_code>
{
    T value{};
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);

    if (ec != std::errc{})
        return std::unexpected(std::make_error_code(ec));

    if (ptr != str.data() + str.size())
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));

    return value;
}

/**
 * @brief Converts a floating-point value to its shortest round-trip string representation.
 *
 * Uses `std::to_chars` with default format, which produces the shortest
 * decimal representation that uniquely identifies the value.
 *
 * @tparam T  `float` or `double`.
 * @param value  The floating-point number to convert.
 * @return The string representation, or an `std::error_code` on failure
 *         (should only occur if the internal buffer is unexpectedly too small).
 */
template<std::floating_point T>
auto string_cast(T value) -> std::expected<std::string, std::error_code>
{
    // 符号/小数点/指数("e+NNN") 余量
    constexpr std::size_t buffer_size = static_cast<std::size_t>(std::numeric_limits<T>::max_digits10) + 8;

    std::array<char, buffer_size> buffer{};

    auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (ec != std::errc{})
        return std::unexpected(std::make_error_code(ec));

    if (ptr == buffer.data())
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));

    return std::string(buffer.data(), static_cast<std::size_t>(ptr - buffer.data()));
}


#endif // BLOG_COMMON_UTILITY_H