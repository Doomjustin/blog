#ifndef BLOG_EXCEPTIONS_H
#define BLOG_EXCEPTIONS_H

#include <format>
#include <system_error>
#include <expected>


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

auto unexpected_system_error() -> std::unexpected<std::error_code>
{
    return std::unexpected{ std::error_code{ errno, std::system_category() } };
}

auto unexpected_system_error(std::errc ec) -> std::unexpected<std::error_code>
{
    return std::unexpected{ std::make_error_code(ec) };
}

auto unexpected_system_error(int error) -> std::unexpected<std::error_code>
{
    return std::unexpected{ std::error_code{ error, std::system_category() } };
}

#endif // BLOG_EXCEPTIONS_H