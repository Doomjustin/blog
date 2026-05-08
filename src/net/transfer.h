#ifndef BLOG_NET_TRANSFER_H
#define BLOG_NET_TRANSFER_H

#include <net/receive_all_awaiter.h>
#include <net/send_all_awaiter.h>
#include <net/send_all_zc_awaiter.h>
#include <net/zero_copy.h>

namespace net {

template<typename Socket>
concept stream_socket = requires { typename Socket::is_stream_t; } 
                     && Socket::is_stream_t::value;

/**
 * @brief Create a send-all awaiter for the given socket and buffer.
 *
 * Convenience factory that avoids spelling out `SendAllAwaiter` at call
 * sites. The returned awaiter retries `send` until the entire span has
 * been delivered or an error occurs.
 *
 * @tparam Socket Connected stream socket type.
 * @param socket Target connected stream socket.
 * @param view   Read-only byte span to send.
 * @pre `view` must remain valid until the coroutine resumes.
 * @return `SendAllAwaiter` ready to be `co_await`-ed.
 */
template<stream_socket Socket>
auto send(Socket& socket, std::span<const std::byte> view)
{
    return SendAllAwaiter{ socket.context(), socket.native_handle(), view };
}

/**
 * @brief Create a zero-copy send-all awaiter for the given socket.
 *
 * Uses `IORING_OP_SEND_ZC` to avoid copying the buffer into the kernel.
 * Retries until the entire span has been delivered or an error occurs.
 * The buffer wrapped in `zc` must remain valid until the coroutine resumes.
 *
 * @tparam Socket Connected stream socket type.
 * @param socket Target connected stream socket.
 * @param zc     Zero-copy buffer tag wrapping the read-only byte span.
 * @pre The buffer inside `zc` must remain valid until the coroutine resumes.
 * @return `SendAllZCAwaiter` ready to be `co_await`-ed.
 */
template<stream_socket Socket>
auto send(Socket& socket, ZeroCopyT zc)
{
    return SendAllZCAwaiter{ socket.context(), socket.native_handle(), zc.span };
}

/**
 * @brief Create a receive-all awaiter for the given socket and buffer.
 *
 * Convenience factory that retries `recv` until `buffer` is fully
 * filled or an error occurs.
 *
 * @tparam Socket Connected stream socket type.
 * @param socket Target connected stream socket.
 * @param buffer Writable byte span to fill.
 * @pre `buffer` must remain valid until the coroutine resumes.
 * @return `ReceiveAllAwaiter` ready to be `co_await`-ed.
 */
template<stream_socket Socket>
auto receive(Socket& socket, std::span<std::byte> buffer)
{
    return ReceiveAllAwaiter{ socket.context(), socket.native_handle(), buffer };
}

} // namespace net

#endif // BLOG_NET_TRANSFER_H