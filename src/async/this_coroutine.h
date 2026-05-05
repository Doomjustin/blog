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


/**
 * @brief Register a buffer ring with the current thread's `IOContext`.
 *
 * Convenience wrapper that forwards to `IOContext::setup_buffer_ring()`
 * using the thread-local context. Call once per thread before issuing
 * multishot receive operations.
 *
 * @param entries Number of buffer slots in the ring (must be a power of two).
 * @param size    Size in bytes of each buffer slot.
 * @return Buffer group ID (`bgid`) to pass to receive operations.
 */
auto setup_buffer_ring(unsigned entries, unsigned size = 4096) -> unsigned;

/**
 * @brief Override the default SQ depth used when creating new `IOContext` instances.
 *
 * Must be called before any thread accesses `this_coroutine::context()`.
 *
 * @param new_entries Desired submission queue depth (must be a power of two).
 */
void setup_entries(unsigned new_entries);

} // namespace async::this_coroutine

#endif // BLOG_ASYNC_THIS_COROUTINE_H