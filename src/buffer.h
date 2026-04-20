#ifndef BLOG_BUFFER_H
#define BLOG_BUFFER_H

#include <concepts>
#include <cstddef>
#include <ranges>
#include <span>

template<std::ranges::contiguous_range T>
auto buffer(const T& range) noexcept -> std::span<const std::byte>
{
    return std::as_bytes(std::span{ range });
}

template<std::ranges::contiguous_range T>
    requires (!std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<T>>>)
auto buffer(T& range) noexcept -> std::span<std::byte>
{
    return std::as_writable_bytes(std::span{ range });
}


template <typename T>
concept mutable_buffer = requires(T& t)
{
    { buffer(t) } -> std::same_as<std::span<std::byte>>;
};

template<typename T>
concept const_buffer = requires(const T& t)
{
    { buffer(t) } -> std::same_as<std::span<const std::byte>>;
};

template <typename T>
concept sequence_buffer = std::ranges::range<T> && const_buffer<std::ranges::range_reference_t<T>>;

#endif // BLOG_BUFFER_H