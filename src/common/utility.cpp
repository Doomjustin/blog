#include "utility.h"

auto to_uppercase(std::string_view input) -> std::string
{
    std::string result{ input };
    for (char& c : result)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - ('a' - 'A'));
    return result;
}

auto to_lowercase(std::string_view input) -> std::string
{
    std::string result{ input };
    for (char& c : result)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return result;
}