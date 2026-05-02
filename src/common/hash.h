#ifndef BLOG_COMMON_HASH_H
#define BLOG_COMMON_HASH_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

/**
 * @brief Tag type that selects `std::hash<string_view>` as the hash algorithm.
 * Construct via `use_std_hash()`.
 */
struct UseStdHashT {
    std::string_view value;
};

/**
 * @brief Tag type that selects the FNV-1a algorithm as the hash algorithm.
 * Construct via `use_fnv_1a()`.
 */
struct UseFNV1aHashT {
    std::string_view value;
};

/**
 * @brief Returns a tag that routes `hash()` to `std::hash<string_view>`.
 *
 * Use when you need platform-consistent integration with the standard
 * library (e.g., as the default bucket hash in `std::unordered_map`).
 */
constexpr auto use_std_hash(const std::string_view value) noexcept -> UseStdHashT
{
    return UseStdHashT{ value };
}

/**
 * @brief Returns a tag that routes `hash()` to the FNV-1a algorithm.
 *
 * Prefer over `use_std_hash` when you need deterministic, cross-platform
 * hash values (e.g., protocol identifiers, compile-time dispatch tables).
 * The result is `constexpr`-friendly.
 */
constexpr auto use_fnv_1a(const std::string_view value) noexcept -> UseFNV1aHashT
{
    return UseFNV1aHashT{ value };
}

/**
 * @brief Hashes `value` using `std::hash<string_view>`.
 *
 * Not `constexpr` because the standard library hash is not guaranteed
 * to be a constant expression. Marked `inline` to prevent ODR violations
 * when included across multiple translation units.
 */
[[nodiscard]]
inline auto hash(const UseStdHashT value) noexcept -> std::size_t
{
    using Hasher = std::hash<std::string_view>;
    return Hasher{}(value.value);
}

/**
 * @brief Hashes `value` using the FNV-1a (64-bit) algorithm.
 *
 * Produces deterministic, cross-platform results and is `constexpr`,
 * so it can be evaluated at compile time.
 *
 * @return 64-bit FNV-1a digest of the input bytes.
 */
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

/**
 * @brief Transparent hasher for `std::string`-keyed unordered containers.
 *
 * Setting `is_transparent` allows heterogeneous lookup with `std::string_view`
 * keys, avoiding temporary `std::string` allocations at lookup sites.
 * Uses FNV-1a internally for deterministic, cross-platform hashing.
 *
 * @code
 * std::unordered_map<std::string, int, StringHash, std::equal_to<>> map;
 * map["key"] = 42;
 * map.contains(std::string_view{ "key" });  // no allocation
 * @endcode
 */
struct StringHash {
    using is_transparent = void;

    auto operator()(const std::string_view sv) const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(hash(use_fnv_1a(sv)));
    }
};

/**
 * @brief Transparent hasher for `std::pmr::string`-keyed unordered containers.
 *
 * Identical in behaviour to `StringHash`; provided as a separate type so that
 * PMR-based containers can use it without accidentally accepting a non-PMR
 * container's hasher by mistake.
 */
struct PmrStringHash {
    using is_transparent = void;

    auto operator()(const std::string_view sv) const noexcept -> std::size_t
    {
        return static_cast<std::size_t>(hash(use_fnv_1a(sv)));
    }
};

#endif // BLOG_COMMON_HASH_H