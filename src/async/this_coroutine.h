#ifndef BLOG_ASYNC_THIS_COROUTINE_H
#define BLOG_ASYNC_THIS_COROUTINE_H

#include <async/io_context.h>

namespace async::this_coroutine {

/**
 * @brief Return the `IOContext` associated with the current thread.
 *
 * If an `IOContext` is currently running on this thread (via `async::run()`),
 * returns that bound instance. Otherwise returns the thread's dedicated
 * `IOContext`, which is lazily created on first access.
 *
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

/**
 * @brief RAII guard that binds an `IOContext` as the active context for the current thread.
 *
 * On construction, records the previously bound context and installs the new one
 * so that `this_coroutine::context()` returns it for the duration of the guard's
 * lifetime. The previous binding is restored on destruction.
 *
 * Two construction modes:
 * - `ContextBinder()` — lazily obtains (or creates) this thread's dedicated
 *   `IOContext` and binds it. Used by `async::run()`.
 * - `ContextBinder(IOContext&)` — binds the given context explicitly.
 *   Used by `IOContext::run()`.
 */
class ContextBinder {
public:
    ContextBinder() noexcept;
    explicit ContextBinder(IOContext& ctx) noexcept;

    ContextBinder(const ContextBinder&) = delete;
    auto operator=(const ContextBinder&) -> ContextBinder& = delete;

    ~ContextBinder() noexcept;

    [[nodiscard]] auto context() noexcept -> IOContext& { return *context_; }

private:
    IOContext* context_;
    IOContext* previous_;
};

} // namespace async::this_coroutine

#endif // BLOG_ASYNC_THIS_COROUTINE_H