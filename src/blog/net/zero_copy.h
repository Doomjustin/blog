#ifndef BLOG_NET_ZERO_COPY_H
#define BLOG_NET_ZERO_COPY_H

#include <ranges>
#include <span>

namespace net {

struct ZeroCopyT {
    std::span<const std::byte> span;
};

template<std::ranges::contiguous_range T>
auto zero_copy(const T& range) -> ZeroCopyT
{
    return { std::as_bytes(std::span{ range }) };
}

} // namespace net

#endif // BLOG_NET_ZERO_COPY_H