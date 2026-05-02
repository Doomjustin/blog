#ifndef BLOG_NET_UTILITY_H
#define BLOG_NET_UTILITY_H

#include <receive_all_awaiter.h>
#include <send_all_awaiter.h>

namespace net {

/**
 * @brief Create a send-all awaiter for the given socket and buffer.
 *
 * Convenience factory that avoids spelling out `SendAllAwaiter` at call
 * sites. The returned awaiter retries `send` until the entire span has
 * been delivered or an error occurs.
 *
 * @tparam Socket Any socket type exposing `context()` and `native_handle()`.
 * @param socket Target connected socket.
 * @param view   Read-only byte span to send.
 * @pre `view` must remain valid until the coroutine resumes.
 * @return `SendAllAwaiter` ready to be `co_await`-ed.
 */
template<typename Socket>
auto send(Socket& socket, std::span<const std::byte> view)
{
    return SendAllAwaiter{ socket.context(), socket.native_handle(), view };
}

/**
 * @brief Create a receive-all awaiter for the given socket and buffer.
 *
 * Convenience factory that retries `recv` until `buffer` is fully
 * filled or an error occurs.
 *
 * @tparam Socket Any socket type exposing `context()` and `native_handle()`.
 * @param socket Target connected socket.
 * @param buffer Writable byte span to fill.
 * @pre `buffer` must remain valid until the coroutine resumes.
 * @return `ReceiveAllAwaiter` ready to be `co_await`-ed.
 */
template<typename Socket>
auto receive(Socket& socket, std::span<std::byte> buffer)
{
    return ReceiveAllAwaiter{ socket.context(), socket.native_handle(), buffer };
}

} // namespace net

#endif // BLOG_NET_UTILITY_H