#ifndef BLOG_DETACHED_TASK_H
#define BLOG_DETACHED_TASK_H

#include <coroutine>
#include <exception>

#include <async/io_context.h>
#include <async/this_coroutine.h>

namespace async {

/**
 * @brief Coroutine return type for fire-and-forget tasks launched by `co_spawn`.
 *
 * The coroutine starts immediately (`initial_suspend` returns `suspend_never`)
 * and destroys its frame on completion without suspending. Lifetime is tracked
 * via the context's work counter so the event loop does not exit while detached
 * coroutines are still running.
 */
struct DetachedTask {
    struct promise_type {
        IOContext* context = nullptr;

        /**
         * @brief Register one unit of outstanding work with the context.
         *
         * Invoked via coroutine promise constructor injection using `co_spawn`'s
         * argument list. `awaitable` is accepted to satisfy the injection protocol
         * but is not stored here; it is moved into the coroutine body by the
         * compiler-generated frame setup.
         *
         * @param ctx       Context that drives the spawned coroutine.
         * @param awaitable Awaitable forwarded from `co_spawn`; not stored.
         */
        template<typename Awaitable>
        promise_type(Awaitable&& awaitable, IOContext& ctx = this_coroutine::context())
          : context{ &ctx }
        {
            context->add_work();
        }

        /**
         * @brief Release the previously registered work unit.
         */
        ~promise_type()
        {
            context->drop_work();
        }

        auto get_return_object() noexcept { return DetachedTask{}; }

        /** @brief Start executing immediately; the spawner does not wait. */
        auto initial_suspend() noexcept { return std::suspend_never{}; }

        /**
         * @brief Destroy the frame on completion without suspending.
         *
         * There is no join point, so the frame can be reclaimed immediately.
         * The work counter is decremented by the destructor.
         */
        auto final_suspend() noexcept { return std::suspend_never{}; }

        void return_void() noexcept {}

        /**
         * @brief Terminate the process on unhandled exceptions.
         *
         * Silently swallowing exceptions in detached tasks hides bugs that are
         * otherwise impossible to diagnose. Calling `std::terminate` surfaces
         * them immediately with a stack trace.
         */
        void unhandled_exception() noexcept
        {
            std::terminate();
        }
    };
};

} // namespace async

#endif // BLOG_DETACHED_TASK_H