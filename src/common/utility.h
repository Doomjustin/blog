#ifndef BLOG_COMMON_UTILITY_H
#define BLOG_COMMON_UTILITY_H

#include <string>
#include <string_view>

auto to_uppercase(std::string_view input) -> std::string;

auto to_lowercase(std::string_view input) -> std::string;

#endif // BLOG_COMMON_UTILITY_H