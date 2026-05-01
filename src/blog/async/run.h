#ifndef BLOG_ASYNC_RUN_H
#define BLOG_ASYNC_RUN_H

#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "co_spawn.h"
#include "this_coroutine.h"

namespace async {

namespace detail {

inline std::vector<IOContext*> active_contexts;
inline std::mutex contexts_mutex;

void push(IOContext& context);

void erase(IOContext& context);

} // namespace detail


template<typename Awaiter, typename... Args>
    requires std::copy_constructible<Awaiter> && 
             (std::copy_constructible<Args> && ...)
void run(Awaiter&& awaiter, Args&&... args) 
{
    detail::push(this_coroutine::context());

    co_spawn(std::invoke(awaiter, args...));
    this_coroutine::context().run();

    detail::erase(this_coroutine::context());
}

template<typename Awaiter, typename... Args>
    requires std::copy_constructible<Awaiter> && 
             (std::copy_constructible<Args> && ...)
void run(std::integral auto thread_count, Awaiter&& awaiter, Args&&... args) 
{
    std::vector<std::jthread> threads;
    for (int i = 1; i < thread_count; ++i) {
        threads.emplace_back([awaiter, args...]() mutable -> void 
        {
            detail::push(this_coroutine::context());

            co_spawn(std::invoke(awaiter, args...));
            this_coroutine::context().run();
            
            detail::erase(this_coroutine::context());
        });
    }

    run(std::forward<Awaiter>(awaiter), std::forward<Args>(args)...);
}

auto setup_buffer_ring(unsigned entries, unsigned size = 4096) -> unsigned;

void setup_entries(unsigned entries);

void stop();

} // namespace async

#endif // BLOG_ASYNC_RUN_H