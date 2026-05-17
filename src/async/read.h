#ifndef BLOG_ASYNC_READ_H
#define BLOG_ASYNC_READ_H

#include <concepts>
#include <cstddef>
#include <expected>
#include <span>
#include <system_error>

#include "buffer.h"
#include "task.h"

namespace async {

template<typename T>
concept ReadableAwaitable = requires(T& awaitable) {
    { awaitable.await_resume() } -> std::same_as<std::expected<std::size_t, std::error_code>>;
};

template<typename T>
concept ReadableStream = requires(T& stream, std::span<std::byte> buffer) {
    { stream.async_read_some(buffer) } -> ReadableAwaitable;
};

template<ReadableStream Stream>
auto read(Stream& stream, std::span<std::byte> buffer) -> Task<std::expected<void, std::error_code>>
{
    std::size_t total_read = 0;

    while (total_read < buffer.size()) {
        auto chunk = buffer.subspan(total_read);

        auto result = co_await stream.async_read_some(chunk);
        if (!result)
            co_return std::unexpected(result.error());

        // 当 result == 0 时，表示对端已关闭连接，无法继续读取数据。
        if (*result == 0)
            co_return unexpected_system_error(std::errc::connection_aborted);

        total_read += *result;
    }

    co_return std::in_place;
}

template<ReadableStream Stream, std::ranges::contiguous_range T>
auto read(Stream& stream, T& range) -> Task<std::expected<void, std::error_code>>
{
    return read(stream, async::buffer(range));
}

template<ReadableStream Stream>
auto read_until(Stream& stream, std::string_view buffer, std::string_view delimiter)
    -> Task<std::expected<std::size_t, std::error_code>>
{
    std::size_t total_read = 0;

    while (total_read < buffer.size()) {
        auto chunk = buffer.substr(total_read);

        auto result = co_await stream.async_read_some(chunk);
        if (!result)
            co_return std::unexpected(result.error());

        if (*result == 0)
            co_return unexpected_system_error(std::errc::connection_aborted);

        auto bytes_read = *result;
        total_read += bytes_read;

        // 在已读取的数据中查找分隔符。
        auto it = std::search(chunk.begin(), chunk.begin() + bytes_read, delimiter.begin(),
                              delimiter.end());
        if (it != chunk.begin() + bytes_read) {
            // 找到分隔符，返回到分隔符位置的总字节数。
            std::size_t position = total_read - (chunk.end() - it);
            co_return position;
        }
    }

    // 如果缓冲区已满但未找到分隔符，返回缓冲区大小。
    co_return buffer.size();
}

} // namespace async

#endif // BLOG_ASYNC_READ_H