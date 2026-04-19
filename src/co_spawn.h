#ifndef BLOG_CO_SPAWN_H
#define BLOG_CO_SPAWN_H

#include "awaitable.h"
#include "detached_task.h"
#include "tracking_context.h"

template<tracking_context Context, awaitable Awaitable>
    requires std::movable<std::remove_cvref_t<Awaitable>>
auto co_spawn(Context& ctx, Awaitable awaitable) -> DetachedTask<Context>
{
    co_await std::move(awaitable);
}

#endif // BLOG_CO_SPAWN_H