#ifndef BLOG_COMMON_AS_STRING_H
#define BLOG_COMMON_AS_STRING_H

#include <span>
#include <string_view>

/**
 * @brief Reinterpret a read-only byte span as a `string_view`.
 *
 * No copy is made; the returned view aliases the same memory.
 * Useful for treating network receive buffers as text without allocation.
 *
 * @param data Byte span to reinterpret.
 * @return `string_view` with the same address and length as `data`.
 * @pre The bytes must be valid for the lifetime of the returned view.
 */
auto as_string(std::span<const std::byte> data) -> std::string_view;

/**
 * @brief Reinterpret a mutable byte span as a `string_view`.
 *
 * Overload for mutable spans; the returned view is still read-only.
 *
 * @param data Byte span to reinterpret.
 * @return `string_view` with the same address and length as `data`.
 * @pre The bytes must be valid for the lifetime of the returned view.
 */
auto as_string(std::span<std::byte> data) -> std::string_view;


template<std::ranges::contiguous_range T>
auto as_string(const T& range) -> std::string_view
{
    return as_string(std::as_bytes(std::span{ range }));
}

template<std::ranges::contiguous_range T>
    requires (!std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<T>>>)
auto as_string(T& range) -> std::string_view
{
    return as_string(std::as_writable_bytes(std::span{ range }));
}

#endif // BLOG_COMMON_AS_STRING_H
