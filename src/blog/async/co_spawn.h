#ifndef BLOG_ASYNC_CO_SPAWN_H
#define BLOG_ASYNC_CO_SPAWN_H

#include "awaitable.h"
#include "detached_task.h"

namespace async {
    
template<awaitable Awaitable>
    requires std::movable<std::remove_cvref_t<Awaitable>>
auto co_spawn(Awaitable awaitable) -> DetachedTask
{
    co_await std::move(awaitable);
}

} // namespace async

#endif // BLOG_ASYNC_CO_SPAWN_H