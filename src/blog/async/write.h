#ifndef BLOG_ASYNC_WRITE_H
#define BLOG_ASYNC_WRITE_H

#include "write_all_awaiter.h"

namespace async {

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
auto write(Socket& socket, std::span<const std::byte> view)
{
    return WriteAllAwaiter{ socket.context(), socket.native_handle(), view };
}

} // namespace async

#endif // BLOG_ASYNC_WRITE_H