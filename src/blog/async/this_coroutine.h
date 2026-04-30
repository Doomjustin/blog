#ifndef BLOG_ASYNC_THIS_COROUTINE_H
#define BLOG_ASYNC_THIS_COROUTINE_H

#include "io_context.h"

namespace async::this_coroutine {

auto context() -> IOContext&;

} // namespace async::this_coroutine

#endif // BLOG_ASYNC_THIS_COROUTINE_H