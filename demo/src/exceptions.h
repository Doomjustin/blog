#ifndef BLOG_COMMON_EXCEPTIONS_H
#define BLOG_COMMON_EXCEPTIONS_H

#include <expected>
#include <format>
#include <system_error>

template<typename... Args>
void throw_system_error(int error, std::format_string<Args...> fmt, Args&&... args)
{
    throw std::system_error{ error, std::generic_category(), std::format(fmt, std::forward<Args>(args)...) };
}

template<typename... Args>
void throw_system_error(std::format_string<Args...> fmt, Args&&... args)
{
    throw std::system_error{ errno, std::generic_category(), std::format(fmt, std::forward<Args>(args)...) };
}

inline auto unexpected_system_error() -> std::unexpected<std::error_code>
{
    return std::unexpected{ std::error_code{ errno, std::system_category() } };
}

inline auto unexpected_system_error(std::errc ec) -> std::unexpected<std::error_code>
{
    return std::unexpected{ std::make_error_code(ec) };
}

inline auto unexpected_system_error(int error) -> std::unexpected<std::error_code>
{
    return std::unexpected{ std::error_code{ error, std::system_category() } };
}

#endif // BLOG_COMMON_EXCEPTIONS_H
