#ifndef BLOG_ASYNC_CO_SPAWN_H
#define BLOG_ASYNC_CO_SPAWN_H

#include <async/awaitable.h>
#include <async/detached_task.h>
#include <async/shift_to.h>

namespace async {

/**
 * @brief Launch an awaitable as a fire-and-forget detached coroutine.
 *
 * The coroutine is scheduled on the current thread's `IOContext` and runs
 * independently of the caller. Ownership of `awaitable` is moved into the
 * new coroutine frame; the caller does not receive a handle and cannot
 * join the task.
 *
 * Unhandled exceptions inside the spawned coroutine terminate the process
 * (see `DetachedTask::promise_type::unhandled_exception`).
 *
 * @tparam Awaitable Awaitable type satisfying the `awaitable` concept.
 * @param awaitable  Awaitable to execute; moved into the coroutine frame.
 */
template<awaitable Awaitable>
    requires std::movable<std::remove_cvref_t<Awaitable>>
auto co_spawn(Awaitable awaitable, IOContext& context = this_coroutine::context()) -> DetachedTask
{
    co_await shift_to(context);
    co_await std::move(awaitable);
}

} // namespace async

#endif // BLOG_ASYNC_CO_SPAWN_H