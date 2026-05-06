#ifndef BLOG_ASYNC_RACE_H
#define BLOG_ASYNC_RACE_H

#include <concepts>
#include <type_traits>
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
 * Example:
 *   co_await async::race(
 *       async::task(worker_token_last,  "task-A", 120ms),
 *       async::task(worker_token_first, "task-B", 400ms));
 */
template<typename F, typename... Args>
    requires std::invocable<F, std::decay_t<Args>&..., std::stop_token&>
auto task(F&& f, Args&&... args)
{
    return [f   = std::forward<F>(f),
            tup = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)](
               std::stop_token token) mutable {
                    return std::apply([&](auto&... a) { return f(a..., token); }, 
                tup);
    };
}

template<typename F, typename... Args>
        requires std::invocable<F, std::stop_token&, std::decay_t<Args>&...>
            && (!std::invocable<F, std::decay_t<Args>&..., std::stop_token&>)
auto task(F&& f, Args&&... args)
{
    return [f   = std::forward<F>(f),
            tup = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)](
               std::stop_token token) mutable {
        return std::apply(
            [&](auto&... a) { return f(token, a...); }, tup);
    };
}

template<typename Provider>
concept stop_awaitable_provider
    = std::invocable<std::decay_t<Provider>, std::stop_token>
   && awaitable<std::invoke_result_t<std::decay_t<Provider>, std::stop_token>>;

template<stop_awaitable_provider... Providers>
auto race(Providers&&... providers) -> Task<>
{
    static_assert(sizeof...(Providers) > 0, "race requires at least one provider");

    Scope scope;
    auto spawn_one = [&](auto p) {
        scope.spawn([p = std::move(p), &scope]() mutable -> Task<> {
            co_await std::move(p)(scope.stop_token());
            scope.request_stop();
        }());
    };
    (spawn_one(std::forward<Providers>(providers)), ...);
    co_await scope.join();
}

} // namespace async

#endif // BLOG_ASYNC_RACE_H
