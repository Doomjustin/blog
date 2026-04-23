#ifndef BLOG_DETACHED_TASK_H
#define BLOG_DETACHED_TASK_H

#include <coroutine>
#include <exception>

#include "tracking_context.h"

/**
 * @brief Coroutine return type for fire-and-forget tasks.
 *
 * The spawned coroutine starts immediately (`initial_suspend` returns
 * `std::suspend_never`) and holds no join point. The context work counter
 * is incremented on construction and decremented when the frame is destroyed,
 * so the event loop stays alive until all detached tasks finish.
 *
 * Unhandled exceptions call `std::terminate` to surface bugs early rather
 * than silently swallowing them.
 *
 * @tparam Context Execution context type satisfying `tracking_context`.
 */
template<tracking_context Context>
struct DetachedTask {
    struct promise_type {
        Context* context = nullptr;

        /**
         * @brief Register one unit of outstanding work with the context.
         *
         * @param ctx Context that drives the spawned coroutine.
         * @param awaitable The awaitable being run (accepted but not stored;
         *                  included so the compiler can resolve the constructor).
         */
        /**
         * @param ctx        Context that drives the spawned coroutine.
         * @param awaitable  Forwarded from `co_spawn`'s argument list via coroutine
         *                   promise constructor injection; captured but not stored here.
         */
        template<typename Awaitable>
        promise_type(Context& ctx, Awaitable&& awaitable)
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

        auto initial_suspend() noexcept { return std::suspend_never{}; }

        auto final_suspend() noexcept { return std::suspend_never{}; }

        void return_void() noexcept {}

        void unhandled_exception() noexcept
        {
            std::terminate();
        }
    };
};

#endif // BLOG_DETACHED_TASK_H