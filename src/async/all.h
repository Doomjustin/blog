#ifndef BLOG_ASYNC_ALL_H
#define BLOG_ASYNC_ALL_H

#include <atomic>
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

template<typename Awaitable>
auto all_spawned_task(Awaitable awaitable,
                      std::stop_source all_done,
                      std::shared_ptr<std::atomic_size_t> pending) -> Task<>
{
    co_await std::move(awaitable);

    if (pending->fetch_sub(1, std::memory_order_acq_rel) == 1)
        all_done.request_stop();
}

/**
 * @brief Run awaitables concurrently and wait until all complete.
 */
template<awaitable... Awaitables>
auto all(Awaitables&&... awaitables) -> Task<>
{
    static_assert(sizeof...(Awaitables) > 0, "all requires at least one awaitable");

    std::stop_source all_done;
    auto pending = std::make_shared<std::atomic_size_t>(sizeof...(Awaitables));

    auto owned = std::tuple<std::decay_t<Awaitables>...>(std::forward<Awaitables>(awaitables)...);
    std::apply(
        [&](auto&... as) -> void {
            (co_spawn(all_spawned_task(std::move(as), all_done, pending)), ...);
        },
        owned);

    co_await StopRequestedAwaiter(all_done.get_token());
}

} // namespace async

#endif // BLOG_ASYNC_ALL_H
