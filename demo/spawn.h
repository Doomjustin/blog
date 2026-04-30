#ifndef BLOG_CO_SPAWN_H
#define BLOG_CO_SPAWN_H

#include "awaitable.h"
#include "detached_task.h"
#include "tracking_context.h"

/**
 * @brief Spawn an awaitable on a tracking context without join semantics.
 *
 * This helper starts a coroutine and detaches completion from the caller.
 * It is suitable for fire-and-forget side tasks such as accept loops,
 * periodic timers, or background housekeeping.
 *
 * @tparam Context Execution context type satisfying `tracking_context`.
 * @tparam Awaitable Awaitable type consumed by this call.
 * @param ctx Target context that tracks and drives the spawned coroutine.
 * @param awaitable Awaitable object to execute; moved into the coroutine body.
 * @return `DetachedTask<Context>`; callers may discard the return value — the coroutine
 *         runs independently and is tracked via the context work counter.
 * @pre `ctx` must outlive the returned detached task.
 * @code
 * co_spawn(ctx, []() -> Task<void> {
 *     co_await sleep_for(std::chrono::seconds{1});
 * }());
 * @endcode
 */
template<tracking_context Context, awaitable Awaitable>
    requires std::movable<std::remove_cvref_t<Awaitable>>
auto co_spawn(Context& ctx, Awaitable awaitable) -> DetachedTask<Context>
{
    co_await std::move(awaitable);
}

#endif // BLOG_CO_SPAWN_H