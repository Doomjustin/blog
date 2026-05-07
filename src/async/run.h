#ifndef BLOG_ASYNC_RUN_H
#define BLOG_ASYNC_RUN_H

#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <co_spawn.h>
#include <task.h>
#include <this_coroutine.h>

namespace async {

namespace detail {

inline std::vector<IOContext*> active_contexts;
inline std::mutex contexts_mutex;

void push(IOContext& context);

void erase(IOContext& context);

struct ContextGuard {
    explicit ContextGuard(IOContext& ctx) 
      : ctx_{ ctx } 
    { 
        push(ctx_); 
    }

    ContextGuard(const ContextGuard&) = delete;
    auto operator=(const ContextGuard&) -> ContextGuard& = delete;

    ~ContextGuard() 
    { 
        erase(ctx_); 
    }

private:
    IOContext& ctx_;
};

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
    this_coroutine::ContextBinder binder;
    detail::ContextGuard guard{ binder.context() };
    co_spawn(std::invoke(awaiter, args...), binder.context());
    binder.context().run();
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
            this_coroutine::ContextBinder binder;
            detail::ContextGuard guard{ binder.context() };
            co_spawn(std::invoke(awaiter, args...), binder.context());
            binder.context().run();
        });
    }

    run(std::forward<Awaiter>(awaiter), std::forward<Args>(args)...);
}

/**
 * @brief Start the event loop with a stop_token-aware entry point.
 *
 * The awaiter is invoked with the `stop_token` from the provided `stop_source`
 * as its only argument. This simplifies patterns like `async::stop_then` by
 * automatically passing the token without manual lifecycle management.
 *
 * Example:
 * ```cpp
 * std::stop_source source;
 * async::run(source, [](std::stop_token token) -> async::Task<> {
 *     co_await async::stop_then(async::sleep_for(3s), token);
 * });
 * ```
 *
 * @tparam Awaiter  Callable that accepts `std::stop_token` and returns an awaitable.
 * @param source    The `stop_source` whose token is passed to the awaiter.
 * @param awaiter   Function/lambda that takes a `std::stop_token` parameter.
 */
template<typename Awaiter>
    requires std::copy_constructible<Awaiter>
void run(std::stop_source& source, Awaiter&& awaiter)
{
    auto coro = [token = source.get_token(), f = std::forward<Awaiter>(awaiter)]() mutable -> Task<>
    {
        co_await std::invoke(f, token);
    };
    
    run(coro);
}

/**
 * @brief Start the event loop on `thread_count` threads with stop_token support.
 *
 * Similar to the stop_source-aware single-threaded version, but spawns
 * the entry point on `thread_count` threads. The `stop_source` is shared
 * across all threads.
 *
 * @tparam Awaiter      Callable that accepts `std::stop_token` and returns an awaitable.
 * @param thread_count  Total number of worker threads (including the caller).
 * @param source        The `stop_source` whose token is passed to the awaiter.
 * @param awaiter       Function/lambda that takes a `std::stop_token` parameter.
 */
template<typename Awaiter>
    requires std::copy_constructible<Awaiter>
void run(std::integral auto thread_count, std::stop_source& source, Awaiter&& awaiter)
{
    auto coro = [token = source.get_token(), f = std::forward<Awaiter>(awaiter)]() mutable -> Task<>
    {
        co_await std::invoke(f, token);
    };
    
    run(thread_count, coro);
}

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