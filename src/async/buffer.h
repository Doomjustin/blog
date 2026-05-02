#ifndef BLOG_ASYNC_BUFFER_H
#define BLOG_ASYNC_BUFFER_H

#include <cstddef>
#include <ranges>
#include <span>

namespace async {

/**
 * @brief Create a read-only byte view over a contiguous range.
 *
 * This adapter is useful when lower-level I/O APIs consume raw bytes but the
 * caller holds typed contiguous storage.
 *
 * @tparam T Any contiguous range type.
 * @param range Source range whose storage is reinterpreted as bytes.
 * @return `std::span<const std::byte>` referencing the same underlying memory.
 * @pre The underlying storage must remain alive for the lifetime of the returned span.
 * @code
 * std::string_view payload = "GET / HTTP/1.1\r\n\r\n";
 * auto bytes = buffer(payload);
 * // bytes.size() == payload.size()
 * @endcode
 */
template<std::ranges::contiguous_range T>
auto buffer(const T& range) noexcept -> std::span<const std::byte>
{
    return std::as_bytes(std::span{ range });
}

/**
 * @brief Create a writable byte view over a mutable contiguous range.
 *
 * Use this overload when an API writes bytes directly into caller-provided
 * storage. The `requires` clause prevents creating writable views from const
 * element ranges.
 *
 * @tparam T Any mutable contiguous range type.
 * @param range Mutable range whose storage is exposed as writable bytes.
 * @return `std::span<std::byte>` referencing the same underlying memory.
 * @pre The underlying storage must remain alive for the lifetime of the returned span.
 * @code
 * std::vector<char> out(1024);
 * auto bytes = buffer(out);
 * bytes[0] = std::byte{0x2A};
 * @endcode
 */
template<std::ranges::contiguous_range T>
    requires (!std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<T>>>)
auto buffer(T& range) noexcept -> std::span<std::byte>
{
    return std::as_writable_bytes(std::span{ range });
}


/**
 * @brief Constrain types that can be adapted to writable byte spans.
 */
template <typename T>
concept mutable_buffer = requires(T& t)
{
    { async::buffer(t) } -> std::same_as<std::span<std::byte>>;
};

/**
 * @brief Constrain types that can be adapted to read-only byte spans.
 */
template<typename T>
concept const_buffer = requires(const T& t)
{
    { async::buffer(t) } -> std::same_as<std::span<const std::byte>>;
};

/**
 * @brief Constrain ranges whose elements each model `const_buffer`.
 */
template <typename T>
concept sequence_buffer = std::ranges::range<T> && const_buffer<std::ranges::range_reference_t<T>>;

} // namespace async

#endif // BLOG_ASYNC_BUFFER_H