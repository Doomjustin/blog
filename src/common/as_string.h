#ifndef BLOG_COMMON_AS_STRING_H
#define BLOG_COMMON_AS_STRING_H

#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>

template<typename T>
struct is_byte_span : std::false_type {};

template<typename T, std::size_t Extent>
struct is_byte_span<std::span<T, Extent>>
  : std::bool_constant<std::same_as<std::remove_cv_t<T>, std::byte>> {};

template<typename T>
inline constexpr bool is_byte_span_v = is_byte_span<std::remove_cvref_t<T>>::value;

auto as_string(std::span<const std::byte> data) -> std::string_view;

auto as_string(std::span<std::byte> data) -> std::string_view;

template<std::ranges::contiguous_range T>
    requires(!is_byte_span_v<T>)
auto as_string(const T& range) -> std::string_view
{
    return as_string(std::as_bytes(std::span{ range }));
}

template<std::ranges::contiguous_range T>
    requires(!std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<T>>> &&
             !is_byte_span_v<T>)
auto as_string(T& range) -> std::string_view
{
    return as_string(std::as_writable_bytes(std::span{ range }));
}

#endif // BLOG_COMMON_AS_STRING_H
