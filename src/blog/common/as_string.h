#ifndef BLOG_COMMON_AS_STRING_H
#define BLOG_COMMON_AS_STRING_H

#include <span>
#include <string_view>

auto as_string(std::span<const std::byte> data) -> std::string_view
{
    return { reinterpret_cast<const char*>(data.data()), data.size() };
}

auto as_string(std::span<std::byte> data) -> std::string_view
{
    return { reinterpret_cast<const char*>(data.data()), data.size() };
}

#endif // BLOG_COMMON_AS_STRING_H
