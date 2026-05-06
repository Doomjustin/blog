#ifndef BLOG_ASYNC_RACE_H
#define BLOG_ASYNC_RACE_H

#include <atomic>
#include <concepts>
#include <memory>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>

#include <awaitable.h>
#include <co_spawn.h>
#include <stop_requested_awaiter.h>
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

template<typename Provider>
auto race_spawned_task(Provider provider,
                       std::stop_source stop,
                       std::stop_source all_done,
                       std::shared_ptr<std::atomic_size_t> pending) -> Task<>
{
    co_await std::move(provider)(stop.get_token());

    stop.request_stop();

    if (pending->fetch_sub(1, std::memory_order_acq_rel) == 1)
        all_done.request_stop();
}

/**
 * @brief Race providers concurrently; first completion requests stop for the rest.
 */
template<stop_awaitable_provider... Providers>
auto race(Providers&&... providers) -> Task<>
{
    static_assert(sizeof...(Providers) > 0, "race requires at least one provider");

    std::stop_source stop;
    std::stop_source all_done;
    auto pending = std::make_shared<std::atomic_size_t>(sizeof...(Providers));

    auto owned = std::tuple<std::decay_t<Providers>...>(std::forward<Providers>(providers)...);
    std::apply(
        [&](auto&... ps) -> void {
            (co_spawn(race_spawned_task(std::move(ps), stop, all_done, pending)), ...);
        },
        owned);

    co_await StopRequestedAwaiter(all_done.get_token());
}

} // namespace async

#endif // BLOG_ASYNC_RACE_H
