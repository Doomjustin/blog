#ifndef BLOG_DETACHED_TASK_H
#define BLOG_DETACHED_TASK_H

#include "tracking_context.h"

#include <coroutine>
#include <exception>


template<tracking_context Context>
struct DetachedTask {
    struct promise_type {
        Context* context = nullptr;

        template<typename Awaitable>
        promise_type(Context& ctx, Awaitable&& awaitable)
          : context{ &ctx }
        {
            context->add_work();
        }

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