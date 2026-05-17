#ifndef BLOG_ASYNC_WRITE_H
#define BLOG_ASYNC_WRITE_H

#include <concepts>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

#include "buffer.h"
#include "task.h"

namespace async {

template<typename T>
concept WritableAwaitable = requires(T& awaitable) {
    { awaitable.await_resume() } -> std::same_as<std::expected<std::size_t, std::error_code>>;
};

template<typename T>
concept WritableStream = requires(T& stream, std::span<const std::byte> buffer) {
    { stream.async_write_some(buffer) } -> WritableAwaitable;
};

template<WritableStream Stream>
auto write(Stream& stream, std::span<const std::byte> buffer)
    -> Task<std::expected<void, std::error_code>>
{
    std::size_t total_write = 0;

    while (total_write < buffer.size()) {
        auto chunk = buffer.subspan(total_write);

        auto result = co_await stream.async_write_some(chunk);
        if (!result)
            co_return std::unexpected(result.error());

        // 当 result == 0 时，表示对端已关闭连接，无法继续写入数据。
        if (*result == 0)
            co_return unexpected_system_error(std::errc::connection_aborted);

        total_write += *result;
    }

    co_return std::in_place;
}

template<WritableStream Stream, std::ranges::contiguous_range T>
auto write(Stream& stream, const T& range) -> Task<std::expected<void, std::error_code>>
{
    return write(stream, async::buffer(range));
}

} // namespace async

#endif // BLOG_ASYNC_WRITE_H