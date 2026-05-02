#ifndef BLOG_ASYNC_RUN_H
#define BLOG_ASYNC_RUN_H

#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <co_spawn.h>
#include <this_coroutine.h>

namespace async {

namespace detail {

inline std::vector<IOContext*> active_contexts;
inline std::mutex contexts_mutex;

void push(IOContext& context);

void erase(IOContext& context);

} // namespace detail


/**
 * @brief Start the event loop on the calling thread with a single entry point.
 *
 * Accesses the thread-local `IOContext` (via `this_coroutine::context()`), spawns
 * `std::invoke(awaiter, args...)` as a detached coroutine, then blocks
 * until all outstanding work completes.
 *
 * @tparam Awaiter  Copy-constructible coroutine factory callable.
 * @tparam Args     Copy-constructible argument types forwarded to `awaiter`.
 * @param awaiter   Entry-point factory; called once per thread.
 * @param args      Arguments forwarded to each `awaiter` invocation.
 */
template<typename Awaiter, typename... Args>
    requires std::copy_constructible<Awaiter> && 
             (std::copy_constructible<Args> && ...)
void run(Awaiter&& awaiter, Args&&... args) 
{
    detail::push(this_coroutine::context());

    co_spawn(std::invoke(awaiter, args...));
    this_coroutine::context().run();

    detail::erase(this_coroutine::context());
}

/**
 * @brief Start the event loop on `thread_count` threads with a shared entry point.
 *
 * Spawns `thread_count - 1` additional `std::jthread` instances, each
 * running the same `awaiter` with copies of `args`. The calling thread
 * also participates as the last worker. All threads are joined when the
 * function returns.
 *
 * @tparam Awaiter      Copy-constructible coroutine factory callable.
 * @tparam Args         Copy-constructible argument types forwarded to `awaiter`.
 * @param thread_count  Total number of worker threads (including the caller).
 * @param awaiter       Entry-point factory; called once per thread.
 * @param args          Arguments forwarded to each `awaiter` invocation.
 */
template<typename Awaiter, typename... Args>
    requires std::copy_constructible<Awaiter> && 
             (std::copy_constructible<Args> && ...)
void run(std::integral auto thread_count, Awaiter&& awaiter, Args&&... args) 
{
    std::vector<std::jthread> threads;
    for (int i = 1; i < thread_count; ++i) {
        threads.emplace_back([awaiter, args...]() mutable -> void 
        {
            detail::push(this_coroutine::context());

            co_spawn(std::invoke(awaiter, args...));
            this_coroutine::context().run();
            
            detail::erase(this_coroutine::context());
        });
    }

    run(std::forward<Awaiter>(awaiter), std::forward<Args>(args)...);
}

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
 * @param entries Desired submission queue depth (must be a power of two).
 */
void setup_entries(unsigned entries);

/**
 * @brief Request all active `IOContext` instances to stop their event loops.
 *
 * Iterates the registry of contexts populated by `async::run()` and calls
 * `IOContext::stop()` on each. Threads exit their loops after draining
 * remaining work.
 */
void stop();

} // namespace async

#endif // BLOG_ASYNC_RUN_H