#ifndef BLOG_ASYNC_THIS_CORO_H
#define BLOG_ASYNC_THIS_CORO_H

namespace async::this_coro {

struct context_tag {};

struct stop_token_tag {};

constexpr context_tag context{};

constexpr stop_token_tag stop_token{};

} // namespace async::this_coro

#endif // BLOG_ASYNC_THIS_CORO_H