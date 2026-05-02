#ifndef BLOG_ASYNC_THIS_COROUTINE_H
#define BLOG_ASYNC_THIS_COROUTINE_H

#include <io_context.h>

namespace async::this_coroutine {

/**
 * @brief Return the `IOContext` bound to the current thread.
 *
 * Each thread that calls `async::run()` owns a thread-local `IOContext`
 * created on first access. This function provides a stable reference to
 * that instance for use as default arguments in socket and awaiter
 * constructors, removing the need to explicitly thread `IOContext&`
 * through every call site.
 *
 * @note The context is thread-local and initialized on first access. For correct
 *       event-loop behavior it should be driven by `async::run()` before I/O operations
 *       are submitted.
 * @return Reference to the calling thread's `IOContext`.
 */
auto context() -> IOContext&;


namespace detail {

static inline unsigned entries = 1024;

} // namespace detail

} // namespace async::this_coroutine

#endif // BLOG_ASYNC_THIS_COROUTINE_H