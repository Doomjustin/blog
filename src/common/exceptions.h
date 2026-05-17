#ifndef BLOG_COMMON_EXCEPTIONS_H
#define BLOG_COMMON_EXCEPTIONS_H

#include <expected>
#include <format>
#include <system_error>

/// @brief 抛出带显式 error code 的 std::system_error，错误消息由 format 结果构造。
/// @param[in] error 平台错误码（通常来自 errno 或系统调用返回值）。
/// @param[in] fmt std::format 格式串。
/// @param[in] args 传给 format 的参数 pack。
template<typename... Args>
void throw_system_error(int error, std::format_string<Args...> fmt, Args&&... args)
{
    throw std::system_error{ error, std::generic_category(),
                             std::format(fmt, std::forward<Args>(args)...) };
}

/// @brief 抛出使用当前 errno 的 std::system_error，错误消息由 format 结果构造。
/// @param[in] fmt std::format 格式串。
/// @param[in] args 传给 format 的参数 pack。
template<typename... Args>
void throw_system_error(std::format_string<Args...> fmt, Args&&... args)
{
    throw std::system_error{ errno, std::generic_category(),
                             std::format(fmt, std::forward<Args>(args)...) };
}

/// @brief 用当前 errno 构造 unexpected<std::error_code>。
/// @return 包含 std::error_code 的 unexpected 结果。
auto unexpected_system_error() -> std::unexpected<std::error_code>;

/// @brief 用 std::errc 构造 unexpected<std::error_code>。
/// @param[in] ec 标准错误枚举值。
/// @return 包含 std::error_code 的 unexpected 结果。
auto unexpected_system_error(std::errc ec) -> std::unexpected<std::error_code>;

/// @brief 用显式 error code 构造 unexpected<std::error_code>。
/// @param[in] error 平台错误码。
/// @return 包含 std::error_code 的 unexpected 结果。
auto unexpected_system_error(int error) -> std::unexpected<std::error_code>;

#endif // BLOG_COMMON_EXCEPTIONS_H