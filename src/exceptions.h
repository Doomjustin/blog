#ifndef BLOG_EXCEPTIONS_H
#define BLOG_EXCEPTIONS_H

#include <expected>
#include <format>
#include <system_error>

/**
 * @brief Throw `std::system_error` with a formatted message and explicit error code.
 *
 * @param error POSIX error code (positive).
 * @param fmt Message format string.
 * @param args Format arguments.
 * @throws std::system_error Always.
 */
template<typename... Args>
void throw_system_error(int error, std::format_string<Args...> fmt, Args&&... args)
{
    throw std::system_error{ error, std::generic_category(), std::format(fmt, std::forward<Args>(args)...) };
}

/**
 * @brief Throw `std::system_error` using the current `errno` with a formatted message.
 *
 * @param fmt Message format string.
 * @param args Format arguments.
 * @throws std::system_error Always.
 */
template<typename... Args>
void throw_system_error(std::format_string<Args...> fmt, Args&&... args)
{
    throw std::system_error{ errno, std::generic_category(), std::format(fmt, std::forward<Args>(args)...) };
}

/**
 * @brief Wrap current `errno` as an `std::unexpected` error code.
 *
 * Use in `std::expected`-returning functions that do not throw.
 *
 * @return `std::unexpected` holding the current `errno` system error.
 */
auto unexpected_system_error() -> std::unexpected<std::error_code>
{
    return std::unexpected{ std::error_code{ errno, std::system_category() } };
}

/**
 * @brief Wrap a specific `std::errc` as an `std::unexpected` error code.
 *
 * @param ec Standard error condition to wrap.
 * @return `std::unexpected` holding the corresponding error code.
 */
auto unexpected_system_error(std::errc ec) -> std::unexpected<std::error_code>
{
    return std::unexpected{ std::make_error_code(ec) };
}

/**
 * @brief Wrap an explicit POSIX error number as an `std::unexpected` error code.
 *
 * @param error Positive POSIX error number.
 * @return `std::unexpected` holding the error code.
 */
auto unexpected_system_error(int error) -> std::unexpected<std::error_code>
{
    return std::unexpected{ std::error_code{ error, std::system_category() } };
}

#endif // BLOG_EXCEPTIONS_H