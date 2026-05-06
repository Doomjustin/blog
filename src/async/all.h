#ifndef BLOG_ASYNC_ALL_H
#define BLOG_ASYNC_ALL_H

#include <utility>

#include <awaitable.h>
#include <scope.h>
#include <task.h>

namespace async {

/**
 * @brief Run awaitables concurrently and wait until all complete.
 */
template<awaitable... Awaitables>
auto all(Awaitables&&... awaitables) -> Task<>
{
    static_assert(sizeof...(Awaitables) > 0, "all requires at least one awaitable");

    Scope scope;
    (scope.spawn(std::forward<Awaitables>(awaitables)), ...);
    co_await scope.join();
}

} // namespace async

#endif // BLOG_ASYNC_ALL_H
