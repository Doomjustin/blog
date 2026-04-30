#include "exceptions.h"

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