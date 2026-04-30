#ifndef BLOG_ASYNC_OPERATIONS_H
#define BLOG_ASYNC_OPERATIONS_H

#include "read_all_awaiter.h"
#include "write_all_awaiter.h"

/**
 * @brief Create a write-all awaiter for the given socket and buffer.
 *
 * Convenience factory that avoids spelling out `WriteAllAwaiter` at call
 * sites. The returned awaiter retries `send` until the entire span has
 * been delivered or an error occurs.
 *
 * @tparam Socket Any socket type exposing `context()` and `native_handle()`.
 * @param socket Target connected socket.
 * @param view   Read-only byte span to send.
 * @pre `view` must remain valid until the coroutine resumes.
 * @return `WriteAllAwaiter` ready to be `co_await`-ed.
 */
template<typename Socket>
auto async_write(Socket& socket, std::span<const std::byte> view)
{
    return WriteAllAwaiter{ socket.context(), socket.native_handle(), view };
}

/**
 * @brief Create a read-all awaiter for the given socket and buffer.
 *
 * Convenience factory that avoids spelling out `ReadAllAwaiter` at call
 * sites. The returned awaiter retries `recv` until the entire buffer is
 * filled or an error occurs.
 *
 * @tparam Socket Any socket type exposing `context()` and `native_handle()`.
 * @param socket Source connected socket.
 * @param buffer Writable byte span to fill.
 * @pre `buffer` must remain valid until the coroutine resumes.
 * @return `ReadAllAwaiter` ready to be `co_await`-ed.
 */
template<typename Socket>
auto async_read(Socket& socket, std::span<std::byte> buffer)
{
    return ReadAllAwaiter{ socket.context(), socket.native_handle(), buffer };
}

#endif // BLOG_ASYNC_OPERATIONS_H