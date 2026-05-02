#ifndef BLOG_NET_ZERO_COPY_H
#define BLOG_NET_ZERO_COPY_H

#include <ranges>
#include <span>

namespace net {

/**
 * @brief Tag type that marks a buffer as eligible for kernel zero-copy send.
 *
 * Wrap a buffer in `zero_copy()` and pass the result to
 * `StreamSocket::async_send_some()` to use `IORING_OP_SEND_ZC` instead
 * of the standard copy path. The buffer memory must remain pinned until
 * the completion notification arrives.
 */
struct ZeroCopyT {
    std::span<const std::byte> span;
};

/**
 * @brief Wrap a contiguous range as a zero-copy send buffer.
 *
 * @tparam T Contiguous range type.
 * @param range Source range; its underlying memory must outlive the I/O operation.
 * @return `ZeroCopyT` referencing the same memory.
 */
template<std::ranges::contiguous_range T>
auto zero_copy(const T& range) -> ZeroCopyT
{
    return { std::as_bytes(std::span{ range }) };
}

} // namespace net

#endif // BLOG_NET_ZERO_COPY_H