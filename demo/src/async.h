#ifndef BLOG_ASYNC_H
#define BLOG_ASYNC_H

#include <algorithm>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "co_spawn.h"
#include "this_coroutine.h"

namespace async {

namespace detail {

static std::vector<IOContext*> active_contexts;
static std::mutex contexts_mutex;

} // namespace detail

inline void stop()
{
    std::scoped_lock lock{ detail::contexts_mutex };
    std::ranges::for_each(detail::active_contexts, 
        [](IOContext* context) -> void
        {
            context->stop();
        }
    );

    detail::active_contexts.clear();
}

template<typename Awaiter, typename... Args>
    requires std::copy_constructible<Awaiter> && 
             (std::copy_constructible<Args> && ...)
void run(Awaiter&& awaiter, Args&&... args) 
{
    {
        std::scoped_lock locker{ detail::contexts_mutex };
        detail::active_contexts.push_back(&this_coroutine::context());
    }

    co_spawn(std::invoke(awaiter, args...));

    this_coroutine::context().run();
}

template<typename Awaiter, typename... Args>
    requires std::copy_constructible<Awaiter> && 
             (std::copy_constructible<Args> && ...)
void run(int thread_count, Awaiter&& awaiter, Args&&... args) 
{
    std::vector<std::jthread> threads;
    for (int i = 1; i < thread_count; ++i) {
        threads.emplace_back([awaiter, args...]() mutable -> void 
        {
            {
                std::scoped_lock locker{ detail::contexts_mutex };
                detail::active_contexts.push_back(&this_coroutine::context());
            }

            co_spawn(std::invoke(awaiter, args...));
            this_coroutine::context().run();
        });
    }

    run(std::forward<Awaiter>(awaiter), std::forward<Args>(args)...);
}

} // namespace async

#endif // BLOG_ASYNC_H