#ifndef BLOG_ASYNC_READ_H
#define BLOG_ASYNC_READ_H

#include <concepts>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

#include "task.h"

namespace async {

template<typename T>
concept ReadableAwaitable = requires(T& awaitable) {
    { awaitable.await_resume() } -> std::same_as<std::expected<std::size_t, std::error_code>>;
};

template<typename T>
concept ReadableStream = requires(T& stream, std::span<std::byte> buffer) {
    { stream.async_read(buffer) } -> ReadableAwaitable;
};

template<ReadableStream Stream>
auto read(Stream& stream, std::span<std::byte> buffer) -> Task<std::expected<void, std::error_code>>
{
    std::size_t total_read = 0;

    while (total_read < buffer.size()) {
        auto chunk = buffer.subspan(total_read);

        auto result = co_await stream.async_read(chunk);
        if (!result)
            co_return std::unexpected(result.error());

        // 当 result == 0 时，表示对端已关闭连接，无法继续读取数据。
        if (*result == 0)
            co_return unexpected_system_error(std::errc::connection_aborted);

        total_read += *result;
    }

    co_return std::in_place;
}

} // namespace async

#endif // BLOG_ASYNC_READ_H