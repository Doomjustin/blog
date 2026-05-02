#ifndef BLOG_COMMON_UTILITY_H
#define BLOG_COMMON_UTILITY_H

#include <charconv>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

auto to_uppercase(std::string_view input) -> std::string;

auto to_lowercase(std::string_view input) -> std::string;

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