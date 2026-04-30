#ifndef BLOG_ASYNC_READ_H
#define BLOG_ASYNC_READ_H

#include "read_all_awaiter.h"

namespace async {

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
auto read(Socket& socket, std::span<std::byte> buffer)
{
    return ReadAllAwaiter{ socket.context(), socket.native_handle(), buffer };
}

} // namespace async

#endif // BLOG_ASYNC_READ_H