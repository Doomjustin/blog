#ifndef BLOG_ASYNC_ANY_H
#define BLOG_ASYNC_ANY_H

#include <concepts>
#include <stop_token>
#include <tuple>
#include <utility>

#include <awaitable.h>
#include <scope.h>
#include <task.h>

namespace async {

/**
 * @brief Bind a function and arguments into a stop_awaitable_provider.
 *
 * Automatically detects whether the stop_token goes last or first:
 *   task(f, args...)  →  f(args..., token)  if valid
 *                    →  f(token, args...)  otherwise
 *
 * NOTE: functions passed to task should take std::stop_token by value.
 * Passing by reference can create dangling-reference bugs when used with
 * coroutine frames and should be avoided.
 *
 * Example:
 *   co_await async::any(
 *       async::task(worker_token_last,  "task-A", 120ms),
 *       async::task(worker_token_first, "task-B", 400ms));
 */
template<typename F, typename... Args>
    requires std::invocable<F, std::decay_t<Args>&..., std::stop_token>
auto task(F&& f, Args&&... args)
{
    return [f   = std::forward<F>(f),
            tup = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)](
               std::stop_token token) mutable {
        return std::apply([&](auto&... a) { return f(a..., std::move(token)); }, tup);
    };
}

template<typename F, typename... Args>
    requires std::invocable<F, std::stop_token, std::decay_t<Args>&...>
          && (!std::invocable<F, std::decay_t<Args>&..., std::stop_token>)
auto task(F&& f, Args&&... args)
{
    return [f   = std::forward<F>(f),
            tup = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)](
               std::stop_token token) mutable {
        return std::apply(
            [&](auto&... a) { return f(std::move(token), a...); }, tup);
    };
}

template<typename Provider>
concept stop_awaitable_provider
    = std::invocable<std::decay_t<Provider>, std::stop_token>
   && awaitable<std::invoke_result_t<std::decay_t<Provider>, std::stop_token>>;

/**
 * @brief Heap-allocated named coroutine that wraps a single any provider.
 *
 * Using a named function (rather than an inline lambda) prevents the compiler
 * from applying Heap Elision Optimization to this coroutine frame. The frame
 * must outlive the spawning site (it lives until the inner task finishes and
 * request_stop is called), so eliding the heap allocation and placing the frame
 * on the call stack would produce a dangling reference to `scope`.
 */
template<stop_awaitable_provider Provider>
auto any_spawned_task(Provider provider, Scope& scope) -> Task<>
{
    co_await std::move(provider)(scope.stop_token());
    scope.request_stop();
}

template<stop_awaitable_provider... Providers>
auto any(Providers&&... providers) -> Task<>
{
    static_assert(sizeof...(Providers) > 0, "any requires at least one provider");

    Scope scope;
    (scope.spawn(any_spawned_task(std::forward<Providers>(providers), scope)), ...);
    co_await scope.join();
}

} // namespace async

#endif // BLOG_ASYNC_ANY_H
