#ifndef BLOG_ASYNC_ALL_H
#define BLOG_ASYNC_ALL_H

#include <utility>

#include <async/awaitable.h>
#include <async/task.h>
#include <async/task_group.h> // TaskGroup

namespace async {

/**
 * @brief Run awaitables concurrently and wait until all complete.
 */
template<awaitable... Awaitables>
auto all(Awaitables&&... awaitables) -> Task<>
{
    static_assert(sizeof...(Awaitables) > 0, "all requires at least one awaitable");

    TaskGroup group;
    (group.spawn(std::forward<Awaitables>(awaitables)), ...);
    co_await group.join();
}

} // namespace async

#endif // BLOG_ASYNC_ALL_H
