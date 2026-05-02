#ifndef BLOG_COMMON_HASH_H
#define BLOG_COMMON_HASH_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

struct UseStdHashT {
    std::string_view value;
};

struct UseFNV1aHashT {
    std::string_view value;
};

constexpr auto use_std_hash(const std::string_view value) noexcept -> UseStdHashT
{
    return UseStdHashT{ value };
}

constexpr auto use_fnv_1a(const std::string_view value) noexcept -> UseFNV1aHashT
{
    return UseFNV1aHashT{ value };
}

[[nodiscard]]
inline auto hash(const UseStdHashT value) noexcept -> std::size_t
{
    using Hasher = std::hash<std::string_view>;
    return Hasher{}(value.value);
}

[[nodiscard]]
constexpr auto hash(const UseFNV1aHashT value) noexcept -> std::uint64_t
{
    constexpr auto FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr auto FNV_PRIME = 1099511628211ULL;

    auto result = FNV_OFFSET_BASIS;
    for (const unsigned char byte : value.value) {
        result ^= byte;
        result *= FNV_PRIME;
    }

    return result;
}


// 支持hash表的异构查找
struct StringHash {
    using is_transparent = void;

    auto operator()(const std::string_view sv) const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(hash(use_fnv_1a(sv)));
    }
};

// 支持hash表的异构查找
struct PmrStringHash {
    using is_transparent = void;

    auto operator()(const std::string_view sv) const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(hash(use_fnv_1a(sv)));
    }
};

#endif // BLOG_COMMON_HASH_H